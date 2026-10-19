/**
 * @file aes256.h
 * @brief Модуль шифрования архива (AES-256-CBC)
 * Соответствует ТЗ п. 1.a.v
 */
#ifndef AES256_H
#define AES256_H

#include <stdint.h>
#include <stddef.h>

#define AES_KEY_SIZE 32       // 256 бит = 32 байта
#define AES_BLOCK_SIZE 16     // 128 бит = 16 байт
#define AES_IV_SIZE 16

// Инициализация модуля шифрования (получение ключа из eFuse SoC)
int aes256_init(void);

// Шифрование буфера данных
// data: исходные данные (изменяются на месте или копируются)
// length: длина данных (должна быть кратна AES_BLOCK_SIZE)
int aes256_encrypt(uint8_t* data, size_t length);

// Дешифрование буфера данных (для режима Service)
int aes256_decrypt(uint8_t* data, size_t length);

// Освобождение ресурсов
void aes256_deinit(void);

#endif // AES256_H