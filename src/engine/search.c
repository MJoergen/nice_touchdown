/***************************************************************
 * search.c: negamax with alpha-beta pruning, to a fixed depth.
 *
 * A position has a value from the side to move: a Win (ENGINE_WIN
 * less the plies to it), a Loss (the negative of one), or, if neither
 * is proved within the depth, the evaluation of eval.c, or 0, Even,
 * without it. A leaf has that unless the game is over there. As every
 * move takes a pawn forward, a game has no repetitions and no draws,
 * and a Win or Loss found is exact.
 *
 * If it proves neither a Win nor a Loss, the opening book (book.c) may
 * know the value of the root, and the moves that win, of which it
 * plays one at random.
 *
 * The root searches its first move with a full window, and each other
 * with alpha at the best score so far, so that it learns only of a
 * better one: a faster win, or a slower loss. But while the best is
 * not proved, alpha is one less, so that a move as good scores the
 * same, exactly. Among the moves of the best score, when that is not
 * proved, it picks one at random.
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
static int eval_on;
static int (*stop_fn)(void);
static int stopped;

static uint32_t rnd_state = 1;
static int book_on = 1;

void engine_set_book(int on)
{
    book_on = on != 0;
}

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
        return eval_on ? engine_eval(&stack_pos[ply]) : 0;

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
    // The moves of the best score, if it is not proved, and the
    // principal variation of each.
    static move_t tie_pv[ENGINE_MAX_MOVES][ENGINE_MAX_DEPTH];
    static int tie_len[ENGINE_MAX_MOVES];
    move_t tie[ENGINE_MAX_MOVES];
    int n_tie = 0;

    if (depth < 1)
        depth = 1;
    if (depth > ENGINE_MAX_DEPTH)
        depth = ENGINE_MAX_DEPTH;

    nodes = 0;
    eval_on = engine_eval_on();
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
        int alpha = best == -INF || ENGINE_PROVED(best) ? best : best - 1;
        engine_make(p, list[i], &stack_pos[1]);
        int score = -negamax(1, depth - 1, -INF, -alpha);
        if (stopped)
            break;
        if (score <= alpha)
            continue; // Not better, nor as good

        // The score is exact, inside the window, and so is the
        // principal variation of the move. It is the best so far, or
        // as good as the best, which is not proved.
        if (score > best)
            n_tie = 0;
        best = score;
        move_t *v;
        if (!ENGINE_PROVED(score))
        {
            tie_len[n_tie] = pv_len[1] + 1;
            tie[n_tie] = list[i];
            v = tie_pv[n_tie++];
        }
        else
        {
            r->move = list[i];
            r->pv_len = pv_len[1] + 1;
            v = r->pv;
        }
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
    else if (!ENGINE_PROVED(best))
    {
        int k = (int) rnd((uint32_t) n_tie);
        r->move = tie[k];
        r->pv_len = tie_len[k];
        for (int j = 0; j < tie_len[k]; j++)
            r->pv[j] = tie_pv[k][j];
    }
    r->score = best;
    r->nodes = nodes;

    // The book, if the search proves neither a Win nor a Loss: one of
    // its moves that win, at random.
    const uint16_t *wins;
    int v, n_wins;
    if (book_on && !ENGINE_PROVED(best) && (v = book_probe(p, &wins, &n_wins)) >= 0)
    {
        r->score = v ? ENGINE_BOOK_WIN : -ENGINE_BOOK_WIN;
        if (v)
        {
            r->move = r->pv[0] = wins[n_wins > 1 ? rnd((uint32_t) n_wins) : 0];
            r->pv_len = 1;
        }
    }
}

// The probability that the side to move wins, in percent, of a score
// that is not proved, of PERCENT_MIN + PERCENT_STEP * i hundredths of
// a logit, from 1 to 99, as only a proved score is sure. The scores of
// the search are too careful by themselves: of the positions of games
// that the solver finds the value of, those of a score of 0 are a WIN
// for the side to move 61% of the time, as it has the move, and those
// of 0.5, which would be 62%, 77% of the time. The
// table is that probability, from the scores at depths 4 to 8 and the
// values: make calibrate (tools/calibrate.c and calibrate.py) fits it,
// and prints it.
#define PERCENT_MIN (-252)
#define PERCENT_STEP 4
static const uint8_t percent[127] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 4,
    4, 5, 5, 5, 6, 7, 8, 9, 11, 12, 14, 16, 18, 21, 25, 28, 32,
    36, 39, 41, 42, 45, 47, 50, 52, 54, 55, 57, 59, 61, 63, 65, 66, 67,
    68, 69, 70, 70, 71, 72, 73, 75, 77, 79, 81, 84, 85, 86, 88, 89, 91,
    92, 93, 94, 94, 95, 95, 95, 96, 97, 97, 98, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
};

// Writes n, of 0 to 99999, in decimal to c, and returns the end.
static char *put_number(char *c, int n)
{
    char digits[8];
    int k = 0;
    do
        digits[k++] = (char) ('0' + n % 10);
    while ((n /= 10) > 0);
    while (k > 0)
        *c++ = digits[--k];
    return c;
}

char *engine_score_str(int score, char *s)
{
    char *c = s;
    const char *word;
    if (score == ENGINE_BOOK_WIN || score == -ENGINE_BOOK_WIN)
    {
        word = score > 0 ? "Win" : "Loss";
        while (*word)
            *c++ = *word++;
    }
    else if (ENGINE_PROVED(score))
    {
        word = score > 0 ? "Win in " : "Loss in ";
        while (*word)
            *c++ = *word++;
        c = put_number(c, score > 0 ? ENGINE_WIN - score : ENGINE_WIN + score);
    }
    else if (!engine_eval_on())
    {
        word = "Even";
        while (*word)
            *c++ = *word++;
    }
    else
    {
        int i = (score - PERCENT_MIN + PERCENT_STEP / 2) / PERCENT_STEP;
        i = i < 0 ? 0 : i > 126 ? 126 : i;
        c = put_number(c, percent[i]);
        *c++ = '%';
    }
    *c = '\0';
    return s;
}
