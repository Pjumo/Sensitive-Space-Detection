CC ?= gcc
AR ?= ar

CPPFLAGS += -I../include
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -Wpedantic -std=c11

TARGET = libsensor_backend.a
OBJS = sensor_backend.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs $@ $^

sensor_backend.o: sensor_backend.c sensor_backend.h ../include/presence_uapi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ sensor_backend.c

clean:
	$(RM) $(OBJS) $(TARGET)

