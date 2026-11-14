/***************************************************************
 * notation.c: moves and positions as text.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <string.h>
#include "internal.h"

char *engine_move_str(move_t m, char *s)
{
    int from = MOVE_FROM(m), to = MOVE_TO(m);
    s[0] = (char) ('a' + SQUARE_FILE(from));
    s[1] = (char) ('1' + SQUARE_RANK(from));
    s[2] = (char) ('a' + SQUARE_FILE(to));
    s[3] = (char) ('1' + SQUARE_RANK(to));
    s[4] = '\0';
    return s;
}

move_t engine_parse_move(const position_t *p, const char *s)
{
    move_t list[ENGINE_MAX_MOVES];
    char t[5];
    if (strlen(s) != 4)
        return 0;
    int n = engine_moves(p, list);
    for (int i = 0; i < n; i++)
        if (!strcmp(engine_move_str(list[i], t), s))
            return list[i];
    return 0;
}

int engine_set_position(position_t *p, const char *s)
{
    // The size first: the ranks are divided by '/', and end at the
    // space before the side to move.
    int files = (int) strcspn(s, "/ ");
    int ranks = 1;
    for (const char *c = s; *c && *c != ' '; c++)
        ranks += *c == '/';
    if (files < ENGINE_MIN_FILES || files > ENGINE_MAX_FILES
        || ranks < ENGINE_MIN_RANKS || ranks > ENGINE_MAX_RANKS)
        return -1;

    position_t q = {{{0}}, WHITE};
    for (int r = ranks - 1; r >= 0; r--)
    {
        for (int f = 0; f < files; f++, s++)
        {
            int sq = SQUARE(f, r);
            if (*s == 'X')
                q.pawns[WHITE][sq >> 5] |= 1u << (sq & 31);
            else if (*s == 'O')
                q.pawns[BLACK][sq >> 5] |= 1u << (sq & 31);
            else if (*s != '.')
                return -1;
        }
        if (*s++ != (r ? '/' : ' '))
            return -1;
    }
    if ((s[0] != 'x' && s[0] != 'o') || s[1] != '\0')
        return -1;
    q.side = s[0] == 'x' ? WHITE : BLACK;

    engine_set_size(files, ranks);
    *p = q;
    return 0;
}

char *engine_position_str(const position_t *p, char *s)
{
    char *c = s;
    for (int r = board_ranks - 1; r >= 0; r--)
    {
        for (int f = 0; f < board_files; f++)
        {
            int pawn = engine_pawn(p, f, r);
            *c++ = pawn == WHITE ? 'X' : pawn == BLACK ? 'O' : '.';
        }
        *c++ = r ? '/' : ' ';
    }
    *c++ = p->side == WHITE ? 'x' : 'o';
    *c = '\0';
    return s;
}
