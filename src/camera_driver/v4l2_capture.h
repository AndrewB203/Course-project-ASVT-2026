/**
 * @file v4l2_capture.h
 * @brief Интерфейс драйвера камеры (V4L2)
 * Соответствует ТЗ п. 4.3 (драйверы V4L2 и Rockchip ISP)
 */
#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include <stdint.h>
#include <stddef.h>

#define MAX_BUFFERS 4 // Количество буферов в кольце V4L2

typedef struct {
    int fd;                 // Файловый дескриптор устройства
    uint8_t* buffers[MAX_BUFFERS]; // Указатели на mmap буферы
    size_t buffer_sizes[MAX_BUFFERS];
    int width;
    int height;
    int is_streaming;
} camera_context_t;

// Инициализация камеры (открытие устройства, настройка формата, mmap)
int camera_init(camera_context_t* ctx, const char* device_path, int width, int height);

// Запуск потока захвата
int camera_start(camera_context_t* ctx);

// Получение кадра (блокирующее ожидание)
// Возвращает указатель на данные и размер кадра
int camera_get_frame(camera_context_t* ctx, uint8_t** frame_data, size_t* frame_size);

// Возврат буфера обратно в очередь драйвера
int camera_release_frame(camera_context_t* ctx);

// Остановка и освобождение ресурсов
void camera_deinit(camera_context_t* ctx);

#endif // V4L2_CAPTURE_H