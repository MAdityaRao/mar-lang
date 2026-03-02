#ifndef MAR_ARENA_H
#define MAR_ARENA_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_SIZE (1024 * 1024)

typedef struct ArenaBlock {
    char             *data;
    size_t            used;
    size_t            cap;
    struct ArenaBlock *next;
} ArenaBlock;

typedef struct {
    ArenaBlock *head;
    size_t      total_allocated;
} Arena;

Arena  *arena_create(void);
void   *arena_alloc (Arena *a, size_t size);
char   *arena_strdup(Arena *a, const char *s);
void    arena_destroy(Arena *a);

extern Arena *g_arena;

#define MAR_ALLOC(T)      ((T*)arena_alloc(g_arena, sizeof(T)))
#define MAR_ALLOC_N(T, N) ((T*)arena_alloc(g_arena, sizeof(T)*(N)))
#define MAR_STRDUP(s)     (arena_strdup(g_arena, (s)))

#endif