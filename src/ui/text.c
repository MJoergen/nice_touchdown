/***************************************************************
 * text.c: the text front end, on the console.
 *
 * Commands, one per line:
 *   new [WxH]        a new game, on a board of W files and H ranks
 *                    (4x4 to 12x6), or of the size it has
 *   position TEXT    set up a position, such as "OOOO/..../..../XXXX x"
 *   d                show the board
 *   moves            the legal moves
 *   M, or move M     make a move, such as a1a2
 *   undo             take back the last move
 *   go               the engine moves
 *   perft N          count the paths of the move tree of depth N
 *   divide N         perft N, for each legal move
 *   help             list the commands
 *   quit             end the program
 *
 * A position as text has its ranks from the last down to rank 1,
 * divided by '/', with X for White's pawn, O for Black's, and . for
 * an empty square, then the side to move, x or o.
 *
 * go plays the first legal move until the engine has its search.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/engine.h"
#include "../platform/platform.h"
#include "version.h"

/***************************************************************
 * next_word
 * Returns the next word of *ps, which it ends with a 0, and moves
 * *ps past it. Returns "" at the end of the line.
 ***************************************************************/
static char *next_word(char **ps)
{
    char *s = *ps;
    while (*s == ' ' || *s == '\t')
        s++;
    char *w = s;
    while (*s && *s != ' ' && *s != '\t')
        s++;
    if (*s)
        *s++ = '\0';
    *ps = s;
    return w;
}

/***************************************************************
 * read_line
 * Reads a line of the console into line, of size bytes, without its
 * end. A line ends at LF or CR, as a terminal on the board's line
 * sends CR, and a file LF; an LF right after a CR is the same end. A
 * line longer than the buffer loses the rest. Returns 0 once the
 * input has ended with no line.
 ***************************************************************/
static int read_line(char *line, int size)
{
    static int last; // The byte before, to join CR LF
    int len = 0;
    while (1)
    {
        int c = platform_getc();
        if (c < 0)
        {
            line[len] = '\0';
            return len > 0;
        }
        int prev = last;
        last = c;
        if (c == '\n' && prev == '\r')
            continue;
        if (c == '\n' || c == '\r')
        {
            line[len] = '\0';
            return 1;
        }
        if (len < size - 1)
            line[len++] = (char) c;
    }
}

// A number of 1 to max, or 0 if s is not one.
static int number(const char *s, int max)
{
    char *end;
    long n = strtol(s, &end, 10);
    if (end == s || *end || n < 1 || n > max)
        return 0;
    return (int) n;
}

/***************************************************************
 * The commands
 ***************************************************************/

static void print_help(void)
{
    printf("Commands:\n"
           "  new [WxH]        a new game, on a board of W files and H ranks (4x4 to 12x6)\n"
           "  position TEXT    set up a position, such as OOOO/..../..../XXXX x\n"
           "  d                show the board\n"
           "  moves            the legal moves\n"
           "  M, or move M     make a move, such as a1a2\n"
           "  undo             take back the last move\n"
           "  go               the engine moves\n"
           "  perft N          count the paths of the move tree of depth N\n"
           "  divide N         perft N, for each legal move\n"
           "  help             list the commands\n"
           "  quit             end the program\n");
}

static void show_board(void)
{
    const position_t *p = engine_position();
    char s[ENGINE_POSITION_STR_SIZE];
    int files = engine_files(), ranks = engine_ranks();
    for (int r = ranks - 1; r >= 0; r--)
    {
        printf("  %d ", r + 1);
        for (int f = 0; f < files; f++)
        {
            int pawn = engine_pawn(p, f, r);
            printf(" %c", pawn == WHITE ? 'X' : pawn == BLACK ? 'O' : '.');
        }
        printf("\n");
    }
    printf("    ");
    for (int f = 0; f < files; f++)
        printf(" %c", 'a' + f);
    printf("\n");
    printf("%dx%d, %s to move\n", files, ranks, p->side == WHITE ? "White (X)" : "Black (O)");
    printf("position %s\n", engine_position_str(p, s));
}

// Prints the result of the game, if it has ended, and returns it.
static int print_result(void)
{
    int result = engine_result();
    if (result == ENGINE_WHITE_WINS)
        printf("White wins\n");
    else if (result == ENGINE_BLACK_WINS)
        printf("Black wins\n");
    return result;
}

