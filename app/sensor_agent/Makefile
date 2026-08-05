# CC는 기본값을 gcc로 두되, 밖에서 오버라이드 가능하게 만듦.
# 예: make CC=aarch64-linux-gnu-gcc  ← 이렇게 부르면 크로스컴파일러로 교체됨
CC ?= gcc

CFLAGS = -Wall -Wextra -I../../common/include

TARGET = sensor_agent
SRCS = sensor_agent.c mock/mock_backend.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
