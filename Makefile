CC=cc
CFLAGS?=-O2 -Wall
LDFLAGS?=
LIBS=-lpng -lz -lcurl

SRC_DIR=src
OBJ_DIR=build
BIN=glitch

.DEFAULT_GOAL := $(BIN)

INCLUDES=-I$(SRC_DIR)
SRCS=$(SRC_DIR)/glitch.c $(SRC_DIR)/img.c
OBJS=$(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: clean lean minimal

$(BIN): $(OBJS) $(SRC_DIR)/colors.h $(SRC_DIR)/shape.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(OBJS) -lm $(LIBS) $(LDFLAGS)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/img.h $(SRC_DIR)/xxhash.h $(SRC_DIR)/colors.h $(SRC_DIR)/shape.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lean: CFLAGS+=-O2 -pipe -march=native -fno-plt -Wall
lean: LDFLAGS+=-s
lean: $(BIN)

minimal: CFLAGS+=-DMINIMAL_BUILD -Wall
minimal: LIBS=-lpng -lz
minimal: $(BIN)

clean:
	rm -f $(BIN) $(OBJS) $(SRC_DIR)/colors.h $(SRC_DIR)/shape.h src/*.o glitch.o img.o
