/***************************************************************
 * solve.c: solves positions of TouchDown, on the host: whether the
 * side to move wins or loses, with best play.
 *
 *   solve [OPTIONS] SIZE|POSITION...
 *
 * Each argument is a size, such as 6x4, for its start, or a position
 * as text, such as "OOOO/..../..../XXXX x" (engine_set_position).
 *
 * The search. A game has no draws, and every move takes a pawn one
 * rank forward, so a game is short and has no cycles: a position is a
 * WIN if one of its moves leads to a LOSS for the opponent, and else a
 * LOSS (also when it has no move). The search is depth-first, and
 * stops at the first move that wins. At each position it tries first
 * a move to the last rank, then a move to a position that has no
 * move, or that the table has as a LOSS, and then the others, the one
 * whose position the evaluation (engine_eval) has worst for the
 * opponent first (--order plain: in the order of engine_moves).
 * Before the moves, the rule of races (below) may know the value
 * already (--no-races: not).
 *
 * The table. Every position searched is kept in a table of --hash
 * megabytes, with its value, so that a position that comes again by
 * other moves is not searched again. A position is kept as the side to
 * move sees it, with its board turned around when that is Black, so
 * that White to move and Black to move share their entries; and as
 * the smaller of it and its mirror, files a and h swapped, which has
 * the same value. When a bucket of the table is full, the entry whose
 * search took the fewest nodes goes.
 *
 * --threads N searches with N threads, which share the table: each
 * solves the root, in an order of the moves of its own, and the first
 * to finish stops the others. An entry is written without a lock, as
 * a and b ^ a, so that one that two threads wrote at once, half of
 * each, matches no key. The counts are then not the same every time.
 *
 * --moves prints the value of each move at the root, which solves
 * each of them.
 *
 * --check FILE solves every position of a database (tools/db.h), with
 * one table, and compares them with its values.
 *
 * --dump FILE writes, as a database, every position that can come
 * from its one argument, with its value: a search of every move, with
 * no rule of races and no stop at a move that wins, and with a table
 * that keeps every position (a full bucket of it gives on to the
 * next), so that each comes once. A position, the same with the other
 * side to move, and their mirrors come as one, as they share an entry.
 *
 * It prints the value and the nodes, and then the time on a line of
 * its own, which tests/solve.grep leaves out.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/engine/engine.h"
#include "db.h"

static int order_eval = 1;
static int show_moves;
static int use_races = 1;
static long hash_mb = 1024;
static const char *check_file;
static const char *dump_file;
static const char *book_file;

static int files, ranks;
static int n_threads = 1;

// The counts of a thread, and of all of them.
static __thread uint64_t nodes, stored, replaced;
static __thread int thread_id;
static __thread uint64_t rng;
static uint64_t total_nodes, total_stored, total_replaced;
static volatile int stop; // A thread has solved the root: the others stop.

/***************************************************************
 * The table
 ***************************************************************/

// An entry: the pawns of the side to move, as it sees the board, its
// ranks 0 to ranks - 2 by files bits each, in a, with in the top 4
// bits the log2 of the nodes its search took; and the pawns of the
// opponent, ranks 1 to ranks - 1, in b, with the value in bit 63, 1 a
// WIN. Both have at most 5 * 12 = 60 bits. a is 0 for an empty entry:
// a position that is kept has a pawn of the side to move.
typedef struct
{
    uint64_t a, b;
} entry_t;

#define KEY_MASK ((UINT64_C(1) << 60) - 1)
#define BUCKET 4

static entry_t *table;
static uint64_t n_buckets;

// The buckets an entry may go to, from the one of its key: 1, when a
// full bucket loses an entry to a new one; more for --dump, which
// must keep every position, and stops when they are all full.
static int spill = 1;

static void table_init(void)
{
    n_buckets = 1;
    while (n_buckets * 2 * BUCKET * sizeof(entry_t) <= (uint64_t) hash_mb << 20)
        n_buckets *= 2;
    table = calloc(n_buckets * BUCKET, sizeof(entry_t));
    if (!table)
    {
        perror("solve");
        exit(1);
    }
}

static void table_clear(void)
{
    memset(table, 0, n_buckets * BUCKET * sizeof(entry_t));
    total_nodes = total_stored = total_replaced = 0;
    nodes = stored = replaced = 0;
}

