# MAO host unit tests (no ESP-IDF needed): make -C tests/host
# Compiles the platform-independent cores straight from the components.
ROOT    := ../..
CC      ?= cc
BUILD   ?= build
CFLAGS  := -std=c99 -O1 -g -Wall -Wextra -Wshadow -Wpedantic -Werror -Wno-unused-parameter \
           -fsanitize=address,undefined -fno-omit-frame-pointer \
           -I. -I$(ROOT)/components/mao_perception/include -I$(ROOT)/components/mao_perception/core \
           -I$(ROOT)/components/mao_power/include -I$(ROOT)/components/mao_board/boards/main_a0
LDFLAGS := -fsanitize=address,undefined -lm

SRCS := test_main.c test_percept.c test_policy.c test_backlight.c \
        $(ROOT)/components/mao_perception/core/percept_engine.c \
        $(ROOT)/components/mao_power/mao_power_policy.c \
        $(ROOT)/components/mao_board/boards/main_a0/aw9364_dimming.c

.PHONY: test clean
test: $(BUILD)/mao_host_tests
	./$(BUILD)/mao_host_tests

$(BUILD)/mao_host_tests: $(SRCS) mini_test.h $(wildcard $(ROOT)/components/mao_perception/core/*.h) \
                         $(ROOT)/components/mao_perception/include/mao_percept.h \
                         $(ROOT)/components/mao_power/include/mao_power_policy.h \
                         $(ROOT)/components/mao_board/boards/main_a0/aw9364_dimming.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD)
