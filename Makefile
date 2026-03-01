CC     = cc
CFLAGS = -std=c11 -Wall -Wextra -g -Iinclude

SRCS = src/arena.c \
       src/error.c \
       src/ast.c \
       src/lexer.c \
       src/parser.c \
       src/codegen_c.c \
       src/main.c

OBJS = $(SRCS:src/%.c=build/%.o)
TARGET = bin/mar

all: dirs $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

dirs:
	mkdir -p bin build

clean:
	rm -rf build bin

release: clean
	$(MAKE) CFLAGS="-std=c11 -Wall -Wextra -O2 -Iinclude" all

.PHONY: all clean dirs release
