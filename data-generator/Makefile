ifeq ($(shell uname -s),Darwin)
    CC ?= gcc-15
else
    CC ?= gcc
endif
CFLAGS = -O2 -Wall -Wextra -fopenmp
SRCS   = main.c cjson/cJSON.c
TARGET = generate

$(TARGET): $(SRCS) cjson/cJSON.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) -lm

clean:
	rm -f $(TARGET)

.PHONY: clean
