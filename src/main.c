/**
 * @file main.c
 * @brief Главный исполняемый модуль Охранной Видеосистемы (ОВС)
 * 
 * Интегрирует все модули: управление питанием, камеру, NPU, запись и GSM.
 * Соответствует ТЗ п. 1.a.ix (Режимы работы) и п. 1.a.viii (Задачи системы).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>

// Подключение наших модулей
#include "power_daemon/gpio_handler.h"
#include "power_daemon/power_manager.h"
#include "camera_driver/v4l2_capture.h"
#include "inference_engine/detector.h"
#include "video_recorder/recorder.h"
#include "gsm_module/sms_sender.h"

// Глобальные переменные состояния
static volatile int running = 1;
static system_state_t current_state = STATE_DEEP_SLEEP;
static camera_context_t cam_ctx;
static int is_recording_active = 0;

// Обработчик сигналов завершения (Ctrl+C)
void signal_handler(int signum) {
    syslog(LOG_INFO, "Получен сигнал %d, корректное завершение работы...", signum);
    running = 0;
}

// Инициализация всех подсистем
int system_init_all(void) {
    syslog(LOG_INFO, "=== Инициализация ОВС ===");
    
    if (gpio_init() != 0) return -1;
    if (power_manager_init() != 0) return -1;
    if (aes256_init() != 0) return -1; // Инициализация шифрования
    if (recorder_init() != 0) return -1; // Инициализация кольцевого буфера
    if (gsm_init() != 0) {
        syslog(LOG_WARNING, "GSM модуль не ответил, продолжаем без SMS-уведомлений");
    }
    
    // Камеру инициализируем, но не запускаем поток до режима ACTIVE (экономия энергии)
    if (camera_init(&cam_ctx, "/dev/video0", 640, 480) != 0) {
        syslog(LOG_ERR, "Критическая ошибка: камера не найдена");
        return -1;
    }

    // Инициализация NPU (модель должна лежать в /etc/ovs/models/yolo_fastest.rknn)
    if (detector_init("/etc/ovs/models/yolo_fastest.rknn") != 0) {
        syslog(LOG_ERR, "Критическая ошибка: не удалось инициализировать NPU");
        return -1;
    }

    syslog(LOG_INFO, "=== Все системы инициализированы ===");
    return 0;
}

// Освобождение ресурсов
void system_cleanup_all(void) {
    syslog(LOG_INFO, "=== Остановка ОВС ===");
    detector_deinit();
    camera_deinit(&cam_ctx);
    recorder_deinit();
    aes256_deinit();
    gsm_deinit();
    power_manager_cleanup();
    gpio_cleanup();
    syslog(LOG_INFO, "Ресурсы освобождены");
}

// Главный цикл работы
void main_loop(void) {
    syslog(LOG_INFO, "Запуск главного цикла. Текущий режим: DEEP_SLEEP");
    power_manager_sleep(); // Переходим в режим охраны (п. 1.a.ix.1)

    while (running) {
        // 1. Проверка аппаратных прерываний (PIR / G-сенсор)
        sensor_event_t event = gpio_check_interrupts();

        if (event.type != EVENT_NONE) {
            syslog(LOG_INFO, "Событие от датчика: %d", event.type);

            if (current_state == STATE_DEEP_SLEEP) {
                // Переход в режим ВЕРИФИКАЦИЯ (п. 1.a.ix.2)
                syslog(LOG_INFO, "Пробуждение... Переход в режим ACTIVE");
                power_manager_wakeup();
                camera_start(&cam_ctx);
                current_state = STATE_ACTIVE;
            }

            if (current_state == STATE_ACTIVE) {
                // Захват кадра и инференс на NPU (п. 1.a.viii.2, время ≤ 150 мс)
                uint8_t* frame_data = NULL;
                size_t frame_size = 0;
                
                if (camera_get_frame(&cam_ctx, &frame_data, &frame_size) >= 0) {
                    // Записываем кадр в кольцевой буфер (для сохранения контекста "до" тревоги)
                    recorder_write_frame(frame_data, frame_size);

                    detection_result_t det_result;
                    // Запуск инференса (упрощенный вызов, реальный размер зависит от модели, напр. 320x320)
                    if (detector_run_inference(frame_data, 320, 320, &det_result) == 0) {
                        if (det_result.person_detected) {
                            syslog(LOG_WARNING, "NPU: ОБНАРУЖЕН ЧЕЛОВЕК! Переход в режим ТРЕВОГА");
                            current_state = STATE_ALARM;
                            power_manager_trigger_alarm();
                            
                            // Сброс буфера на SD с шифрованием (п. 1.a.v, 1.a.vi)
                            char filename[128];
                            time_t t = time(NULL);
                            snprintf(filename, sizeof(filename), "/mnt/microsd/archive/event_%ld.enc", t);
                            recorder_flush_to_sd(filename);
                            is_recording_active = 1;

                            // Активное противодействие (п. 1.a.viii.4)
                            gpio_siren_on();

                            // Уведомление (п. 1.a.viii.5)
                            gsm_send_sms("+79991234567", "ТРЕВОГА: Обнаружен человек у автомобиля!");
                        } else {
                            syslog(LOG_INFO, "NPU: Человек не обнаружен (ложное срабатывание). Возврат в сон.");
                            current_state = STATE_DEEP_SLEEP;
                            camera_deinit(&cam_ctx); // Выключаем камеру для экономии
                            power_manager_sleep();
                        }
                    }
                    camera_release_frame(&cam_ctx);
                }
            }

            if (current_state == STATE_ALARM) {
                // В режиме тревоги продолжаем писать напрямую на SD (п. 1.a.ix.3)
                uint8_t* frame_data = NULL;
                size_t frame_size = 0;
                if (camera_get_frame(&cam_ctx, &frame_data, &frame_size) >= 0) {
                    recorder_write_direct_to_sd(frame_data, frame_size);
                    camera_release_frame(&cam_ctx);
                }

                // Проверка завершения тревоги (например, через 30 секунд)
                if (power_manager_alarm_completed()) {
                    syslog(LOG_INFO, "Тревога завершена. Возврат в режим ОХРАНА");
                    gpio_siren_off();
                    is_recording_active = 0;
                    current_state = STATE_DEEP_SLEEP;
                    camera_deinit(&cam_ctx);
                    power_manager_sleep();
                }
            }
        }

        // Обновление сторожевого таймера (WDT) (п. 1.a.ii)
        power_manager_update_watchdog();
        
        // Небольшая задержка, чтобы не грузить CPU на 100%
        usleep(10000); // 10 мс
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    openlog("ovs_system_daemon", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Запуск демона ОВС (PID: %d)", getpid());

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (system_init_all() != 0) {
        syslog(LOG_ERR, "Критическая ошибка инициализации. Завершение.");
        closelog();
        return EXIT_FAILURE;
    }

    main_loop();
    system_cleanup_all();
    
    closelog();
    return EXIT_SUCCESS;
}