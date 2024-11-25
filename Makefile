PREFIX := /usr/local

CC := gcc
CFLAGS := -Wextra -Wall -Wshadow -std=gnu11 -O2 -DSHAREDIR=\"$(PREFIX)/share\"
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

install: all
	install -dm 755 $(PREFIX)/share/bor/scripts
	install -dm 755 $(PREFIX)/lib/systemd/user
	install -Dm 755 bin/bor $(PREFIX)/bin/bor
	install -Dm 644 scripts/browsers/*.sh $(PREFIX)/share/bor/scripts
	install -Dm 644 systemd/* $(PREFIX)/lib/systemd/user

sync: debug
	bin/bor -vi --sync -c ./test/config/bor \
		-d ./test/share/bor -t ./test/tmpfs/bor/

unsync: debug
	bin/bor -vi --unsync --config=./test/config/bor \
		-d ./test/share/bor -t ./test/tmpfs/bor/

resync: debug
	bin/bor -vi --resync --config=./test/config/bor \
		-d ./test/share/bor -t ./test/tmpfs/bor/

status: debug
	bin/bor -vi --status --config=./test/config/bor \
		-d ./test/share/bor -t ./test/tmpfs/bor/


$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

-include $(DEPS)

$(OBJ_PATH)/%.o: $(SRC_PATH)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_PATH) -MD -MP -MF $(DEP_PATH)/$(notdir $(basename $@).d) -o $@ -c $<

.PHONY: all clean prebuild debug install sync unsync resync status
