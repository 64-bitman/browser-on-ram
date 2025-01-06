ifndef MY_FLAG
	PREFIX := /usr/local
endif

CC := gcc
CFLAGS := -Wextra -Wall -Wshadow -std=gnu11 -DSHAREDIR=\"$(PREFIX)/share\"
DEBUG_FLAGS := -ggdb -g3 -DDEBUG
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

SRC := main.c util.c
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

install:
	install -dm 755 $(PREFIX)/share/bor/scripts
	install -Dm 755 $(BUILD_DIR)/bin/bor $(PREFIX)/bin/bor
	install -Dm 644 scripts/browsers/*.sh $(PREFIX)/share/bor/scripts

install-systemd:
	install -dm 755 $(PREFIX)/lib/systemd/user
	install -Dm 644 systemd/* $(PREFIX)/lib/systemd/user

install_setuid:
	chown root:root $(PREFIX)/bin/bor
	chmod u+s $(PREFIX)/bin/bor

test: all
	test/start-test $(BUILD_DIR)/bin/bor

sync: all
	$(BUILD_DIR)/bin/bor -v -c test/config/bor -d test/share/bor -t test/tmpfs --sync

unsync: all
	$(BUILD_DIR)/bin/bor -v -c test/config/bor -d test/share/bor -t test/tmpfs --unsync

resync: all
	$(BUILD_DIR)/bin/bor -v -c test/config/bor -d test/share/bor -t test/tmpfs --resync

status: all
	$(BUILD_DIR)/bin/bor -v -c test/config/bor -d test/share/bor -t test/tmpfs --status

setuid:
	sudo chown root:root $(TARGET)
	sudo chmod u+s $(TARGET)


-include $(DEPS)

.PHONY: all clean prebuild install rebuild test sync unsync resync setuid
