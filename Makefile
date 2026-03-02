# ════════════════════════════════════════════════════════════════════════════
# Mar Language Compiler — Makefile
# ════════════════════════════════════════════════════════════════════════════

CC      = cc
CFLAGS  = -Wall -Wextra -Wno-unused-parameter -std=c11 -Iinclude
CFLAGS_DBG = $(CFLAGS) -g -O0 -DDEBUG -fsanitize=address,undefined
CFLAGS_REL = $(CFLAGS) -O2 -DNDEBUG

SRCS = src/arena.c \
       src/error.c \
       src/ast.c   \
       src/lexer.c \
       src/parser.c \
       src/codegen_c.c \
       src/lsp.c \
       src/main.c

OBJS = $(SRCS:src/%.c=build/%.o)
BIN  = bin/mar

.PHONY: all release debug clean test examples help

all: $(BIN)

$(BIN): $(OBJS) | bin
	$(CC) $(CFLAGS) -o $@ $^ -lm
	@echo "✓ Built $(BIN)"

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

bin build:
	mkdir -p $@

release:
	mkdir -p bin build
	$(CC) $(CFLAGS_REL) -o $(BIN) $(SRCS) -lm
	@echo "✓ Release build: $(BIN)"

debug:
	mkdir -p bin build
	$(CC) $(CFLAGS_DBG) -o $(BIN) $(SRCS) -lm
	@echo "✓ Debug build: $(BIN)"

clean:
	rm -rf build bin
	@echo "✓ Cleaned"

# Run all example .mar files
examples: $(BIN)
	@for f in examples/*.mar; do \
	    echo "\n--- $$f ---"; \
	    $(BIN) run $$f; \
	done

# Quick test
test: $(BIN)
	@echo "=== Running tests ==="; \
	PASS=0; FAIL=0; \
	for f in tests/integration/*.mar; do \
	    expected="tests/integration/expected/$$(basename $$f .mar).txt"; \
	    if [ -f "$$expected" ]; then \
	        actual=$$($(BIN) run $$f 2>/dev/null); \
	        exp=$$(cat $$expected); \
	        if [ "$$actual" = "$$exp" ]; then \
	            echo "  PASS: $$f"; PASS=$$((PASS+1)); \
	        else \
	            echo "  FAIL: $$f"; FAIL=$$((FAIL+1)); \
	        fi \
	    fi; \
	done; \
	echo "\nResults: $$PASS passed, $$FAIL failed"

help:
	@echo ""
	@echo "  make         — build the compiler (debug mode)"
	@echo "  make release — build optimized"
	@echo "  make debug   — build with sanitizers"
	@echo "  make clean   — remove build artifacts"
	@echo "  make examples— run all example programs"
	@echo "  make test    — run integration tests"
	@echo ""