static void set_size(int f, int r)
{
    files = f;
    ranks = r;
}

#define rank_bits db_rank_bits

// The key of p, which must not be over (db.h).
static entry_t key(const position_t *p)
{
    db_key_t k = db_key(p);
    entry_t e = {k.a, k.b};
    return e;
}

static uint64_t bucket_of(entry_t k)
{
    uint64_t h = k.a * UINT64_C(0x9e3779b97f4a7c15) ^ k.b * UINT64_C(0xc2b2ae3d27d4eb4f);
    h ^= h >> 31;
    h *= UINT64_C(0xbf58476d1ce4e5b9);
    h ^= h >> 29;
    return h & (n_buckets - 1);
}

static entry_t *bucket(entry_t k, int j)
{
    return table + ((bucket_of(k) + (uint64_t) j) & (n_buckets - 1)) * BUCKET;
}

// The value of k in the table: 1 a WIN, 0 a LOSS, -1 not in it.
static int probe(entry_t k)
{
    for (int j = 0; j < spill; j++)
    {
        entry_t *e = bucket(k, j);
        for (int i = 0; i < BUCKET; i++)
        {
            // b is kept as b ^ a, so that an entry that two threads
            // wrote at once, half of each, matches no key.
            uint64_t a = e[i].a, b = e[i].b ^ a;
            if (!a)
                return -1; // The buckets after it have none of k.
            if ((a & KEY_MASK) == k.a && (b & KEY_MASK) == k.b)
                return (int) (b >> 63);
        }
    }
    return -1;
}

static void store(entry_t k, int win, uint64_t work)
{
    int w = 0;
    while (w < 15 && work >> (w + 1))
        w++;
    entry_t *e = bucket(k, 0), *victim = e;
    for (int j = 1; spill > 1 && j <= spill; j++)
    {
        // --dump: the first free entry of the buckets.
        int i = 0;
        while (i < BUCKET && e[i].a)
            i++;
        if (i < BUCKET)
            break;
        if (j == spill)
        {
            fprintf(stderr, "solve: the table is full; a larger --hash\n");
            exit(1);
        }
        e = victim = bucket(k, j);
    }
    for (int i = 0; i < BUCKET; i++)
    {
        if (!e[i].a || ((e[i].a & KEY_MASK) == k.a && ((e[i].b ^ e[i].a) & KEY_MASK) == k.b))
        {
            victim = e + i;
            break;
        }
        if (e[i].a >> 60 < victim->a >> 60)
            victim = e + i;
    }
    uint64_t va = victim->a;
    if (va && ((va & KEY_MASK) != k.a || ((victim->b ^ va) & KEY_MASK) != k.b))
        replaced++;
    else if (!va)
        stored++;
    uint64_t a = k.a | (uint64_t) w << 60;
    victim->a = a;
    victim->b = (k.b | (uint64_t) win << 63) ^ a;
}

/***************************************************************
 * Races
 *
 * A pawn is free when nothing can ever stop it: no pawn of its own is
 * ahead of it on its file, and no enemy pawn can ever come to a
 * square from which it blocks or takes it. An enemy pawn moves a
 * rank a move towards it, and a file only when it takes, so from a
 * rank d ahead of the pawn, it can come at most d - 1 files nearer by
 * the time it is one rank ahead of it; it can stop the pawn if it is
 * at most d files away. A free pawn reaches the last rank in as many
 * moves as it has ranks to go, and no pawn in fewer. So the side to
 * move wins if it has a free pawn no slower than every pawn of the
 * opponent, and loses if the opponent has a free pawn faster than
 * every pawn of its own.
 ***************************************************************/

// The pawns of side c of p, as the side to move sees the board: the
// file and the rank of each, the side to move moving up.
static int pawns_of(const position_t *p, int c, int *f, int *r)
{
    int n = 0;
    for (int rank = 0; rank < ranks; rank++)
    {
        int rr = p->side == WHITE ? rank : ranks - 1 - rank;
        uint32_t bits = rank_bits(p, c, rr);
        for (int file = 0; file < files; file++)
            if (bits >> file & 1)
                f[n] = file, r[n++] = rank;
    }
    return n;
}

static int dist(int a, int b)
{
    return a > b ? a - b : b - a;
}

