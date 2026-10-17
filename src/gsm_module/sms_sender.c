/**
 * @file sms_sender.c
 * @brief Реализация отправки SMS через AT-команды (SIM800L)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <syslog.h>
#include "sms_sender.h"

static int uart_fd = -1;

// Вспомогательная функция для отправки AT-команды и ожидания ответа
static int send_at_command(const char* cmd, const char* expected_response, int timeout_ms) {
    if (uart_fd < 0) return -1;
    
    write(uart_fd, cmd, strlen(cmd));
    write(uart_fd, "\r\n", 2);
    
    // В реальном проекте здесь нужен цикл read() с таймаутом и поиском подстроки expected_response
    // Для курсовой работы эмулируем успешный ответ
    syslog(LOG_DEBUG, "AT CMD: %s | Ожидание: %s", cmd, expected_response);
    usleep(timeout_ms * 1000); 
    return 0; 
}

int gsm_init(void) {
    syslog(LOG_INFO, "Инициализация GSM модуля (UART: %s)", GSM_UART_DEVICE);
    
    uart_fd = open(GSM_UART_DEVICE, O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_fd < 0) {
        syslog(LOG_ERR, "Не удалось открыть UART порт %s", GSM_UART_DEVICE);
        return -1;
    }

    // Настройка termios (115200 8N1)
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    tcgetattr(uart_fd, &tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cflag &= ~PARENB; // No parity
    tty.c_cflag &= ~CSTOPB; // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;     // 8 bits
    tty.c_cflag &= ~CRTSCTS; // No flow control
    tty.c_cflag |= CREAD | CLOCAL; 
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Raw mode
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); 
    tty.c_oflag &= ~OPOST; 
    tcsetattr(uart_fd, TCSANOW, &tty);

    // Проверка связи
    if (send_at_command("AT", "OK", 500) == 0) {
        syslog(LOG_INFO, "GSM модуль отвечает на AT");
        send_at_command("AT+CMGF=1", "OK", 500); // Текстовый режим SMS
        return 0;
    }
    
    return -1;
}

int gsm_send_sms(const char* phone_number, const char* message) {
    if (uart_fd < 0) {
        syslog(LOG_ERR, "GSM не инициализирован");
        return -1;
    }

    syslog(LOG_WARNING, "Отправка SMS на номер %s...", phone_number);

    // 1. Устанавливаем номер получателя
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", phone_number);
    
    // 2. Отправляем текст сообщения
    write(uart_fd, cmd, strlen(cmd));
    write(uart_fd, "\r\n", 2);
    usleep(500000); // Ждем приглашения "> "
    
    write(uart_fd, message, strlen(message));
    usleep(100000);
    
    // 3. Отправляем Ctrl+Z (0x1A) для завершения ввода
    uint8_t ctrl_z = 0x1A;
    write(uart_fd, &ctrl_z, 1);
    
    syslog(LOG_INFO, "SMS успешно отправлено (эмуляция)");
    return 0;
}

void gsm_deinit(void) {
    if (uart_fd >= 0) {
        close(uart_fd);
        uart_fd = -1;
        syslog(LOG_INFO, "GSM UART порт закрыт");
    }
}