static void do_new(char *arg)
{
    if (*arg)
    {
        int files, ranks;
        char *x = strchr(arg, 'x');
        if (!x)
        {
            printf("The size is WxH, such as 8x6\n");
            return;
        }
        *x = '\0';
        files = number(arg, 99);
        ranks = number(x + 1, 99);
        if (engine_set_size(files, ranks))
        {
            printf("The size is %dx%d to %dx%d\n", ENGINE_MIN_FILES, ENGINE_MIN_RANKS,
                   ENGINE_MAX_FILES, ENGINE_MAX_RANKS);
            return;
        }
    }
    engine_new_game();
}

static void do_position(const char *arg)
{
    if (engine_game_set_position(arg))
        printf("Not a position: %s\n", arg);
    else
        print_result();
}

static void do_moves(void)
{
    move_t list[ENGINE_MAX_MOVES];
    char s[5];
    int n = engine_moves(engine_position(), list);
    printf("moves");
    for (int i = 0; i < n; i++)
        printf(" %s", engine_move_str(list[i], s));
    printf("\n");
}

static void play(move_t m)
{
    char s[5];
    if (engine_play(m))
    {
        printf("The game is too long\n");
        return;
    }
    printf("move %s\n", engine_move_str(m, s));
    print_result();
}

static void do_move(const char *arg)
{
    if (engine_result() != ENGINE_PLAYING)
    {
        printf("The game is over\n");
        return;
    }
    move_t m = engine_parse_move(engine_position(), arg);
    if (!m)
        printf("Not a legal move: %s\n", arg);
    else
        play(m);
}

static void do_undo(void)
{
    if (engine_undo())
        printf("No move to take back\n");
}

static void do_go(void)
{
    move_t list[ENGINE_MAX_MOVES];
    if (!engine_moves(engine_position(), list))
    {
        printf("The game is over\n");
        return;
    }
    play(list[0]);
}

static void do_perft(const char *arg, int divide)
{
    int depth = number(arg, 20);
    if (!depth)
    {
        printf("The depth is 1 to 20\n");
        return;
    }
    const position_t *p = engine_position();
    if (!divide)
    {
        printf("perft %d: %llu\n", depth, (unsigned long long) engine_perft(p, depth));
        return;
    }
    move_t list[ENGINE_MAX_MOVES];
    char s[5];
    uint64_t sum = 0;
    int n = engine_moves(p, list);
    for (int i = 0; i < n; i++)
    {
        position_t q;
        engine_make(p, list[i], &q);
        uint64_t c = engine_perft(&q, depth - 1);
        sum += c;
        printf("%s: %llu\n", engine_move_str(list[i], s), (unsigned long long) c);
    }
    printf("divide %d: %llu\n", depth, (unsigned long long) sum);
}

int main(void)
{
    static char line[256];

    engine_new_game();
    printf("nice_touchdown %s. Type help for the commands.\n", nice_touchdown_version);

    while (1)
    {
        printf("> ");
        fflush(stdout);
        if (!read_line(line, sizeof line))
            break;

        char *arg = line;
        char *cmd = next_word(&arg);
        while (*arg == ' ' || *arg == '\t')
            arg++;

        if (*cmd == '\0')
            continue;
        else if (!strcmp(cmd, "quit"))
            break;
        else if (!strcmp(cmd, "help"))
            print_help();
        else if (!strcmp(cmd, "new"))
            do_new(arg);
        else if (!strcmp(cmd, "position"))
            do_position(arg);
        else if (!strcmp(cmd, "d"))
            show_board();
        else if (!strcmp(cmd, "moves"))
            do_moves();
        else if (!strcmp(cmd, "move"))
            do_move(arg);
        else if (!strcmp(cmd, "undo"))
            do_undo();
        else if (!strcmp(cmd, "go"))
            do_go();
        else if (!strcmp(cmd, "perft"))
            do_perft(arg, 0);
        else if (!strcmp(cmd, "divide"))
            do_perft(arg, 1);
        else if (strlen(cmd) == 4 && cmd[0] >= 'a' && cmd[0] <= 'l')
            do_move(cmd);
        else
            printf("Unknown command: %s. Type help for the commands.\n", cmd);
    }
    printf("\n");
    return 0;
}