// The value of p by races: 1 a WIN, 0 a LOSS, -1 not known.
static int race(const position_t *p)
{
    int mf[ENGINE_MAX_FILES], mr[ENGINE_MAX_FILES], of[ENGINE_MAX_FILES], orr[ENGINE_MAX_FILES];
    int nm = pawns_of(p, p->side, mf, mr), no = pawns_of(p, !p->side, of, orr);

    // The moves the fastest pawn of each needs, and the fastest free one.
    int m_min = 99, m_free = 99, o_min = 99, o_free = 99;
    for (int i = 0; i < nm; i++)
    {
        int d = ranks - 1 - mr[i], free = 1;
        if (d < m_min)
            m_min = d;
        for (int j = 0; j < nm && free; j++)
            free = !(mf[j] == mf[i] && mr[j] > mr[i]);
        for (int j = 0; j < no && free; j++)
            free = !(orr[j] > mr[i] && dist(of[j], mf[i]) <= orr[j] - mr[i]);
        if (free && d < m_free)
            m_free = d;
    }
    for (int i = 0; i < no; i++)
    {
        int d = orr[i], free = 1;
        if (d < o_min)
            o_min = d;
        for (int j = 0; j < no && free; j++)
            free = !(of[j] == of[i] && orr[j] < orr[i]);
        for (int j = 0; j < nm && free; j++)
            free = !(mr[j] < orr[i] && dist(mf[j], of[i]) <= orr[i] - mr[j]);
        if (free && d < o_free)
            o_free = d;
    }
    if (m_free <= o_min)
        return 1;
    if (o_free < m_min)
        return 0;
    return -1;
}

/***************************************************************
 * The search
 ***************************************************************/

// Whether the side to move of p wins. p must have n moves, list.
static int solve_moves(const position_t *p, const move_t *list, int n)
{
    uint64_t start = nodes++;
    entry_t k = key(p);
    int v = probe(k);
    if (v >= 0)
        return v;

    // A move to the last rank wins; they come first.
    int last = p->side == WHITE ? ranks - 1 : 0;
    if (SQUARE_RANK(MOVE_TO(list[0])) == last)
    {
        store(k, 1, 1);
        return 1;
    }

    if (use_races && (v = race(p)) >= 0)
    {
        store(k, v, 1);
        return v;
    }

    // The positions after the moves; one that has no move, or that
    // the table has as a LOSS, wins.
    position_t q[ENGINE_MAX_MOVES];
    move_t l2[ENGINE_MAX_MOVES][ENGINE_MAX_MOVES];
    int n2[ENGINE_MAX_MOVES], score[ENGINE_MAX_MOVES], idx[ENGINE_MAX_MOVES];
    for (int i = 0; i < n; i++)
    {
        engine_make(p, list[i], &q[i]);
        n2[i] = engine_moves(&q[i], l2[i]);
        if (n2[i] == 0 || probe(key(&q[i])) == 0)
        {
            store(k, 1, nodes - start);
            return 1;
        }
        score[i] = order_eval ? engine_eval(&q[i]) : 0;
        if (thread_id)
        {
            // Each thread but the first its own order, so that the
            // threads search different positions.
            rng = rng * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
            score[i] += (int) (rng >> 58);
        }
        idx[i] = i;
    }

    // The others, the worst for the opponent first (an insertion
    // sort, stable, so that ties keep the order of engine_moves).
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && score[idx[j]] < score[idx[j - 1]]; j--)
        {
            int t = idx[j];
            idx[j] = idx[j - 1];
            idx[j - 1] = t;
        }
    int win = 0;
    for (int i = 0; i < n && !win; i++)
    {
        win = !solve_moves(&q[idx[i]], l2[idx[i]], n2[idx[i]]);
        if (stop)
            return 0; // Not solved: kept nowhere.
    }
    store(k, win, nodes - start);
    return win;
}

// Whether the side to move of p wins. A position set up with a pawn
// of the side to move on its last rank is a WIN already; the table
// has no room for it.
static int solve(const position_t *p)
{
    if (rank_bits(p, p->side, p->side == WHITE ? ranks - 1 : 0))
        return 1;
    move_t list[ENGINE_MAX_MOVES];
    int n = engine_moves(p, list);
    return n ? solve_moves(p, list, n) : 0;
}

