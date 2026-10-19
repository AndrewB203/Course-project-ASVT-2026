# Корневой Makefile для ОВС (Кросс-компиляция под Luckfox Pico / ARM)

# Кросс-компилятор (должен быть установлен в Ubuntu: sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf)
CC = arm-linux-gnueabihf-gcc
CXX = arm-linux-gnueabihf-g++

# Флаги компиляции
CFLAGS = -Wall -Wextra -O2 -I./src
CXXFLAGS = -Wall -Wextra -O2 -I./src -I/usr/include/rknn_api
LDFLAGS = -lrknnrt -lm

# Исполняемый файл
TARGET = ovs_system_daemon

# Исходные файлы
SRCS_C = \
    src/main.c \
    src/power_daemon/gpio_handler.c \
    src/power_daemon/power_manager.c \
    src/camera_driver/v4l2_capture.c \
    src/video_recorder/recorder.c \
    src/encryption/aes256.c \
    src/gsm_module/sms_sender.c

SRCS_CPP = \
    src/inference_engine/rknn_wrapper.cpp

OBJS = $(SRCS_C:.c=.o) $(SRCS_CPP:.cpp=.o)

# Цель по умолчанию
all: $(TARGET)

# Линковка
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✅ Сборка завершена успешно: $(TARGET)"

# Компиляция C файлов
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Компиляция C++ файлов
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Очистка
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "🧹 Очистка завершена"

# Установка на целевую систему (пример через scp, если настроен USB RNDIS)
install: $(TARGET)
	scp $(TARGET) root@192.168.7.2:/usr/bin/
	@echo "🚀 Файл отправлен на Luckfox Pico"

.PHONY: all clean install