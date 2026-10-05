/**
 * @file main.c
 * @brief Главный модуль демона управления питанием ОВС
 * 
 * Демон обрабатывает прерывания от PIR-датчика и G-сенсора,
 * управляет переходами между режимами работы системы:
 * - DEEP_SLEEP: потребление < 5 мА, активен только контроллер прерываний
 * - ACTIVE: пробуждение SoC, захват кадра, инференс на NPU (до 3 сек)
 * - ALARM: запись видео, сирена, GSM-уведомление
 * - SERVICE: USB/UART для обслуживания
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include <errno.h>

#include "gpio_handler.h"
#include "power_manager.h"

// Глобальные переменные
static volatile int running = 1;
static system_state_t current_state = STATE_DEEP_SLEEP;

// Обработчик сигналов завершения
void signal_handler(int signum) {
    syslog(LOG_INFO, "Получен сигнал %d, завершение работы...", signum);
    running = 0;
}

// Инициализация системы
int system_init(void) {
    syslog(LOG_INFO, "Инициализация охранной видеосистемы...");
    
    // Инициализация GPIO (PIR, G-сенсор, сирена)
    if (gpio_init() != 0) {
        syslog(LOG_ERR, "Ошибка инициализации GPIO");
        return -1;
    }
    
    // Инициализация менеджера питания
    if (power_manager_init() != 0) {
        syslog(LOG_ERR, "Ошибка инициализации менеджера питания");
        return -1;
    }
    
    syslog(LOG_INFO, "Система инициализирована успешно");
    return 0;
}

// Освобождение ресурсов
void system_cleanup(void) {
    syslog(LOG_INFO, "Освобождение ресурсов...");
    gpio_cleanup();
    power_manager_cleanup();
    syslog(LOG_INFO, "Система остановлена");
}

// Основной цикл обработки событий
void main_loop(void) {
    syslog(LOG_INFO, "Запуск основного цикла");
    
    while (running) {
        // Проверка прерываний от датчиков
        sensor_event_t event = gpio_check_interrupts();
        
        if (event.type != EVENT_NONE) {
            syslog(LOG_INFO, "Получено событие от датчика: %d", event.type);
            
            switch (current_state) {
                case STATE_DEEP_SLEEP:
                    // Пробуждение системы
                    syslog(LOG_INFO, "Пробуждение из Deep Sleep...");
                    if (power_manager_wakeup() == 0) {
                        current_state = STATE_ACTIVE;
                        syslog(LOG_INFO, "Система в режиме ACTIVE");
                    }
                    break;
                    
                case STATE_ACTIVE:
                    // Обработка события в активном режиме
                    if (event.type == EVENT_PERSON_DETECTED) {
                        syslog(LOG_WARNING, "Обнаружен человек! Переход в ALARM");
                        current_state = STATE_ALARM;
                        power_manager_trigger_alarm();
                    } else if (event.type == EVENT_TIMEOUT) {
                        // Таймаут верификации (3 секунды по ТЗ)
                        syslog(LOG_INFO, "Таймаут верификации, возврат в DEEP_SLEEP");
                        current_state = STATE_DEEP_SLEEP;
                        power_manager_sleep();
                    }
                    break;
                    
                case STATE_ALARM:
                    // В режиме тревоги ждем завершения
                    if (power_manager_alarm_completed()) {
                        syslog(LOG_INFO, "Тревога завершена, возврат в DEEP_SLEEP");
                        current_state = STATE_DEEP_SLEEP;
                        power_manager_sleep();
                    }
                    break;
                    
                case STATE_SERVICE:
                    // Сервисный режим (обслуживание по USB)
                    syslog(LOG_INFO, "Сервисный режим активен");
                    break;
            }
        }
        
        // Обновление сторожевого таймера
        power_manager_update_watchdog();
        
        // Небольшая задержка для снижения нагрузки на CPU
        usleep(10000); // 10 мс
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    // Открытие syslog
    openlog("ovs_power_daemon", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Демон управления питанием запущен (PID: %d)", getpid());
    
    // Регистрация обработчиков сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Инициализация системы
    if (system_init() != 0) {
        syslog(LOG_ERR, "Ошибка инициализации системы");
        closelog();
        return EXIT_FAILURE;
    }
    
    // Переход в режим Deep Sleep
    current_state = STATE_DEEP_SLEEP;
    power_manager_sleep();
    
    // Основной цикл
    main_loop();
    
    // Освобождение ресурсов
    system_cleanup();
    closelog();
    
    return EXIT_SUCCESS;
}
