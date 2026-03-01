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
# Add this to the end of your Makefile

test: all
	@echo "Running integration tests..."
	./bin/mar run examples/hello.mar
	./bin/mar run examples/primes.mar
	@echo "All tests passed!"

.PHONY: all clean dirs release test
