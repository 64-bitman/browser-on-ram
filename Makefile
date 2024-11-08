CC := gcc
CFLAGS := -Wextra -Wall -Wshadow -std=gnu11 -ggdb -g3 -DDEBUG

BIN_PATH := ./bin
SRC_PATH := ./src
OBJ_PATH := ./bin
DEP_PATH := ./dep
INCLUDE_PATH := ./src/include

TARGET_NAME := bor
TARGET := $(BIN_PATH)/$(TARGET_NAME)

SRC := main.c util.c
OBJ := $(SRC:.c=.o)
OBJ := $(foreach file,$(OBJ),$(OBJ_PATH)/$(file))
DEPS := $(foreach file,$(notdir $(OBJ:.o=.d)),$(DEP_PATH)/$(file))

CLEAN_LIST := $(OBJ) $(TARGET) $(DEPS)

all: prebuild $(TARGET) Makefile

prebuild:
	@mkdir -p $(BIN_PATH) $(SRC_PATH) $(DEP_PATH) $(OBJ_PATH) $(INCLUDE_PATH)

clean:
	rm -f $(CLEAN_LIST)

run: all
	$(TARGET) --sync --verbose

test: all
	test/bor-test

valgrind: all
	valgrind -s --leak-check=full --track-origins=yes --show-leak-kinds=all $(args) $(TARGET) '$(targs)'

debug: all
	gdb --args $(TARGET) $(args)

rebuild: clean all

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

-include $(DEPS)

$(OBJ_PATH)/%.o: $(SRC_PATH)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_PATH) -MD -MP -MF $(DEP_PATH)/$(notdir $(basename $@).d) -o $@ -c $<

.PHONY: all clean run rebuild prebuild
