ifndef PREFIX
	PREFIX := /usr/local
endif

CC := gcc
MACROS := -DINI_STOP_ON_FIRST_ERROR=1 -DINI_ALLOW_NO_VALUE=1 -DVERSION=\"$(shell git describe)\"
CLIBS := $(shell pkg-config --cflags --libs libcap)
CFLAGS := -Wextra -Wall -Wshadow -Wcast-align=strict -Wno-format-truncation -std=gnu11 $(CLIBS) $(MACROS)
DEBUG_FLAGS := -ggdb -g3 -DDEBUG -fsanitize=address,undefined -fanalyzer
REL_FLAGS := -O2

REL_PREFIX := release
DEBUG_PREFIX := debug

ifeq ($(RELEASE), 1)
	BUILD_DIR := build/$(REL_PREFIX)
	CFLAGS += $(REL_FLAGS)
else
	BUILD_DIR := build/$(DEBUG_PREFIX)
	CFLAGS += $(DEBUG_FLAGS)
endif


BIN_PATH := $(BUILD_DIR)/bin
OBJ_PATH := $(BUILD_DIR)/bin
DEP_PATH := $(BUILD_DIR)/dep

SRC_PATH := src
INCLUDE_PATH := ./src/include

TARGET_NAME := bor
TARGET := $(BIN_PATH)/$(TARGET_NAME)

SRC := main.c util.c config.c types.c sync.c overlay.c ini.c teeny-sha1.c
OBJ := $(addprefix $(OBJ_PATH)/, $(SRC:.c=.o))
DEPS := $(addprefix $(DEP_PATH)/, $(notdir $(OBJ:.o=.d)))

all: prebuild $(TARGET)

prebuild:
	@mkdir -p $(BIN_PATH) $(SRC_PATH) $(DEP_PATH) $(OBJ_PATH) $(INCLUDE_PATH) $(BUILD_PREFIX)

rebuild: clean all

clean:
	rm -fr build/release/*/*
	rm -fr build/debug/*/*

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_PATH)/%.o: $(SRC_PATH)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_PATH) -MD -MP -MF $(DEP_PATH)/$(notdir $(basename $@).d) -o $@ -c $<

test: all
	test/start_test

setcap:
	sudo setcap 'cap_dac_override,cap_sys_admin=p' $(TARGET)

install:
	install -dm 755 $(PREFIX)/share/bor/scripts
	install -Dm 755 $(BUILD_DIR)/bin/bor $(PREFIX)/bin/bor
	install -Dm 644 scripts/*.sh $(PREFIX)/share/bor/scripts

install-systemd:
	install -dm 755 $(PREFIX)/lib/systemd/user
	install -Dm 644 systemd/* $(PREFIX)/lib/systemd/user

install-cap:
	setcap 'cap_dac_override,cap_sys_admin=p' $(PREFIX)/bin/bor

-include $(DEPS)

.PHONY: all clean prebuild rebuild run setcap install install-systemd install-cap
