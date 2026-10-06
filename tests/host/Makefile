# MAO host unit tests (no ESP-IDF needed): make -C tests/host
# Compiles the platform-independent cores straight from the components.
# SAN= (empty) builds without the sanitizers (e.g. zig cc on Windows).
ROOT    := ../..
CC      ?= cc
BUILD   ?= build
SAN     ?= -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  := -std=c99 -O1 -g -Wall -Wextra -Wshadow -Wpedantic -Werror -Wno-unused-parameter $(SAN) \
           -I. -I$(ROOT)/components/mao_perception/include -I$(ROOT)/components/mao_perception/core \
           -I$(ROOT)/components/mao_battery/include
LDFLAGS := $(SAN) -lm

SRCS := test_main.c test_percept.c test_policy.c \
        $(ROOT)/components/mao_perception/core/percept_engine.c \
        $(ROOT)/components/mao_battery/mao_battery_policy.c

.PHONY: test clean
test: $(BUILD)/mao_host_tests
	./$(BUILD)/mao_host_tests

$(BUILD)/mao_host_tests: $(SRCS) mini_test.h $(wildcard $(ROOT)/components/mao_perception/core/*.h) \
                         $(ROOT)/components/mao_perception/include/mao_percept.h \
                         $(ROOT)/components/mao_battery/include/mao_battery_policy.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD)
