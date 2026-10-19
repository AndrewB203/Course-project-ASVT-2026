/**
 * @file recorder.h
 * @brief Модуль циклической записи видео с кольцевым буфером
 * Соответствует ТЗ п. 1.a.vi
 */
#ifndef RECORDER_H
#define RECORDER_H

#include <stdint.h>
#include <stddef.h>

// Размер кольцевого буфера в ОЗУ (например, 2 МБ = ~5 секунд видео H.264)
#define RING_BUFFER_SIZE (2 * 1024 * 1024) 

typedef struct {
    uint8_t* buffer;
    size_t size;
    size_t write_pos;
    size_t read_pos;
    int is_full;
} ring_buffer_t;

// Инициализация рекордера и выделение памяти под Ring Buffer
int recorder_init(void);

// Запись кадра (H.264 NAL unit) в буфер
int recorder_write_frame(const uint8_t* frame_data, size_t frame_size);

// Сброс кольцевого буфера на MicroSD (при переходе в режим ALARM)
int recorder_flush_to_sd(const char* output_filename);

// Прямая запись на SD (когда буфер уже сброшен)
int recorder_write_direct_to_sd(const uint8_t* frame_data, size_t frame_size);

// Освобождение ресурсов
void recorder_deinit(void);

#endif // RECORDER_H