/***************************************************************
 * calibrate.c: the positions of games, with the scores of the search,
 * for tools/calibrate.py, which solves them and fits the probability
 * of a win that the engine shows (engine_score_str) to their values.
 *
 *   calibrate [OPTIONS]
 *
 * It plays --games games on each size of --sizes, each from 0 to 4
 * moves at random, then by the search to --depth. For each position
 * from ply --from on whose score at that depth is not proved, it
 * prints, once all the games of the size are played, a line of
 *
 *   SIZE <tab> POSITION <tab> PLY <tab> S1 <tab> S2 ... <tab> S8
 *
 * with the scores of the search to depths 1 to 8, in hundredths of a
 * logit, or P where the score is proved. The same options make the
 * same lines.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine/engine.h"

static uint64_t rng;

// xorshift64*.
static uint32_t rnd(void)
{
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return (uint32_t) ((rng * UINT64_C(0x2545F4914F6CDD1D)) >> 32);
}

static void usage(void)
{
    fprintf(stderr, "usage: calibrate [OPTIONS]\n"
                    "  --sizes LIST   the sizes, such as 8x6,6x6 (8x6,6x6,8x5,10x4,12x6)\n"
                    "  --games N      games a size (200)\n"
                    "  --depth D      the depth of the games (6)\n"
                    "  --from PLY     the first ply of the positions printed (8)\n"
                    "  --seed S       the seed (1)\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *sizes = "8x6,6x6,8x5,10x4,12x6";
    int games = 200, depth = 6, from = 8;
    uint32_t seed = 1;
    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!v)
            usage();
        if (!strcmp(a, "--sizes"))
            sizes = v;
        else if (!strcmp(a, "--games"))
            games = atoi(v);
        else if (!strcmp(a, "--depth"))
            depth = atoi(v);
        else if (!strcmp(a, "--from"))
            from = atoi(v);
        else if (!strcmp(a, "--seed"))
            seed = (uint32_t) atol(v);
        else
            usage();
        i++;
    }

    for (const char *s = sizes; *s;)
    {
        int f, r, len;
        if (sscanf(s, "%dx%d%n", &f, &r, &len) != 2 || engine_set_size(f, r))
            usage();
        s += len + (s[len] == ',');
        rng = (uint64_t) seed * UINT64_C(0x9e3779b97f4a7c15) + (uint64_t) (f * 16 + r);
        engine_seed(seed);
        // The positions, to score after the games, so that the games
        // are the same whatever is scored (the search's choice at
        // random among equal moves takes from the engine's seed).
        static position_t pos[1 << 20];
        static int pos_ply[1 << 20];
        int n_pos = 0;
        for (int g = 0; g < games; g++)
        {
            position_t p;
            move_t list[ENGINE_MAX_MOVES];
            engine_start(&p);
            int opening = (int) (rnd() % 5);
            for (int ply = 0;; ply++)
            {
                int n = engine_moves(&p, list);
                if (n == 0)
                    break;
                engine_search_t res;
                engine_search(&p, depth, 0, &res);
                if (ply >= from && !ENGINE_PROVED(res.score) && n_pos < (1 << 20))
                {
                    pos[n_pos] = p;
                    pos_ply[n_pos++] = ply;
                }
                engine_make(&p, ply < opening ? list[rnd() % (uint32_t) n] : res.move, &p);
            }
        }
        char text[ENGINE_POSITION_STR_SIZE];
        for (int i = 0; i < n_pos; i++)
        {
            printf("%dx%d\t%s\t%d", f, r, engine_position_str(&pos[i], text), pos_ply[i]);
            for (int d = 1; d <= 8; d++)
            {
                engine_search_t rd;
                engine_search(&pos[i], d, 0, &rd);
                if (ENGINE_PROVED(rd.score))
                    printf("\tP");
                else
                    printf("\t%d", rd.score);
            }
            printf("\n");
        }
    }
    return 0;
}
