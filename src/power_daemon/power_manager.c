/**
 * @file power_manager.c
 * @brief Управление режимами питания системы
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <time.h>

#include "power_manager.h"
#include "gpio_handler.h"

// Состояния системы
typedef enum {
    STATE_DEEP_SLEEP,
    STATE_ACTIVE,
    STATE_ALARM,
    STATE_SERVICE
} system_state_t;

// Таймеры
static time_t wakeup_time = 0;
static time_t alarm_start_time = 0;

// Инициализация менеджера питания
int power_manager_init(void) {
    syslog(LOG_INFO, "Инициализация менеджера питания");
    // Здесь можно добавить инициализацию WDT, DC-DC и т.д.
    return 0;
}

// Переход в режим Deep Sleep
int power_manager_sleep(void) {
    syslog(LOG_INFO, "Переход в режим DEEP_SLEEP");
    
    // Выключаем камеру
    gpio_camera_off();
    
    // Выключаем сирену (если была включена)
    gpio_siren_off();
    
    // Здесь можно добавить команды для отключения питания SoC
    // и перехода в режим низкого энергопотребления
    
    return 0;
}

// Пробуждение системы
int power_manager_wakeup(void) {
    syslog(LOG_INFO, "Пробуждение системы");
    
    // Включаем питание SoC и камеры
    gpio_camera_on();
    
    // Ждем стабилизации питания камеры (200 мс по ТЗ)
    usleep(200000);
    
    wakeup_time = time(NULL);
    
    // Здесь запускается процесс инференса нейросети
    // (будет реализовано в inference_engine)
    
    return 0;
}

// Активация режима тревоги
void power_manager_trigger_alarm(void) {
    syslog(LOG_WARNING, "АКТИВАЦИЯ РЕЖИМА ТРЕВОГИ");
    
    alarm_start_time = time(NULL);
    
    // Включаем сирену
    gpio_siren_on();
    
    // Здесь запускается:
    // 1. Запись видео на MicroSD
    // 2. Отправка GSM-уведомления
    // 3. Сохранение кадра в архив
}

// Проверка завершения тревоги
int power_manager_alarm_completed(void) {
    time_t now = time(NULL);
    
    // Тревога длится 30 секунд (настраивается)
    if (difftime(now, alarm_start_time) >= 30.0) {
        syslog(LOG_INFO, "Режим тревоги завершен");
        gpio_siren_off();
        return 1;
    }
    
    return 0;
}

// Обновление сторожевого таймера
void power_manager_update_watchdog(void) {
    // Здесь нужно писать в /dev/watchdog для сброса таймера
    // write(wdt_fd, "1", 1);
}

// Освобождение ресурсов
void power_manager_cleanup(void) {
    syslog(LOG_INFO, "Очистка менеджера питания");
}