// The threads of --threads: each solves the root, with the table
// shared; the first to finish stops the others.
static const position_t *root;
static int root_win;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void add_counts(void)
{
    pthread_mutex_lock(&lock);
    total_nodes += nodes;
    total_stored += stored;
    total_replaced += replaced;
    nodes = stored = replaced = 0;
    pthread_mutex_unlock(&lock);
}

static void *thread_main(void *arg)
{
    thread_id = (int) (intptr_t) arg;
    rng = (uint64_t) thread_id * UINT64_C(0x9e3779b97f4a7c15);
    int win = solve(root);
    pthread_mutex_lock(&lock);
    if (!stop)
    {
        root_win = win;
        stop = 1;
    }
    pthread_mutex_unlock(&lock);
    add_counts();
    return NULL;
}

// Whether the side to move of p wins, by n_threads threads.
static int solve_threads(const position_t *p)
{
    if (n_threads == 1)
    {
        int win = solve(p);
        add_counts();
        return win;
    }
    pthread_t t[256];
    root = p;
    stop = 0;
    for (int i = 0; i < n_threads; i++)
        if (pthread_create(&t[i], NULL, thread_main, (void *) (intptr_t) i))
        {
            perror("solve");
            exit(1);
        }
    for (int i = 0; i < n_threads; i++)
        pthread_join(t[i], NULL);
    stop = 0;
    return root_win;
}

/***************************************************************
 * The front
 ***************************************************************/

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// The position of an argument, a size for its start or a position as
// text, and its size set.
static void parse_arg(const char *arg, position_t *p)
{
    int f, r;
    char c;
    if (sscanf(arg, "%dx%d%c", &f, &r, &c) == 2)
    {
        if (engine_set_size(f, r))
        {
            fprintf(stderr, "solve: no size %s\n", arg);
            exit(2);
        }
        engine_start(p);
    }
    else if (engine_set_position(p, arg))
    {
        fprintf(stderr, "solve: not a position: %s\n", arg);
        exit(2);
    }
    set_size(engine_files(), engine_ranks());
}

static void solve_arg(const char *arg)
{
    position_t p;
    parse_arg(arg, &p);
    table_clear();

    char text[ENGINE_POSITION_STR_SIZE], ms[8];
    double t0 = now();
    int win = solve_threads(&p);
    printf("%dx%d %s: %s, %llu nodes, %llu positions kept, %llu replaced\n", files, ranks,
           engine_position_str(&p, text), win ? "WIN" : "LOSS", (unsigned long long) total_nodes,
           (unsigned long long) total_stored, (unsigned long long) total_replaced);
    printf("  time %.2f s\n", now() - t0);
    fflush(stdout);

    if (show_moves)
    {
        move_t list[ENGINE_MAX_MOVES];
        int n = engine_moves(&p, list);
        for (int i = 0; i < n; i++)
        {
            position_t q;
            engine_make(&p, list[i], &q);
            printf("  %s %s\n", engine_move_str(list[i], ms), solve_threads(&q) ? "loses" : "wins");
        }
        fflush(stdout);
    }
}

/***************************************************************
 * --book: the engine's opening book
 ***************************************************************/

// The moves of p that win, in the order of engine_moves, into out, and
// their number: those that end the game, or whose position the table
// has as a LOSS for the opponent, or else the search solves as one. 0
// if none, when p is a LOSS.
static int winning_moves(const position_t *p, move_t *out)
{
    move_t list[ENGINE_MAX_MOVES], l2[ENGINE_MAX_MOVES];
    position_t q;
    int n = engine_moves(p, list), k = 0;
    for (int i = 0; i < n; i++)
    {
        engine_make(p, list[i], &q);
        int v = engine_moves(&q, l2) == 0 ? 0 : probe(key(&q));
        if (v < 0)
            v = solve_threads(&q);
        if (v == 0)
            out[k++] = list[i];
    }
    return k;
}

// Writes the moves of list, n of them, as text to fp.
static void book_moves_str(FILE *fp, const move_t *list, int n)
{
    char ms[8];
    for (int i = 0; i < n; i++)
        fprintf(fp, " %s", engine_move_str(list[i], ms));
}

