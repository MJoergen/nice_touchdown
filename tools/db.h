/***************************************************************
 * db.h: databases of positions and their values, for the programs
 * of the host (tools/solve.c, tools/train.c, tests/tablebase.c).
 *
 * A database is a file of lines, of one size of board, in either of
 * two forms:
 *
 *   - touchdown_db -d's dump of the 4x4 board, from the side to move,
 *     X, its ranks from X's last down to its first:
 *
 *         1d : .... OO.. .X.. X...  :    ... WIN
 *
 *   - solve --dump's, of any size: a position as text
 *     (engine_set_position), then its value:
 *
 *         OOOO/..../.X../XX.X o LOSS
 *
 * The value is that of the side to move, WIN or LOSS.
 *
 * A key names a position that is not over, whichever side is to move
 * and whichever way round its files are: the pawns of the side to
 * move, as it sees the board, ranks 0 to ranks - 2 by files bits each,
 * in a; the pawns of the opponent, ranks 1 to ranks - 1, in b; and of
 * it and its mirror, files a and h swapped, which has the same value,
 * the smaller. Both have at most 5 * 12 = 60 bits, so that their top
 * 4 bits are free for the table of the solver.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#ifndef DB_H
#define DB_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine/engine.h"

typedef struct
{
    uint64_t a, b;
} db_key_t;

// The files reversed, for the mirror, of the size of db_mirror_files.
static uint16_t db_mirror[1 << ENGINE_MAX_FILES];
static int db_mirror_files;

// The pawns of side c of p on rank r (from White's side), files bits.
static inline uint32_t db_rank_bits(const position_t *p, int c, int r)
{
    return p->pawns[c][r / 2] >> (16 * (r % 2)) & 0xfff;
}

// Whether p is over: a pawn of the opponent on the side to move's
// first rank, or one of the side to move on its last (a position set
// up so, as no game comes to it), or a side without pawns.
static inline int db_over(const position_t *p)
{
    int ranks = engine_ranks(), me = p->side;
    int first = me == WHITE ? 0 : ranks - 1, last = ranks - 1 - first;
    if (db_rank_bits(p, !me, first) || db_rank_bits(p, me, last))
        return 1;
    uint32_t mine = 0, yours = 0;
    for (int i = 0; i < 3; i++)
        mine |= p->pawns[me][i], yours |= p->pawns[!me][i];
    return !mine || !yours;
}

// The key of p, which must not be over (db_over).
static inline db_key_t db_key(const position_t *p)
{
    int files = engine_files(), ranks = engine_ranks();
    if (db_mirror_files != files)
    {
        for (int x = 0; x < 1 << files; x++)
        {
            int m = 0;
            for (int i = 0; i < files; i++)
                if (x >> i & 1)
                    m |= 1 << (files - 1 - i);
            db_mirror[x] = (uint16_t) m;
        }
        db_mirror_files = files;
    }
    int me = p->side, you = !me;
    uint64_t a = 0, b = 0, ma = 0, mb = 0;
    for (int r = 0; r < ranks; r++)
    {
        // r is the rank as the side to move sees it.
        int rr = me == WHITE ? r : ranks - 1 - r;
        uint32_t mine = db_rank_bits(p, me, rr), yours = db_rank_bits(p, you, rr);
        if (r < ranks - 1)
        {
            a |= (uint64_t) mine << (r * files);
            ma |= (uint64_t) db_mirror[mine] << (r * files);
        }
        if (r > 0)
        {
            b |= (uint64_t) yours << ((r - 1) * files);
            mb |= (uint64_t) db_mirror[yours] << ((r - 1) * files);
        }
    }
    db_key_t k = {a, b};
    if (ma < a || (ma == a && mb < b))
        k.a = ma, k.b = mb;
    return k;
}

/***************************************************************
 * A database in memory
 ***************************************************************/

typedef struct
{
    db_key_t key;
    long index; // In pos
} db_entry_t;

typedef struct
{
    int files, ranks;
    long n;               // The positions, in the order of the file
    position_t *pos;
    unsigned char *win;   // 1 a WIN
    long n_keys;          // Those not over, by key
    db_entry_t *keys;
} db_t;

static inline int db_compare(const void *x, const void *y)
{
    const db_key_t *p = &((const db_entry_t *) x)->key, *q = &((const db_entry_t *) y)->key;
    if (p->a != q->a)
        return p->a < q->a ? -1 : 1;
    return p->b < q->b ? -1 : p->b > q->b;
}

// Reads a database from fp, of name what for the messages, and sets
// the size of the board to its. Exits on an error.
static inline void db_read(db_t *db, FILE *fp, const char *what)
{
    static char line[256];
    long cap = 0;
    memset(db, 0, sizeof *db);
    while (fgets(line, sizeof line, fp))
    {
        char t[ENGINE_POSITION_STR_SIZE + 8];
        char *colon = strchr(line, ':');
        if (colon)
        {
            // touchdown_db's: 4x4, X to move.
            char *s = colon + 2;
            for (int r = 0; r < 4; r++, s += 5)
            {
                memcpy(t + r * 5, s, 4);
                t[r * 5 + 4] = r < 3 ? '/' : ' ';
            }
            t[20] = 'x';
            t[21] = '\0';
        }
        else
        {
            // solve's: the text up to the last space.
            char *sp = strrchr(line, ' ');
            if (!sp || sp - line >= (long) sizeof t)
                continue;
            memcpy(t, line, (size_t) (sp - line));
            t[sp - line] = '\0';
        }
        if (db->n == cap)
        {
            cap = cap ? 2 * cap : 1 << 16;
            db->pos = realloc(db->pos, (size_t) cap * sizeof *db->pos);
            db->win = realloc(db->win, (size_t) cap);
            if (!db->pos || !db->win)
            {
                perror(what);
                exit(1);
            }
        }
        if (engine_set_position(&db->pos[db->n], t))
        {
            fprintf(stderr, "%s: not a position: %s\n", what, t);
            exit(1);
        }
        if (db->n == 0)
            db->files = engine_files(), db->ranks = engine_ranks();
        else if (engine_files() != db->files || engine_ranks() != db->ranks)
        {
            fprintf(stderr, "%s: positions of more than one size\n", what);
            exit(1);
        }
        db->win[db->n++] = strstr(colon ? colon + 1 : line, "WIN") != NULL;
    }
    if (db->n == 0)
    {
        fprintf(stderr, "%s: no positions\n", what);
        exit(1);
    }
    db->keys = malloc((size_t) db->n * sizeof *db->keys);
    if (!db->keys)
    {
        perror(what);
        exit(1);
    }
    for (long i = 0; i < db->n; i++)
        if (!db_over(&db->pos[i]))
        {
            db->keys[db->n_keys].key = db_key(&db->pos[i]);
            db->keys[db->n_keys++].index = i;
        }
    qsort(db->keys, (size_t) db->n_keys, sizeof *db->keys, db_compare);
}

static inline void db_load(db_t *db, const char *file)
{
    FILE *fp = fopen(file, "r");
    if (!fp)
    {
        perror(file);
        exit(1);
    }
    db_read(db, fp, file);
    fclose(fp);
}

// The index of p in db->pos, or -1 if it is over or not in it. The
// size of the board must be db's.
static inline long db_lookup(const db_t *db, const position_t *p)
{
    if (db_over(p))
        return -1;
    db_entry_t e = {db_key(p), 0};
    db_entry_t *f = bsearch(&e, db->keys, (size_t) db->n_keys, sizeof e, db_compare);
    return f ? f->index : -1;
}

#endif
