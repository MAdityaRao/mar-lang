#include "mar/arena.h"
#include <stdio.h>

Arena *g_arena = NULL;

Arena *arena_create(void) {
    Arena *a = malloc(sizeof(Arena));
    a->head = NULL;
    a->total_allocated = 0;
    return a;
}

static ArenaBlock *block_new(size_t cap) {
    ArenaBlock *b = malloc(sizeof(ArenaBlock));
    b->data = malloc(cap);
    b->used = 0;
    b->cap  = cap;
    b->next = NULL;
    return b;
}

void *arena_alloc(Arena *a, size_t size) {
    size = (size + 7) & ~7ULL; /* align to 8 bytes */
    if (!a->head || a->head->used + size > a->head->cap) {
        size_t cap = size > ARENA_BLOCK_SIZE ? size * 2 : ARENA_BLOCK_SIZE;
        ArenaBlock *b = block_new(cap);
        b->next = a->head;
        a->head = b;
    }
    void *ptr = a->head->data + a->head->used;
    a->head->used += size;
    a->total_allocated += size;
    memset(ptr, 0, size);
    return ptr;
}

char *arena_strdup(Arena *a, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char  *p   = arena_alloc(a, len);
    memcpy(p, s, len);
    return p;
}

void arena_destroy(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b->data);
        free(b);
        b = next;
    }
    free(a);
}
