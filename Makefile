CC=gcc
CFLAGS=-g -Wall -Werror
LDFLAGS=-lraylib -lGL -lm -pthread -ldl -lrt -lX11
SRC_DIR=src
BIN_DIR=bin

all: dirs $(BIN_DIR)/main

dirs:
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/main: $(SRC_DIR)/wfc.c $(SRC_DIR)/main.c
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -rf $(BIN_DIR)
