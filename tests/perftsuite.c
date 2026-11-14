/***************************************************************
 * perftsuite.c: checks the engine's move generator against a plain,
 * slow one, on every size of board.
 *
 *   perftsuite [DEPTH [GAMES]]
 *
 * Without arguments, as on NICE, DEPTH and GAMES are PERFT_DEPTH and
 * PERFT_GAMES, which the build may set.
 *
 * For each size, from 4x4 to 12x6, it walks the move tree from the
 * start to DEPTH plies. At every node it checks the engine against
 * this file's own generator, which looks at the board square by
 * square:
 *
 *   - the legal moves, as a set, and the end of the game;
 *   - their order: to the last rank, then captures, then the rest;
 *   - the position after each move, and that no bit is set off the
 *     board or for both sides;
 *   - the position as text, read back, and each move as text, parsed.
 *
 * Then it plays GAMES games of moves at random, to their end, and
 * checks each position of them the same way. Those reach what a tree
 * from the start does not: captures at the edges, and every way a
 * game ends. The moves at random come from this file's own generator,
 * so they are the same on every machine.
 *
 * It prints, for each size, the number of paths of each depth, which
 * engine_perft must give too, and how the games ended, then "ok" or
 * the first differences.
 * The program's result is 0 if all agree, and 1 if not.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine/engine.h"

#define MAX_DEPTH 12

// The depth and the games without arguments, as on NICE.
#ifndef PERFT_DEPTH
#define PERFT_DEPTH 4
#endif
#ifndef PERFT_GAMES
#define PERFT_GAMES 100
#endif

static int files, ranks;
static uint64_t counts[MAX_DEPTH + 1];
static int errors;

// The board, square by square: '.', 'X' (White), or 'O' (Black).
typedef char board_t[ENGINE_MAX_RANKS][ENGINE_MAX_FILES];

static void error(const position_t *p, const char *what)
{
    char s[ENGINE_POSITION_STR_SIZE];
    if (errors < 10)
        printf("%dx%d %s: %s\n", files, ranks, engine_position_str(p, s), what);
    errors++;
}

static void to_board(const position_t *p, board_t b)
{
    for (int r = 0; r < ranks; r++)
        for (int f = 0; f < files; f++)
        {
            int pawn = engine_pawn(p, f, r);
            b[r][f] = pawn == WHITE ? 'X' : pawn == BLACK ? 'O' : '.';
        }
}

// The slow generator: the legal moves of side on b.
static int slow_moves(board_t b, int side, move_t *list)
{
    char us = side == WHITE ? 'X' : 'O', them = side == WHITE ? 'O' : 'X';
    int dir = side == WHITE ? 1 : -1;
    int back = side == WHITE ? 0 : ranks - 1;
    for (int f = 0; f < files; f++)
        if (b[back][f] == them)
            return 0;
    int n = 0;
    for (int r = 0; r < ranks; r++)
        for (int f = 0; f < files; f++)
        {
            int r2 = r + dir;
            if (b[r][f] != us || r2 < 0 || r2 >= ranks)
                continue;
            if (b[r2][f] == '.')
                list[n++] = MOVE(SQUARE(f, r), SQUARE(f, r2));
            for (int f2 = f - 1; f2 <= f + 1; f2 += 2)
                if (f2 >= 0 && f2 < files && b[r2][f2] == them)
                    list[n++] = MOVE(SQUARE(f, r), SQUARE(f2, r2));
        }
    return n;
}

static int contains(const move_t *list, int n, move_t m)
{
    for (int i = 0; i < n; i++)
        if (list[i] == m)
            return 1;
    return 0;
}

// Whether a position's bits are on the board only, and not on one
// square for both sides.
static int clean(const position_t *p)
{
    uint32_t valid[3] = {0, 0, 0};
    for (int r = 0; r < ranks; r++)
        for (int f = 0; f < files; f++)
            valid[r >> 1] |= 1u << ((r & 1) * 16 + f);
    for (int i = 0; i < 3; i++)
        if ((p->pawns[WHITE][i] | p->pawns[BLACK][i]) & ~valid[i]
            || p->pawns[WHITE][i] & p->pawns[BLACK][i])
            return 0;
    return 1;
}

// Checks the engine at p against the slow generator, and each move
// of p, and writes the engine's moves to list. Returns how many.
static int check_node(const position_t *p, move_t *list)
{
    board_t b;
    move_t slow[ENGINE_MAX_MOVES];
    char s[ENGINE_POSITION_STR_SIZE], ms[5];

    to_board(p, b);
    int n = engine_moves(p, list);
    int ns = slow_moves(b, p->side, slow);

    // The moves, as a set.
    if (n != ns)
        error(p, "the number of moves differs");
    for (int i = 0; i < n; i++)
        if (!contains(slow, ns, list[i]))
            error(p, "a move is not legal");
    for (int i = 0; i < ns; i++)
        if (!contains(list, n, slow[i]))
            error(p, "a legal move is missing");

    // engine_touchdown, which is a part of the end of the game.
    int back = p->side == WHITE ? 0 : ranks - 1;
    int td = 0;
    for (int f = 0; f < files; f++)
        td |= b[back][f] == (p->side == WHITE ? 'O' : 'X');
    if (engine_touchdown(p) != td)
        error(p, "engine_touchdown differs");

    // The order: to the last rank (0), captures (1), the rest (2).
    int last = p->side == WHITE ? ranks - 1 : 0, group = 0;
    for (int i = 0; i < n; i++)
    {
        int from = MOVE_FROM(list[i]), to = MOVE_TO(list[i]);
        int g = SQUARE_RANK(to) == last ? 0 : SQUARE_FILE(from) != SQUARE_FILE(to) ? 1 : 2;
        if (g < group)
            error(p, "the moves are out of order");
        group = g;
    }

    // The position as text.
    position_t q;
    engine_position_str(p, s);
    if (engine_set_position(&q, s) || memcmp(&q, p, sizeof q))
        error(p, "the position as text reads back as another");

    for (int i = 0; i < n; i++)
    {
        if (engine_parse_move(p, engine_move_str(list[i], ms)) != list[i])
            error(p, "a move as text parses as another");

        // The move, on the slow board and by the engine.
        int from = MOVE_FROM(list[i]), to = MOVE_TO(list[i]);
        board_t b2;
        memcpy(b2, b, sizeof b2);
        b2[SQUARE_RANK(to)][SQUARE_FILE(to)] = b2[SQUARE_RANK(from)][SQUARE_FILE(from)];
        b2[SQUARE_RANK(from)][SQUARE_FILE(from)] = '.';

        engine_make(p, list[i], &q);
        board_t bq;
        to_board(&q, bq);
        if (!clean(&q) || q.side != (p->side ^ 1))
            error(p, "a move leaves bits off the board, or the side");
        for (int r = 0; r < ranks; r++)
            if (memcmp(bq[r], b2[r], (size_t) files))
                error(p, "a move gives another position");
    }
    return n;
}

// The move tree from p, ply plies from the start, to depth.
static void walk(const position_t *p, int ply, int depth)
{
    move_t list[ENGINE_MAX_MOVES];
    int n = check_node(p, list);
    if (ply == depth)
        return;
    counts[ply + 1] += (uint64_t) n;
    for (int i = 0; i < n; i++)
    {
        position_t q;
        engine_make(p, list[i], &q);
        walk(&q, ply + 1, depth);
    }
}

// A generator of numbers at random, the same on every machine.
static uint32_t rnd_state = 1;
static uint32_t rnd(uint32_t n)
{
    rnd_state = rnd_state * 1103515245u + 12345u;
    return (rnd_state >> 16) % n;
}

// Plays games of moves at random to their end, and checks each
// position. Prints how many each side won, and how the games ended:
// by a pawn on the last rank, with no pawns, or with no move.
static void play(int games)
{
    int won[2] = {0, 0}, touchdowns = 0, no_pawns = 0, blocked = 0, plies = 0;
    for (int g = 0; g < games; g++)
    {
        position_t p;
        move_t list[ENGINE_MAX_MOVES];
        engine_start(&p);
        int n, ply = 0;
        while ((n = check_node(&p, list)) > 0)
        {
            engine_make(&p, list[rnd((uint32_t) n)], &p);
            if (++ply > ENGINE_MAX_PLIES)
            {
                error(&p, "the game is longer than ENGINE_MAX_PLIES");
                break;
            }
        }
        const uint32_t *w = p.pawns[p.side];
        won[p.side ^ 1]++;
        plies += ply;
        if (engine_touchdown(&p))
            touchdowns++;
        else if (!(w[0] | w[1] | w[2]))
            no_pawns++;
        else
            blocked++;
    }
    printf("games %dx%d: White %d, Black %d; touchdown %d, no pawns %d, no move %d; %d plies\n",
           files, ranks, won[WHITE], won[BLACK], touchdowns, no_pawns, blocked, plies);
}

int main(int argc, char **argv)
{
    int depth = argc > 1 ? atoi(argv[1]) : PERFT_DEPTH;
    int games = argc > 2 ? atoi(argv[2]) : PERFT_GAMES;
    if (depth < 1 || depth > MAX_DEPTH || games < 0)
    {
        printf("perftsuite DEPTH GAMES: DEPTH is 1 to %d\n", MAX_DEPTH);
        return 1;
    }

    for (ranks = ENGINE_MIN_RANKS; ranks <= ENGINE_MAX_RANKS; ranks++)
        for (files = ENGINE_MIN_FILES; files <= ENGINE_MAX_FILES; files++)
        {
            position_t p;
            engine_set_size(files, ranks);
            engine_start(&p);
            memset(counts, 0, sizeof counts);
            walk(&p, 0, depth);
            printf("perft %dx%d:", files, ranks);
            for (int d = 1; d <= depth; d++)
            {
                printf(" %llu", (unsigned long long) counts[d]);
                if (engine_perft(&p, d) != counts[d])
                    error(&p, "engine_perft differs");
            }
            printf("\n");
            play(games);
        }

    if (errors)
        printf("perft suite to depth %d: %d errors\n", depth, errors);
    else
        printf("perft suite to depth %d: ok\n", depth);
    return errors ? 1 : 0;
}
