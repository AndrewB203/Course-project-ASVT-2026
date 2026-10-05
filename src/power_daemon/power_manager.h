/**
 * @file power_manager.h
 * @brief Интерфейс менеджера питания
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

// Состояния системы
typedef enum {
    STATE_DEEP_SLEEP,
    STATE_ACTIVE,
    STATE_ALARM,
    STATE_SERVICE
} system_state_t;

// Функции управления питанием
int power_manager_init(void);
void power_manager_cleanup(void);

int power_manager_sleep(void);
int power_manager_wakeup(void);

void power_manager_trigger_alarm(void);
int power_manager_alarm_completed(void);

void power_manager_update_watchdog(void);

#endif // POWER_MANAGER_H