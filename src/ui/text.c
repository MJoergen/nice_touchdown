/***************************************************************
 * text.c: the text front end, on the console.
 *
 * Commands, one per line:
 *   help            list the commands
 *   quit            end the program
 *
 * This is the skeleton of Stage 0: the game's commands (new, moves,
 * d, perft, go) come with the engine.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <string.h>
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

static void print_help(void)
{
    printf("Commands:\n"
           "  help            list the commands\n"
           "  quit            end the program\n");
}

int main(void)
{
    static char line[256];

    printf("nice_touchdown %s. Type help for the commands.\n", nice_touchdown_version);

    while (1)
    {
        printf("> ");
        fflush(stdout);
        if (!read_line(line, sizeof line))
            break;

        char *arg = line;
        char *cmd = next_word(&arg);

        if (*cmd == '\0')
            continue;
        else if (!strcmp(cmd, "quit"))
            break;
        else if (!strcmp(cmd, "help"))
            print_help();
        else
            printf("Unknown command: %s. Type help for the commands.\n", cmd);
    }
    printf("\n");
    return 0;
}
