/**
 * @file gpio_handler.c
 * @brief Обработка прерываний GPIO для PIR-датчика и G-сенсора
 * 
 * Использует sysfs GPIO интерфейс Linux для чтения прерываний.
 * Настраивает GPIO на rising edge для детекции событий.
 */
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <syslog.h>
#include <errno.h>

#include "gpio_handler.h"

// Номера GPIO (зависят от разводки платы Luckfox)
#define GPIO_PIR_SENSOR     48   // PIR-датчик движения
#define GPIO_G_SENSOR_INT   49   // Прерывание G-сенсора (MPU6050)
#define GPIO_SIREN_CTRL     50   // Управление сиреной
#define GPIO_CAMERA_EN      51   // Питание камеры

// Файловые дескрипторы для GPIO
static int fd_pir = -1;
static int fd_g_sensor = -1;
static int fd_siren = -1;
static int fd_camera = -1;

// Экспорт GPIO в sysfs
static int gpio_export(int gpio_num) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", gpio_num);
    
    // Проверяем, экспортирован ли уже GPIO
    if (access(path, F_OK) == 0) {
        return 0; // Уже экспортирован
    }
    
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) {
        syslog(LOG_ERR, "Не удалось открыть /sys/class/gpio/export: %s", strerror(errno));
        return -1;
    }
    
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d", gpio_num);
    if (write(fd, buf, len) != len) {
        syslog(LOG_ERR, "Ошибка записи в /sys/class/gpio/export: %s", strerror(errno));
        close(fd);
        return -1;
    }
    
    close(fd);
    struct timespec ts = {0, 100000000}; // 100 мс
    nanosleep(&ts, NULL);
   
    return 0;
}

// Настройка направления GPIO (in/out)
static int gpio_set_direction(int gpio_num, const char *direction) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio_num);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        syslog(LOG_ERR, "Не удалось открыть %s: %s", path, strerror(errno));
        return -1;
    }
    
    if (write(fd, direction, strlen(direction)) != (ssize_t)strlen(direction)) {
        syslog(LOG_ERR, "Ошибка записи направления GPIO %d: %s", gpio_num, strerror(errno));
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

// Настройка edge detection для прерываний
static int gpio_set_edge(int gpio_num, const char *edge) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/edge", gpio_num);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        syslog(LOG_ERR, "Не удалось открыть %s: %s", path, strerror(errno));
        return -1;
    }
    
    if (write(fd, edge, strlen(edge)) != (ssize_t)strlen(edge)) {
        syslog(LOG_ERR, "Ошибка записи edge GPIO %d: %s", gpio_num, strerror(errno));
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

// Открытие GPIO для чтения значений
static int gpio_open_value(int gpio_num) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio_num);
    
    int fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        syslog(LOG_ERR, "Не удалось открыть %s: %s", path, strerror(errno));
        return -1;
    }
    
    return fd;
}

// Инициализация GPIO
int gpio_init(void) {
    syslog(LOG_INFO, "Инициализация GPIO...");
    
    // Экспорт GPIO
    if (gpio_export(GPIO_PIR_SENSOR) != 0) return -1;
    if (gpio_export(GPIO_G_SENSOR_INT) != 0) return -1;
    if (gpio_export(GPIO_SIREN_CTRL) != 0) return -1;
    if (gpio_export(GPIO_CAMERA_EN) != 0) return -1;
    
    // Настройка направлений
    if (gpio_set_direction(GPIO_PIR_SENSOR, "in") != 0) return -1;
    if (gpio_set_direction(GPIO_G_SENSOR_INT, "in") != 0) return -1;
    if (gpio_set_direction(GPIO_SIREN_CTRL, "out") != 0) return -1;
    if (gpio_set_direction(GPIO_CAMERA_EN, "out") != 0) return -1;
    
    // Настройка edge detection для прерываний
    if (gpio_set_edge(GPIO_PIR_SENSOR, "rising") != 0) return -1;
    if (gpio_set_edge(GPIO_G_SENSOR_INT, "rising") != 0) return -1;
    
    // Открытие файловых дескрипторов для чтения
    fd_pir = gpio_open_value(GPIO_PIR_SENSOR);
    fd_g_sensor = gpio_open_value(GPIO_G_SENSOR_INT);
    fd_siren = gpio_open_value(GPIO_SIREN_CTRL);
    fd_camera = gpio_open_value(GPIO_CAMERA_EN);
    
    if (fd_pir < 0 || fd_g_sensor < 0 || fd_siren < 0 || fd_camera < 0) {
        syslog(LOG_ERR, "Ошибка открытия GPIO value файлов");
        return -1;
    }
    
    syslog(LOG_INFO, "GPIO инициализированы успешно");
    return 0;
}