// Writes the entry of the book of the start of the size of arg to fp:
// the files, the ranks, 1 if the start is a WIN for White and else 0,
// the number of the numbers that follow, and they: if a WIN, White's
// first moves that win; if a LOSS, for each of White's first moves,
// the number of Black's replies that win, and they.
static void book_size(FILE *fp, const char *arg)
{
    position_t p, q;
    parse_arg(arg, &p);
    engine_start(&p);
    table_clear();
    double t0 = now();
    int win = solve_threads(&p);
    char ms[8], text[ENGINE_POSITION_STR_SIZE];
    move_t list[ENGINE_MAX_MOVES], wins[ENGINE_MAX_MOVES];
    uint16_t data[ENGINE_MAX_MOVES * (ENGINE_MAX_MOVES + 1)];
    int len = 0;
    if (win)
    {
        int k = winning_moves(&p, wins);
        fprintf(fp, "    // %dx%d: a WIN for White, by", files, ranks);
        book_moves_str(fp, wins, k);
        fprintf(fp, "\n");
        for (int i = 0; i < k; i++)
            data[len++] = wins[i];
    }
    else
    {
        int n = engine_moves(&p, list);
        fprintf(fp, "    // %dx%d: a LOSS for White; Black's replies that win:\n", files, ranks);
        for (int i = 0; i < n; i++)
        {
            engine_make(&p, list[i], &q);
            int k = winning_moves(&q, wins);
            if (!k)
            {
                fprintf(stderr, "solve: %s: no reply wins\n", engine_position_str(&q, text));
                exit(1);
            }
            fprintf(fp, "    //   %s:", engine_move_str(list[i], ms));
            book_moves_str(fp, wins, k);
            fprintf(fp, "\n");
            data[len++] = (uint16_t) k;
            for (int j = 0; j < k; j++)
                data[len++] = wins[j];
        }
    }
    fprintf(fp, "    %d, %d, %d, %d,", files, ranks, win, len);
    for (int i = 0; i < len; i++)
        fprintf(fp, "%s %s%x,", i % 8 == 0 ? "\n   " : "", data[i] < 0x100 ? "" : "0x", data[i]);
    fprintf(fp, "\n");
    printf("book %dx%d: %s, %.0f s\n", files, ranks, win ? "WIN" : "LOSS", now() - t0);
    fflush(stdout);
}

// Writes the book of the sizes, n of them, to file; argc and argv are
// the command line, for its comment.
static int book(const char *file, int n, char **sizes, int argc, char **argv)
{
    FILE *fp = fopen(file, "w");
    if (!fp)
    {
        perror(file);
        return 1;
    }
    fprintf(fp, "/***************************************************************\n"
                " * book.h: the engine's opening book, of the start of the sizes that\n"
                " * tools/solve.c solves. Written by it (make book):\n"
                " *\n"
                " *   build/host/solve");
    for (int i = 1; i < argc; i++)
        fprintf(fp, " %s", argv[i]);
    fprintf(fp, "\n"
                " *\n"
                " * Each entry: the files, the ranks, 1 if the start is a WIN for\n"
                " * White and 0 if a LOSS, the number of the numbers that follow, and\n"
                " * they: if a WIN, all of White's first moves that win; if a LOSS,\n"
                " * for each of White's first moves, in the order of engine_moves, the\n"
                " * number of Black's replies that win, and they. A move is a move_t (engine.h): the square it is from in the\n"
                " * low byte, and the square it is to in the high byte, a square\n"
                " * rank * 16 + file. 0 ends the book.\n"
                " *\n"
                " * Do not edit.\n"
                " ***************************************************************/\n"
                "\n"
                "static const uint16_t book[] = {\n");
    for (int i = 0; i < n; i++)
        book_size(fp, sizes[i]);
    fprintf(fp, "    0,\n};\n");
    if (fclose(fp))
    {
        perror(file);
        return 1;
    }
    return 0;
}

// Solves every position of a database (db.h), and compares them.
static int check(const char *file)
{
    db_t db;
    db_load(&db, file);
    set_size(db.files, db.ranks);
    table_clear();
    long wins = 0, losses = 0, diffs = 0;
    char text[ENGINE_POSITION_STR_SIZE];
    for (long i = 0; i < db.n; i++)
    {
        int win = solve(&db.pos[i]);
        wins += db.win[i];
        losses += !db.win[i];
        if (win != db.win[i] && diffs++ < 10)
            printf("%s: the database has %s, solve %s\n", engine_position_str(&db.pos[i], text),
                   db.win[i] ? "WIN" : "LOSS", win ? "WIN" : "LOSS");
    }
    printf("solve %dx%d: %ld positions, %ld wins, %ld losses, %ld differences\n", db.files, db.ranks, db.n,
           wins, losses, diffs);
    return diffs != 0;
}

