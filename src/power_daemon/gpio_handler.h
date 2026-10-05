/**
 * @file gpio_handler.h
 * @brief Интерфейс для работы с GPIO
 */

#ifndef GPIO_HANDLER_H
#define GPIO_HANDLER_H

#include <time.h>

// Типы событий от датчиков
typedef enum {
    EVENT_NONE = 0,
    EVENT_PIR_TRIGGER,        // Срабатывание PIR-датчика
    EVENT_G_SENSOR_TRIGGER,   // Срабатывание G-сенсора
    EVENT_PERSON_DETECTED,    // Нейросеть обнаружила человека
    EVENT_TIMEOUT             // Таймаут верификации
} sensor_event_type_t;

// Структура события от датчика
typedef struct {
    sensor_event_type_t type;
    time_t timestamp;
} sensor_event_t;

// Функции инициализации и очистки
int gpio_init(void);
void gpio_cleanup(void);

// Проверка прерываний
sensor_event_t gpio_check_interrupts(void);

// Управление сиреной
void gpio_siren_on(void);
void gpio_siren_off(void);

// Управление камерой
void gpio_camera_on(void);
void gpio_camera_off(void);

#endif // GPIO_HANDLER_H