// Проверка прерываний от датчиков
sensor_event_t gpio_check_interrupts(void) {
    sensor_event_t event = {EVENT_NONE, 0};
    
    struct pollfd fds[2];
    fds[0].fd = fd_pir;
    fds[0].events = POLLPRI;
    fds[1].fd = fd_g_sensor;
    fds[1].events = POLLPRI;
    
    // Опрос с таймаутом 100 мс
    int ret = poll(fds, 2, 100);
    
    if (ret < 0) {
        syslog(LOG_ERR, "Ошибка poll: %s", strerror(errno));
        return event;
    }
    
    if (ret == 0) {
        return event; // Таймаут
    }
    
    // Проверка PIR-датчика
    if (fds[0].revents & POLLPRI) {
        char buf[4] = {0}; // Явная инициализация нулями
        lseek(fd_pir, 0, SEEK_SET);
        if (read(fd_pir, buf, sizeof(buf) - 1) > 0) { // -1 для безопасности
            if (buf[0] == '1') {
                syslog(LOG_INFO, "PIR-датчик сработал");
                event.type = EVENT_PIR_TRIGGER;
                event.timestamp = time(NULL);
                return event;
            }
        }
    }
    
    // Проверка G-сенсора
    if (fds[1].revents & POLLPRI) {
        char buf[4]={0};
        lseek(fd_g_sensor, 0, SEEK_SET);
        if (read(fd_g_sensor, buf, sizeof(buf)-1) > 0) {
            if (buf[0] == '1') {
                syslog(LOG_INFO, "G-сенсор сработал (удар/вибрация)");
                event.type = EVENT_G_SENSOR_TRIGGER;
                event.timestamp = time(NULL);
                return event;
            }
        }
    }
    
    return event;
}

// Включение/выключение сирены
void gpio_siren_on(void) {
    if (fd_siren >= 0) {
        (void)write(fd_siren, "1", 1);
        syslog(LOG_INFO, "Сирена включена");
    }
}

void gpio_siren_off(void) {
    if (fd_siren >= 0) {
        (void)write(fd_siren, "0", 1);
        syslog(LOG_INFO, "Сирена выключена");
    }
}

// Включение/выключение камеры
void gpio_camera_on(void) {
    if (fd_camera >= 0) {
        (void)write(fd_camera, "1", 1);
        syslog(LOG_INFO, "Камера включена");
    }
}

void gpio_camera_off(void) {
    if (fd_camera >= 0) {
        (void)write(fd_camera, "0", 1);
        syslog(LOG_INFO, "Камера выключена");
    }
}

// Освобождение ресурсов GPIO
void gpio_cleanup(void) {
    syslog(LOG_INFO, "Освобождение GPIO...");
    
    if (fd_pir >= 0) close(fd_pir);
    if (fd_g_sensor >= 0) close(fd_g_sensor);
    if (fd_siren >= 0) close(fd_siren);
    if (fd_camera >= 0) close(fd_camera);
    
    // Unexport GPIO
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd >= 0) {
        char buf[16];
        int len;
        
        len = snprintf(buf, sizeof(buf), "%d", GPIO_PIR_SENSOR);
        (void)write(fd, buf, len);
        
        len = snprintf(buf, sizeof(buf), "%d", GPIO_G_SENSOR_INT);
        (void)write(fd, buf, len);
        
        len = snprintf(buf, sizeof(buf), "%d", GPIO_SIREN_CTRL);
        (void)write(fd, buf, len);
        
        len = snprintf(buf, sizeof(buf), "%d", GPIO_CAMERA_EN);
        (void)write(fd, buf, len);
        
        close(fd);
    }
}
