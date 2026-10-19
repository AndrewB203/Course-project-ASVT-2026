/**
 * @file recorder.c
 * @brief Реализация циклической записи и кольцевого буфера
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include "recorder.h"
#include "../encryption/aes256.h"

static ring_buffer_t ring_buf;
static int sd_file_fd = -1;
static int is_flushed = 0; // Флаг: был ли буфер сброшен на SD

int recorder_init(void) {
    syslog(LOG_INFO, "Инициализация видеорекордера...");
    
    // Выделяем память под кольцевой буфер в ОЗУ
    ring_buf.buffer = (uint8_t*)malloc(RING_BUFFER_SIZE);
    if (!ring_buf.buffer) {
        syslog(LOG_ERR, "Не удалось выделить память под Ring Buffer (%d байт)", RING_BUFFER_SIZE);
        return -1;
    }
    
    ring_buf.size = RING_BUFFER_SIZE;
    ring_buf.write_pos = 0;
    ring_buf.read_pos = 0;
    ring_buf.is_full = 0;
    is_flushed = 0;
    
    // Инициализируем шифрование
    aes256_init();

    syslog(LOG_INFO, "Ring Buffer создан (%d МБ). Ожидание кадров...", RING_BUFFER_SIZE / (1024*1024));
    return 0;
}

// Запись кадра в кольцевой буфер (режим ACTIVE / ожидание тревоги)
int recorder_write_frame(const uint8_t* frame_data, size_t frame_size) {
    if (!ring_buf.buffer) return -1;
    if (is_flushed) return -1; // Если уже сбросили, пишем напрямую

    // Если кадр больше буфера, пропускаем (в реальности нужно разбивать на части)
    if (frame_size > ring_buf.size) {
        syslog(LOG_WARNING, "Кадр слишком большой для буфера");
        return -1;
    }

    // Проверка на переполнение и перезапись старых данных
    if (ring_buf.write_pos + frame_size > ring_buf.size) {
        // Копируем частями, если конец буфера
        size_t part1 = ring_buf.size - ring_buf.write_pos;
        memcpy(ring_buf.buffer + ring_buf.write_pos, frame_data, part1);
        memcpy(ring_buf.buffer, frame_data + part1, frame_size - part1);
        ring_buf.write_pos = frame_size - part1;
    } else {
        memcpy(ring_buf.buffer + ring_buf.write_pos, frame_data, frame_size);
        ring_buf.write_pos += frame_size;
    }

    // Обновляем read_pos, чтобы не потерять начало, если буфер переполнен
    if (ring_buf.write_pos >= ring_buf.size) {
        ring_buf.write_pos = 0;
        ring_buf.is_full = 1;
        ring_buf.read_pos = 0; // Перезаписываем с начала
    }

    return 0;
}

// Сброс буфера на MicroSD с шифрованием (переход в режим ALARM)
int recorder_flush_to_sd(const char* output_filename) {
    syslog(LOG_WARNING, "ТРЕВОГА! Сброс Ring Buffer на MicroSD: %s", output_filename);
    
    sd_file_fd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (sd_file_fd < 0) {
        syslog(LOG_ERR, "Ошибка открытия файла на SD карте");
        return -1;
    }

    // Определяем объем данных для записи
    size_t data_to_write;
    size_t start_pos;

    if (ring_buf.is_full) {
        // Буфер полон, пишем всё от read_pos до конца, потом от 0 до write_pos
        // Для упрощения в курсовой запишем весь буфер целиком
        data_to_write = ring_buf.size;
        start_pos = ring_buf.read_pos; 
    } else {
        data_to_write = ring_buf.write_pos;
        start_pos = 0;
    }

    // Читаем данные из буфера, шифруем и пишем на SD
    uint8_t temp_block[4096]; // Блок для шифрования (кратен 16 байтам AES)
    size_t remaining = data_to_write;
    size_t current_pos = start_pos;

    while (remaining > 0) {
        size_t block_size = (remaining < sizeof(temp_block)) ? remaining : sizeof(temp_block);
        
        // Копируем блок из кольцевого буфера
        if (current_pos + block_size > ring_buf.size) {
            size_t part1 = ring_buf.size - current_pos;
            memcpy(temp_block, ring_buf.buffer + current_pos, part1);
            memcpy(temp_block + part1, ring_buf.buffer, block_size - part1);
        } else {
            memcpy(temp_block, ring_buf.buffer + current_pos, block_size);
        }

        // ДОШИФРОВЫВАЕМ БЛОК ДО КРАТНОСТИ 16 (Padding PKCS7)
        // ... (в полноценном коде добавляется padding) ...

        // Шифруем блок перед записью (ТЗ п. 1.a.v)
        aes256_encrypt(temp_block, block_size);

        // Пишем зашифрованный блок на MicroSD
        write(sd_file_fd, temp_block, block_size);

        current_pos = (current_pos + block_size) % ring_buf.size;
        remaining -= block_size;
    }

    is_flushed = 1;
    syslog(LOG_INFO, "Ring Buffer успешно сброшен и зашифрован на SD");
    return 0;
}

// Прямая запись на SD (режим ALARM, после сброса буфера)
int recorder_write_direct_to_sd(const uint8_t* frame_data, size_t frame_size) {
    if (sd_file_fd < 0) return -1;

    // Шифруем кадр перед записью
    // В реальности нужно собирать кадры в блоки по 16 байт
    uint8_t encrypted_frame[4096];
    if (frame_size > sizeof(encrypted_frame)) return -1;
    
    memcpy(encrypted_frame, frame_data, frame_size);
    // Дополнение до кратности 16 байтам (упрощенно)
    size_t padded_size = (frame_size + 15) & ~15; 
    memset(encrypted_frame + frame_size, 0, padded_size - frame_size);

    aes256_encrypt(encrypted_frame, padded_size);
    
    write(sd_file_fd, encrypted_frame, padded_size);
    return 0;
}

void recorder_deinit(void) {
    if (sd_file_fd >= 0) {
        close(sd_file_fd);
        syslog(LOG_INFO, "Файл на SD карте закрыт");
    }
    if (ring_buf.buffer) {
        free(ring_buf.buffer);
        ring_buf.buffer = NULL;
    }
    aes256_deinit();
    syslog(LOG_INFO, "Видеорекордер остановлен");
}