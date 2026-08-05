CC ?= gcc

CPPFLAGS += -I../../../common/include
CFLAGS += -O2 -Wall -Wextra -Werror -std=c11

TARGET := pir_test

.PHONY: all clean

all: $(TARGET)

$(TARGET): pir_test.c ../../../common/include/presence_uapi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ pir_test.c

clean:
	$(RM) $(TARGET)
