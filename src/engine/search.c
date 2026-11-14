/***************************************************************
 * search.c: negamax with alpha-beta pruning, to a fixed depth.
 *
 * A position has one of three values, from the side to move: a Win
 * (ENGINE_WIN less the plies to it), a Loss (the negative of one),
 * or Even (0): neither is proved within the depth. A leaf is Even
 * unless the game is over there. As every move takes a pawn forward,
 * a game has no repetitions and no draws, and a Win or Loss found is
 * exact.
 *
 * The root searches its moves so that it knows each Even move as Even:
 * until it has an Even move, with a full window, so that the slowest
 * loss is exact when every move loses; then with alpha -1, so that a
 * later Even move scores 0 exactly; and once it has a win, with alpha
 * at that win, for a faster one only. Among its Even moves it picks
 * one at random.
 *
 * The positions and move lists of the plies are in the engine's own
 * data, so the recursion takes little of the stack.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

#define INF (ENGINE_WIN + 1)

static position_t stack_pos[ENGINE_MAX_DEPTH + 1];
static move_t stack_moves[ENGINE_MAX_DEPTH + 1][ENGINE_MAX_MOVES];

// The principal variation from each ply: pv[ply][0 .. pv_len[ply] - 1].
static move_t pv[ENGINE_MAX_DEPTH + 1][ENGINE_MAX_DEPTH];
static int pv_len[ENGINE_MAX_DEPTH + 1];

static uint32_t nodes;
static int (*stop_fn)(void);
static int stopped;

static uint32_t rnd_state = 1;

// Murmur3's finalizer: every bit of x stirs every bit of the result,
// so that seeds such as 1, 2, 3 give unrelated sequences.
static uint32_t mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return x;
}

void engine_seed(uint32_t seed)
{
    rnd_state = mix(seed ^ 0x9e3779b9u);
    if (!rnd_state)
        rnd_state = 1;
}

// A number of 0 to n - 1 at random: xorshift32, mixed.
static uint32_t rnd(uint32_t n)
{
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return mix(rnd_state) % n;
}

// The value of stack_pos[ply], searched depth plies further.
static int negamax(int ply, int depth, int alpha, int beta)
{
    pv_len[ply] = 0;
    nodes++;
    if ((nodes & 1023) == 0 && stop_fn && stop_fn())
        stopped = 1;
    if (stopped)
        return 0;

    move_t *list = stack_moves[ply];
    int n = engine_moves(&stack_pos[ply], list);
    if (n == 0)
        return -(ENGINE_WIN - ply);
    if (depth == 0)
        return 0;

    int best = -INF;
    for (int i = 0; i < n; i++)
    {
        engine_make(&stack_pos[ply], list[i], &stack_pos[ply + 1]);
        int score = -negamax(ply + 1, depth - 1, -beta, -alpha);
        if (stopped)
            return 0;
        if (score > best)
        {
            best = score;
            if (score > alpha)
            {
                alpha = score;
                pv[ply][0] = list[i];
                for (int j = 0; j < pv_len[ply + 1]; j++)
                    pv[ply][j + 1] = pv[ply + 1][j];
                pv_len[ply] = pv_len[ply + 1] + 1;
                if (score >= beta)
                    break;
            }
        }
    }
    return best;
}

void engine_search(const position_t *p, int depth, int (*stop)(void), engine_search_t *r)
{
    // The principal variation of each Even move of the root.
    static move_t even_pv[ENGINE_MAX_MOVES][ENGINE_MAX_DEPTH];
    static int even_len[ENGINE_MAX_MOVES];
    move_t even[ENGINE_MAX_MOVES];
    int n_even = 0;

    if (depth < 1)
        depth = 1;
    if (depth > ENGINE_MAX_DEPTH)
        depth = ENGINE_MAX_DEPTH;

    nodes = 0;
    stop_fn = stop;
    stopped = 0;
    stack_pos[0] = *p;
    r->move = 0;
    r->score = 0;
    r->depth = depth;
    r->pv_len = 0;
    r->stopped = 0;

    move_t *list = stack_moves[0];
    int n = engine_moves(p, list);
    nodes = 1;
    if (n == 0)
    {
        r->score = -ENGINE_WIN;
        r->nodes = nodes;
        return;
    }

    int best = -INF;
    for (int i = 0; i < n; i++)
    {
        int alpha = best > 0 ? best : best == 0 ? -1 : -INF;
        engine_make(p, list[i], &stack_pos[1]);
        int score = -negamax(1, depth - 1, -INF, -alpha);
        if (stopped)
            break;
        if (score <= alpha || (score != 0 && score <= best))
            continue; // Not better, nor another Even move

        // The score is exact, inside the window, and so is the
        // principal variation of the move.
        move_t *v;
        if (score == 0)
        {
            if (best < 0)
                n_even = 0;
            even_len[n_even] = pv_len[1] + 1;
            even[n_even] = list[i];
            v = even_pv[n_even++];
        }
        else
        {
            r->move = list[i];
            r->pv_len = pv_len[1] + 1;
            v = r->pv;
        }
        best = score;
        v[0] = list[i];
        for (int j = 0; j < pv_len[1]; j++)
            v[j + 1] = pv[1][j];
    }

    // A search that stopped plays what it has, or else the first move.
    r->stopped = stopped;
    if (best == -INF)
    {
        r->move = list[0];
        r->pv[0] = list[0];
        r->pv_len = 1;
        best = 0;
    }
    else if (best == 0)
    {
        int k = (int) rnd((uint32_t) n_even);
        r->move = even[k];
        r->pv_len = even_len[k];
        for (int j = 0; j < even_len[k]; j++)
            r->pv[j] = even_pv[k][j];
    }
    r->score = best;
    r->nodes = nodes;
}

char *engine_score_str(int score, char *s)
{
    const char *word = score > 0 ? "Win in " : score < 0 ? "Loss in " : "Even";
    int plies = score > 0 ? ENGINE_WIN - score : ENGINE_WIN + score;
    char *c = s;
    while (*word)
        *c++ = *word++;
    if (score != 0)
    {
        char digits[8];
        int k = 0;
        do
            digits[k++] = (char) ('0' + plies % 10);
        while ((plies /= 10) > 0);
        while (k > 0)
            *c++ = digits[--k];
    }
    *c = '\0';
    return s;
}
