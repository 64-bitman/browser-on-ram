PREFIX := /usr/local

CC := gcc
CFLAGS := -Wextra -Wall -Wshadow -std=gnu11 -DSHAREDIR=\"$(PREFIX)/share\"
DEBUG_FLAGS := -ggdb -g3 -DDEBUG
REL_FLAGS := -O2

REL_PREFIX := release
DEBUG_PREFIX := debug

ifeq ($(RELEASE), 1)
BUILD_DIR := build/$(REL_PREFIX)
else
BUILD_DIR := build/$(DEBUG_PREFIX)
endif

BIN_PATH := $(BUILD_DIR)/bin
OBJ_PATH := $(BUILD_DIR)/bin
DEP_PATH := $(BUILD_DIR)/dep

SRC_PATH := src
INCLUDE_PATH := ./src/include

TARGET_NAME := bor
TARGET := $(BIN_PATH)/$(TARGET_NAME)

SRC := main.c util.c
OBJ := $(SRC:.c=.o)
OBJ := $(foreach file,$(OBJ),$(OBJ_PATH)/$(file))
DEPS := $(foreach file,$(notdir $(OBJ:.o=.d)),$(DEP_PATH)/$(file))

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

-include $(DEPS)

install: all
	install -dm 755 $(PREFIX)/share/bor/scripts
	install -dm 755 $(PREFIX)/lib/systemd/user
	install -Dm 755 build/$(REL_PREFIX)/bin/bor $(PREFIX)/bin/bor
	install -Dm 644 scripts/browsers/*.sh $(PREFIX)/share/bor/scripts
	install -Dm 644 systemd/* $(PREFIX)/lib/systemd/user

.PHONY: all clean prebuild install
