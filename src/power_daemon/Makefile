# Makefile для демона управления питанием ОВС

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS =

TARGET = ovs_power_daemon
SRCS = main.c gpio_handler.c power_manager.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

.PHONY: all clean install