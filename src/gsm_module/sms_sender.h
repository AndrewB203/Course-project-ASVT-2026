/**
 * @file sms_sender.h
 * @brief Интерфейс модуля GSM (SIM800L)
 * Соответствует ТЗ п. 1.a.viii.5 (отправка SMS)
 */
#ifndef SMS_SENDER_H
#define SMS_SENDER_H

#define GSM_UART_DEVICE "/dev/ttyS1" // UART порт для SIM800L на Luckfox
#define GSM_BAUDRATE 115200
#define MAX_PHONE_LEN 16
#define MAX_SMS_LEN 160

// Инициализация UART и проверка связи с модемом (AT)
int gsm_init(void);

// Отправка SMS
int gsm_send_sms(const char* phone_number, const char* message);

// Закрытие порта
void gsm_deinit(void);

#endif // SMS_SENDER_H