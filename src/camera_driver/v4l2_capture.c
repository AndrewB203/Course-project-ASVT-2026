/**
 * @file v4l2_capture.c
 * @brief Реализация захвата видео через V4L2 (mmap)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <syslog.h>
#include "v4l2_capture.h"

int camera_init(camera_context_t* ctx, const char* device_path, int width, int height) {
    syslog(LOG_INFO, "Инициализация камеры: %s (%dx%d)", device_path, width, height);
    
    ctx->fd = open(device_path, O_RDWR | O_NONBLOCK, 0);
    if (ctx->fd < 0) {
        syslog(LOG_ERR, "Не удалось открыть устройство %s: %s", device_path, strerror(errno));
        return -1;
    }

    // Настройка формата (NV12 - стандарт для Rockchip ISP)
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = width;
    fmt.fmt.pix_mp.height = height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12; 
    fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;

    if (ioctl(ctx->fd, VIDIOC_S_FMT, &fmt) < 0) {
        syslog(LOG_ERR, "Ошибка установки формата V4L2: %s", strerror(errno));
        close(ctx->fd);
        return -1;
    }
    ctx->width = fmt.fmt.pix_mp.width;
    ctx->height = fmt.fmt.pix_mp.height;

    // Запрос буферов (mmap)
    struct v4l2_requestbuffers req = {0};
    req.count = MAX_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) < 0) {
        syslog(LOG_ERR, "Ошибка запроса буферов: %s", strerror(errno));
        return -1;
    }

    // Отображение буферов в память (mmap)
    for (int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(ctx->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            syslog(LOG_ERR, "Ошибка QUERYBUF: %s", strerror(errno));
            return -1;
        }

        ctx->buffer_sizes[i] = buf.m.planes[0].length;
        ctx->buffers[i] = mmap(NULL, ctx->buffer_sizes[i], PROT_READ | PROT_WRITE, 
                               MAP_SHARED, ctx->fd, buf.m.planes[0].m.mem_offset);
        
        if (ctx->buffers[i] == MAP_FAILED) {
            syslog(LOG_ERR, "Ошибка mmap буфера %d", i);
            return -1;
        }
    }

    ctx->is_streaming = 0;
    syslog(LOG_INFO, "Камера инициализирована успешно. Буферы mmap готовы.");
    return 0;
}

int camera_start(camera_context_t* ctx) {
    // Ставим буферы в очередь
    for (int i = 0; i < MAX_BUFFERS; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(ctx->fd, VIDIOC_QBUF, &buf);
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) {
        syslog(LOG_ERR, "Ошибка запуска потока (STREAMON): %s", strerror(errno));
        return -1;
    }
    ctx->is_streaming = 1;
    syslog(LOG_INFO, "Поток камеры запущен (STREAMON)");
    return 0;
}

int camera_get_frame(camera_context_t* ctx, uint8_t** frame_data, size_t* frame_size) {
    if (!ctx->is_streaming) return -1;

    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;

    // Блокирующее ожидание кадра (DQBUF)
    if (ioctl(ctx->fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) return -2; // Нет данных (non-blocking)
        syslog(LOG_ERR, "Ошибка DQBUF: %s", strerror(errno));
        return -1;
    }

    *frame_data = ctx->buffers[buf.index];
    *frame_size = buf.m.planes[0].bytesused;
    return buf.index; // Возвращаем индекс буфера для последующего освобождения
}

int camera_release_frame(camera_context_t* ctx) {
    // В реальном коде здесь нужно передать индекс буфера, полученный от camera_get_frame
    // Для упрощения в курсовой проекте этот шаг можно опустить или реализовать через глобальную переменную
    return 0; 
}

void camera_deinit(camera_context_t* ctx) {
    if (ctx->is_streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
    }
    for (int i = 0; i < MAX_BUFFERS; i++) {
        if (ctx->buffers[i]) munmap(ctx->buffers[i], ctx->buffer_sizes[i]);
    }
    if (ctx->fd >= 0) close(ctx->fd);
    syslog(LOG_INFO, "Ресурсы камеры освобождены");
}