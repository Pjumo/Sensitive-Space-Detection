CC ?= gcc
CFLAGS = -Wall -Wextra -I../../common/include

AGENT_TARGET = sensor_agent
AGENT_SRCS = sensor_agent.c mock/mock_backend.c

CLIENT_TARGET = sensor_agent_client
CLIENT_SRCS = sensor_agent_client.c mock/mock_backend.c

all: $(AGENT_TARGET) $(CLIENT_TARGET)

$(AGENT_TARGET): $(AGENT_SRCS)
	$(CC) $(CFLAGS) -o $(AGENT_TARGET) $(AGENT_SRCS)

$(CLIENT_TARGET): $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -o $(CLIENT_TARGET) $(CLIENT_SRCS)

run-agent: $(AGENT_TARGET)
	./$(AGENT_TARGET)

run-client: $(CLIENT_TARGET)
	./$(CLIENT_TARGET) $(ARGS)

clean:
	rm -f $(AGENT_TARGET) $(CLIENT_TARGET)

.PHONY: all run-agent run-client clean
