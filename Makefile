CC := gcc
CFLAGS := -Wextra -Wall -Wshadow -std=gnu11 -O2
DEBUGFLAGS := -ggdb -g3 -DDEBUG
BIN_PATH := bin
SRC_PATH := src
OBJ_PATH := bin
DEP_PATH := dep
INCLUDE_PATH := ./src/include

TARGET_NAME := bor
TARGET := $(BIN_PATH)/$(TARGET_NAME)

SRC := main.c util.c
OBJ := $(SRC:.c=.o)
OBJ := $(foreach file,$(OBJ),$(OBJ_PATH)/$(file))
DEPS := $(foreach file,$(notdir $(OBJ:.o=.d)),$(DEP_PATH)/$(file))

CLEAN_LIST := $(OBJ) $(TARGET) $(DEPS)

# TODO: add tests

all: prebuild $(TARGET)

debug: CFLAGS := $(filter-out -O2, $(CFLAGS))
debug: CFLAGS += $(DEBUGFLAGS)
debug: all

prebuild:
	@mkdir -p $(BIN_PATH) $(SRC_PATH) $(DEP_PATH) $(OBJ_PATH) $(INCLUDE_PATH)

clean:
	rm -f $(CLEAN_LIST)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

-include $(DEPS)

$(OBJ_PATH)/%.o: $(SRC_PATH)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_PATH) -MD -MP -MF $(DEP_PATH)/$(notdir $(basename $@).d) -o $@ -c $<

.PHONY: all clean rebuild prebuild debug