/***************************************************************
 * --dump: every position that can come from the root
 ***************************************************************/

static long dump_n, dump_wins;

// Writes to fp each position that can come from p, which has n moves,
// list, and is not yet in the table, with its value, after those that
// can come from it; and returns the value of p. It searches every
// move, without the rule of races, so that it comes to every position.
static int dump_moves(const position_t *p, const move_t *list, int n, FILE *fp)
{
    nodes++;
    entry_t k = key(p);
    int v = probe(k);
    if (v >= 0)
        return v;
    int win = 0;
    for (int i = 0; i < n; i++)
    {
        position_t q;
        move_t l2[ENGINE_MAX_MOVES];
        engine_make(p, list[i], &q);
        int n2 = engine_moves(&q, l2);
        // A move to the last rank leaves the opponent no move.
        if (n2 == 0 || !dump_moves(&q, l2, n2, fp))
            win = 1;
    }
    store(k, win, 1);
    char text[ENGINE_POSITION_STR_SIZE];
    fprintf(fp, "%s %s\n", engine_position_str(p, text), win ? "WIN" : "LOSS");
    dump_n++;
    dump_wins += win;
    return win;
}

static int dump(const char *file, const position_t *p)
{
    FILE *fp = fopen(file, "w");
    if (!fp)
    {
        perror(file);
        return 1;
    }
    move_t list[ENGINE_MAX_MOVES];
    int n = db_over(p) ? 0 : engine_moves(p, list);
    table_clear();
    spill = 64;
    dump_n = dump_wins = 0;
    if (n)
        dump_moves(p, list, n, fp);
    spill = 1;
    if (fclose(fp))
    {
        perror(file);
        return 1;
    }
    char text[ENGINE_POSITION_STR_SIZE];
    printf("dump %dx%d %s: %ld positions, %ld wins, %ld losses, %llu nodes\n", files, ranks,
           engine_position_str(p, text), dump_n, dump_wins, dump_n - dump_wins, (unsigned long long) nodes);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: solve [OPTIONS] SIZE|POSITION...\n"
            "  --hash MB      the size of the table (1024)\n"
            "  --order eval   try the moves the evaluation likes first (the default)\n"
            "  --order plain  in the order of the move generator\n"
            "  --threads N    N threads, which share the table (1)\n"
            "  --no-races     without the rule of free pawns\n"
            "  --moves        the value of each move at the root\n"
            "  --check FILE   solve every position of a database (tools/db.h), and compare\n"
            "  --dump FILE    write every position that can come from the one argument, with\n"
            "                 its value, as a database\n"
            "  --book FILE    write the engine's opening book of the start of the sizes, as\n"
            "                 src/engine/book.h\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++)
    {
        const char *a = argv[i];
        if (i + 1 < argc && !strcmp(a, "--threads"))
        {
            n_threads = atoi(argv[++i]);
            if (n_threads < 1 || n_threads > 256)
                usage();
        }
        else if (!strcmp(a, "--no-races"))
            use_races = 0;
        else if (!strcmp(a, "--moves"))
            show_moves = 1;
        else if (i + 1 < argc && !strcmp(a, "--hash"))
            hash_mb = atol(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--check"))
            check_file = argv[++i];
        else if (i + 1 < argc && !strcmp(a, "--dump"))
            dump_file = argv[++i];
        else if (i + 1 < argc && !strcmp(a, "--book"))
            book_file = argv[++i];
        else if (i + 1 < argc && !strcmp(a, "--order"))
        {
            const char *v = argv[++i];
            if (!strcmp(v, "eval"))
                order_eval = 1;
            else if (!strcmp(v, "plain"))
                order_eval = 0;
            else
                usage();
        }
        else
            usage();
    }
    if (hash_mb < 1 || (i == argc && !check_file) || (dump_file && i != argc - 1))
        usage();
    table_init();
    if (book_file)
        return book(book_file, argc - i, argv + i, argc, argv);
    if (dump_file)
    {
        position_t p;
        parse_arg(argv[i], &p);
        return dump(dump_file, &p);
    }
    int status = check_file ? check(check_file) : 0;
    for (; i < argc; i++)
        solve_arg(argv[i]);
    return status;
}
