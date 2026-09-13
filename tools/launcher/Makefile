# Makefile modular para o Android Mobile Launcher GTK4
CC ?= gcc
CFLAGS ?= -O2 -Wall $(shell pkg-config --cflags gtk4)
LIBS ?= $(shell pkg-config --libs gtk4)

OBJS = main.o app_drawer.o window_manager.o
TARGET = sanders-launcher

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

main.o: main.c app_drawer.h window_manager.h
	$(CC) $(CFLAGS) -c main.c -o main.o

app_drawer.o: app_drawer.c app_drawer.h
	$(CC) $(CFLAGS) -c app_drawer.c -o app_drawer.o

window_manager.o: window_manager.c window_manager.h
	$(CC) $(CFLAGS) -c window_manager.c -o window_manager.o

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

.PHONY: all clean install
