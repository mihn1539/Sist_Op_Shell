CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -Iinclude

SRC = src/main.c \
      src/parser.c \
      src/executor.c \
	src/signals.c \
	src/background.c \
	src/redirection.c \
      src/utils.c

OBJ = $(SRC:.c=.o)

TARGET = shell

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean