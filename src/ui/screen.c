/***************************************************************
 * screen.c: the windowed front end, on the 80x25 character screen.
 *
 * The board takes the left half of the screen, columns 0 to 41, in its
 * middle: squares 3 rows high, and 5 columns wide up to 8 files, 3
 * above, with the ranks to its left and the files below it. The right
 * half has the title, the players and their clocks, the state of the
 * game, the size of the board and the depth of the search, the
 * engine's last search, the moves, the move being typed, and the keys.
 *
 * The keys:
 *   Arrows          move the cursor on the board
 *   Enter, Space    pick up the pawn under the cursor, or put it down
 *                   on a square it can go to
 *   a1a2            type a move, then Enter. Backspace (INST/DEL)
 *                   erases.
 *   Esc             drop the pawn or the typed move; while the engine
 *                   thinks, make it move now
 *   U               take back your last move (and the engine's reply)
 *   S               switch sides: the engine plays the side to move
 *   R               rotate the board
 *   N               a new game
 *   + -             the depth of the engine's search, 1 to 32
 *   W               the next width, 4 to 12 files, and a new game
 *   H               the next height, 4 to 6 ranks, and a new game
 *   Q               quit, and clear the screen
 * A lowercase letter that can start or go on with the move being typed
 * is a part of it: on a board of 8 files or more, h is the file h, and
 * Shift+H the next height. The other letters of commands act only when
 * no move is being typed.
 *
 * The cursor is two red brackets on a square 5 columns wide, and the
 * square's color, green, on one 3 columns wide. The pawn picked up is
 * green too, and the squares it can go to blue. The last move is
 * yellow.
 *
 * The pawns are glyphs of 3x3 characters, drawn by tools/mkpawns.py
 * (pawns.h): White's as an outline, and Black's filled, in one color on
 * the square's. The program copies the character ROM into the font RAM,
 * with Spleen's letters, writes the pawns over the codes 0x80 to 0x91,
 * and shows the font RAM (nice_cpu's SYSTEM.md §1.8). On Q it shows the
 * ROM again.
 *
 * Each move is also written to the console, as "move a1a2", the end of
 * the game as "result ...", and the position at the end as "position
 * ...", as the text front end writes them. The program ends on Q, or at
 * the end of the console's input, which leaves the screen as it is.
 *
 * Each side's clock counts up the time it has taken. In niceemu the
 * clocks count steps, at 54 million a second, the MEGA65's rate.
 *
 * The engine polls the keyboard during its search on a board, and in
 * niceemu only once a read of the console has found no byte waiting:
 * that is, once the input is a keyboard. Input from a file (niceemu -i,
 * in the tests) is then read only between moves, so a run is the same
 * every time. The keys typed while it thinks, other than Esc, wait for
 * its move.
 *
 * This front end is for NICE only. It does not use printf.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdint.h>
#include <nice.h>
#include "../engine/engine.h"
#include "../platform/platform.h"
#include "pawns.h"
#include "version.h"

/***************************************************************
 * Colors: a screen word has the background in bits 31..20, the
 * foreground in bits 19..8, and the character in bits 7..0.
 ***************************************************************/
#define COLOR(bg, fg) ((uint32_t) (bg) << 20 | (uint32_t) (fg) << 8)

enum
{
    SQ_LIGHT    = 0xCA7, SQ_DARK    = 0x863,  // The squares
    LAST_LIGHT  = 0xDC5, LAST_DARK  = 0xAA3,  // The last move
    SEL_LIGHT   = 0x7C7, SEL_DARK   = 0x5A5,  // The pawn picked up, the narrow cursor
    TGT_LIGHT   = 0x9BD, TGT_DARK   = 0x69B,  // Where it can go
    INK         = 0x111,                      // The pawns
    MARK        = 0xF11,                      // The cursor's brackets
    PANEL       = 0x123,                      // The background of the rest
    TEXT        = 0xCCC, BRIGHT = 0xFFF, DIM = 0x89A, WARN = 0xFA4
};

// The first column of the right half.
#define PX 42

// The rows of the right half.
enum
{
    ROW_TITLE = 0, ROW_PLAYERS = 1,           // Players: White, then Black
    ROW_STATUS = 3, ROW_SIZE = 4,
    ROW_INFO = 6, ROW_PV = 7,                 // PV: two rows
    ROW_MOVES = 10, MOVE_ROWS = 9,            // Two columns of 9 moves
    ROW_INPUT = 20, ROW_MSG = 21, ROW_HELP = 22 // Help: three rows
};

/***************************************************************
 * The keys
 ***************************************************************/
enum
{
    K_END = -1, K_NONE = -2,
    K_BS_CTRL = 8, K_ENTER = 13, K_ESC = 27, K_BS = 127,
    K_UP = 256, K_DOWN, K_RIGHT, K_LEFT, K_OTHER
};

/***************************************************************
 * The state of the front end
 ***************************************************************/
#define MAX_SQUARES (16 * ENGINE_MAX_RANKS)

static int    human = WHITE;       // The side the player has
static int    flipped;             // Black at the bottom
static int    cur_file, cur_rank;  // The cursor
static int    selected = -1;       // The square of the pawn picked up, or -1
static uint8_t targets[MAX_SQUARES]; // The squares it can go to
static char   typed[5];            // The move being typed
static int    n_typed;
static const char *message = "";   // A line for the last thing that went wrong
static char   msg_buf[40];
static int    depth = 8;           // Of the engine's search

// What the engine thought about its last move.
static engine_search_t info;
static int    have_info;
static int    thinking;

// The clocks: for White [0] and Black [1], the seconds taken, and the
// counts of platform_micros beyond them.
static uint32_t clock_secs[2], clock_rest[2];
static uint32_t clock_last;        // platform_micros at the last tick
static int    clock_side;          // The side the clock runs for
static int    clock_on;            // The game goes on, and so does the clock

/***************************************************************
 * Writing to the screen
 ***************************************************************/
static void put(int col, int row, uint32_t color, int c)
{
    nice_screen_put(col, row, (int) (color | (uint8_t) c));
}

// Writes s at col, row, and returns the column after it.
static int put_str(int col, int row, uint32_t color, const char *s)
{
    while (*s && col < 80)
        put(col++, row, color, *s++);
    return col;
}

// Fills a row of the right half from col to its end with blanks.
static void clear_to_end(int col, int row)
{
    while (col < 80)
        put(col++, row, COLOR(PANEL, TEXT), ' ');
}

// Writes the decimal digits of n to buf, and returns the end.
static char *fmt_uint(char *buf, uint32_t n)
{
    char tmp[10];
    int i = 0;
    do
        tmp[i++] = (char) ('0' + n % 10);
    while ((n /= 10) != 0);
    while (i > 0)
        *buf++ = tmp[--i];
    *buf = '\0';
    return buf;
}

static char *fmt_str(char *buf, const char *s)
{
    while (*s)
        *buf++ = *s++;
    *buf = '\0';
    return buf;
}

static void set_cursor(int show, int col, int row)
{
    nice_poke(0xFFFFFF81u, (unsigned) col);
    nice_poke(0xFFFFFF82u, (unsigned) row);
    nice_poke(0xFFFFFF80u, show ? 0x60u : 0u); // Shown, blinking
}

/***************************************************************
 * The font
 ***************************************************************/
#define FONT_RAM  0xFFFE0000u
#define FONT_ROM  0xFFFE2000u
#define FONT_BASE 0xFFFFFF86u
#define GLYPH_PAWNS 0x80           // The code of the first glyph of a pawn

// The ROM's glyphs, and the pawns over them from GLYPH_PAWNS on, into
// the font RAM, and the RAM shown.
static void font_init(void)
{
    for (unsigned i = 0; i < 256 * 32; ++i)
        nice_poke(FONT_RAM + i, nice_peek(FONT_ROM + i));
    for (unsigned g = 0; g < 2 * 9; ++g)
        for (unsigned r = 0; r < 32; ++r)
            nice_poke(FONT_RAM + 32 * (GLYPH_PAWNS + g) + r, pawn_glyphs[g][r]);
    nice_poke(FONT_BASE, FONT_RAM);
}

/***************************************************************
 * The board
 ***************************************************************/

// The width of a square, and the screen column and row of the top left
// cell of a square: the board in the middle of the left half.
static int sq_width(void)
{
    return engine_files() <= 8 ? 5 : 3;
}

static int sq_col(int file)
{
    int files = engine_files(), w = sq_width();
    int left = 1 + (40 - w * files) / 2;
    return left + w * (flipped ? files - 1 - file : file);
}

static int sq_row(int rank)
{
    int ranks = engine_ranks();
    int top = (24 - 3 * ranks) / 2;
    return top + 3 * (flipped ? rank : ranks - 1 - rank);
}

static void draw_square(int file, int rank)
{
    const position_t *p = engine_position();
    int sq = SQUARE(file, rank), w = sq_width();
    int light = (file + rank) & 1;
    int cursor = file == cur_file && rank == cur_rank && !thinking;
    int bg = light ? SQ_LIGHT : SQ_DARK;
    int plies = engine_game_plies();
    if (plies)
    {
        move_t last = engine_game_move(plies - 1);
        if (sq == MOVE_FROM(last) || sq == MOVE_TO(last))
            bg = light ? LAST_LIGHT : LAST_DARK;
    }
    if (sq == selected || (cursor && w == 3))
        bg = light ? SEL_LIGHT : SEL_DARK;
    else if (targets[sq])
        bg = light ? TGT_LIGHT : TGT_DARK;

    int col = sq_col(file), row = sq_row(rank);
    uint32_t blank = COLOR(bg, 0);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < w; ++c)
            put(col + c, row + r, blank, ' ');

    // A pawn is 3x3 glyphs, in the middle columns of the square.
    int pawn = engine_pawn(p, file, rank);
    if (pawn >= 0)
    {
        int g = GLYPH_PAWNS + 9 * pawn;
        uint32_t ink = COLOR(bg, INK);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                put(col + (w - 3) / 2 + c, row + r, ink, g++);
    }
    if (cursor && w == 5)
    {
        put(col, row + 1, COLOR(bg, MARK), '[');
        put(col + 4, row + 1, COLOR(bg, MARK), ']');
    }
}

static void draw_board(void)
{
    int files = engine_files(), ranks = engine_ranks();
    uint32_t label = COLOR(PANEL, DIM);
    for (int row = 0; row < 25; ++row)
        for (int col = 0; col < PX; ++col)
            put(col, row, label, ' ');
    for (int rank = 0; rank < ranks; ++rank)
    {
        put(sq_col(flipped ? files - 1 : 0) - 1, sq_row(rank) + 1, label, '1' + rank);
        for (int file = 0; file < files; ++file)
            draw_square(file, rank);
    }
    int below = sq_row(flipped ? ranks - 1 : 0) + 3;
    for (int file = 0; file < files; ++file)
        put(sq_col(file) + sq_width() / 2, below, label, 'a' + file);
}

/***************************************************************
 * The right half
 ***************************************************************/
static void draw_line(int row, uint32_t color, const char *s)
{
    clear_to_end(put_str(PX, row, color, s), row);
}

// A player and their clock: "White  you              0:12", the side
// whose clock runs bright.
static void draw_player(int side)
{
    char buf[40];
    char *p = fmt_str(buf, side ? "Black  " : "White  ");
    p = fmt_str(p, side == human ? "you" : "nice_touchdown");
    while (p < buf + 24)
        *p++ = ' ';
    uint32_t s = clock_secs[side];
    if (s >= 3600)
    {
        p = fmt_uint(p, s / 3600);
        *p++ = ':';
        *p++ = (char) ('0' + s / 600 % 6);
        *p++ = (char) ('0' + s / 60 % 10);
    }
    else
        p = fmt_uint(p, s / 60);
    *p++ = ':';
    *p++ = (char) ('0' + s / 10 % 6);
    *p++ = (char) ('0' + s % 10);
    *p = '\0';
    draw_line(ROW_PLAYERS + side, COLOR(PANEL, clock_on && side == clock_side ? BRIGHT : TEXT), buf);
}

static void draw_players(void)
{
    draw_player(WHITE);
    draw_player(BLACK);
}

// Charges the time since the last tick to the side the clock runs
// for, and draws its clock when a second has passed.
static void clock_tick(void)
{
    uint32_t now = platform_micros();
    uint32_t d = now - clock_last;
    clock_last = now;
    if (!clock_on)
        return;
    uint32_t second = platform_clock_second();
    uint32_t rest = clock_rest[clock_side] + d;
    if (rest >= second)
    {
        clock_secs[clock_side] += rest / second;
        rest %= second;
        clock_rest[clock_side] = rest;
        draw_player(clock_side);
        return;
    }
    clock_rest[clock_side] = rest;
}

// Charges the clock, then runs it for the side to move, if the game
// goes on. Called after each change to the game.
static void clock_switch(void)
{
    clock_tick();
    clock_side = engine_position()->side;
    clock_on = engine_result() == ENGINE_PLAYING;
}

// The state of the game: who is to move, or who won, and how.
static const char *status_text(void)
{
    const position_t *p = engine_position();
    int result = engine_result();
    if (result != ENGINE_PLAYING)
    {
        const uint32_t *loser = p->pawns[p->side];
        int white_won = result == ENGINE_WHITE_WINS;
        if (engine_touchdown(p))
            return white_won ? "Touchdown. White wins." : "Touchdown. Black wins.";
        if (!(loser[0] | loser[1] | loser[2]))
            return white_won ? "Black has no pawns. White wins." : "White has no pawns. Black wins.";
        return white_won ? "Black cannot move. White wins." : "White cannot move. Black wins.";
    }
    if (thinking)
        return "Thinking... Esc: move now";
    return p->side == WHITE ? "White to move." : "Black to move.";
}

static void draw_status(void)
{
    int over = engine_result() != ENGINE_PLAYING;
    draw_line(ROW_STATUS, COLOR(PANEL, over || thinking ? WARN : BRIGHT), status_text());
}

static void draw_size(void)
{
    char buf[40];
    char *p = fmt_uint(fmt_str(buf, "Board "), (uint32_t) engine_files());
    p = fmt_uint(fmt_str(p, "x"), (uint32_t) engine_ranks());
    fmt_uint(fmt_str(p, ", depth "), (uint32_t) depth);
    draw_line(ROW_SIZE, COLOR(PANEL, DIM), buf);
}

static void draw_info(void)
{
    char buf[48], s[16];
    char *p = buf;
    *p = '\0';
    if (have_info)
    {
        p = fmt_uint(fmt_str(p, "Depth "), (uint32_t) info.depth);
        p = fmt_str(fmt_str(p, "  Score "), engine_score_str(info.score, s));
        fmt_uint(fmt_str(p, "  Nodes "), info.nodes);
    }
    draw_line(ROW_INFO, COLOR(PANEL, TEXT), buf);

    // The principal variation, in as many moves as fit on two rows.
    int col = PX, row = ROW_PV;
    if (have_info)
        col = put_str(col, row, COLOR(PANEL, DIM), "PV");
    for (int i = 0; have_info && i < info.pv_len; ++i)
    {
        char m[5];
        engine_move_str(info.pv[i], m);
        if (col + 5 > 80)
        {
            clear_to_end(col, row);
            if (++row > ROW_PV + 1)
                break;
            col = PX + 2;
        }
        col = put_str(put_str(col, row, COLOR(PANEL, DIM), " "), row, COLOR(PANEL, DIM), m);
    }
    for (; row <= ROW_PV + 1; ++row, col = PX)
        clear_to_end(col, row);
}

// Two columns of moves: "12. a1a2  b6b5". The last ones that fit.
static void draw_moves(void)
{
    int plies = engine_game_plies();
    draw_line(ROW_MOVES - 1, COLOR(PANEL, DIM), "Moves");
    int full = (plies + 1) / 2;                   // Full moves, the last maybe half
    int first = full > 2 * MOVE_ROWS ? full - 2 * MOVE_ROWS : 0;
    for (int row = 0; row < MOVE_ROWS; ++row)
        clear_to_end(PX, ROW_MOVES + row);
    for (int k = first; k < full; ++k)
    {
        int slot = k - first;
        int col = PX + (slot / MOVE_ROWS) * 19;
        int row = ROW_MOVES + slot % MOVE_ROWS;
        char buf[20];
        fmt_str(fmt_uint(buf, (uint32_t) k + 1), ". ");
        int c = col + (k + 1 < 10 ? 2 : k + 1 < 100 ? 1 : 0);
        c = put_str(c, row, COLOR(PANEL, DIM), buf);
        for (int h = 0; h < 2 && 2 * k + h < plies; ++h)
        {
            char m[5];
            put_str(c + 6 * h, row, COLOR(PANEL, TEXT), engine_move_str(engine_game_move(2 * k + h), m));
        }
    }
}

static void draw_input(void)
{
    int col = put_str(PX, ROW_INPUT, COLOR(PANEL, DIM), "Your move: ");
    typed[n_typed] = '\0';
    int end = put_str(col, ROW_INPUT, COLOR(PANEL, BRIGHT), typed);
    clear_to_end(end, ROW_INPUT);
    set_cursor(!thinking, end, ROW_INPUT);
    draw_line(ROW_MSG, COLOR(PANEL, WARN), message);
}

static void draw_all(void)
{
    draw_board();
    int col = put_str(PX, ROW_TITLE, COLOR(PANEL, BRIGHT), "nice_touchdown ");
    clear_to_end(put_str(col, ROW_TITLE, COLOR(PANEL, BRIGHT), nice_touchdown_version), ROW_TITLE);
    draw_players();
    draw_status();
    draw_size();
    draw_line(ROW_SIZE + 1, COLOR(PANEL, TEXT), "");
    draw_info();
    draw_moves();
    draw_line(ROW_INPUT - 1, COLOR(PANEL, TEXT), "");
    draw_input();
    draw_line(ROW_HELP,     COLOR(PANEL, DIM), "Arrows+Enter or a1a2: move  Q quit");
    draw_line(ROW_HELP + 1, COLOR(PANEL, DIM), "U undo  S switch  R rotate  N new");
    draw_line(ROW_HELP + 2, COLOR(PANEL, DIM), "+/- depth  W width  H height");
}

/***************************************************************
 * The console: the keys, and the log of the moves
 ***************************************************************/
static int pushed_back = K_NONE;
static int live;                   // The input is a keyboard (see the top)

// The log goes to the console's register itself, not through
// nice_putc: under the monitor on the board, nice_putc prints on the
// screen too, where the board is (NiceMonMEGA65's monitor/README.md).
static void console_putc(int c)
{
    nice_poke(NICE_CONSOLE, (unsigned) c);
}

static void console_print(const char *s)
{
    while (*s)
        console_putc(*s++);
}

static int read_byte(void)
{
    int c = pushed_back;
    if (c != K_NONE)
    {
        pushed_back = K_NONE;
        return c;
    }
    c = platform_pollc();
    if (c == K_NONE)
        live = 1;
    return c;
}

// The byte after an ESC: the rest of a key's bytes may still be on
// their way from a keyboard, so this waits up to 20 ms for one.
static int read_byte_soon(void)
{
    uint32_t t0 = platform_micros();
    int c;
    while ((c = read_byte()) == K_NONE && platform_micros() - t0 < 20000)
        ;
    return c;
}

// The next key, K_NONE if none waits, or K_END.
static int poll_key(void)
{
    int c = read_byte();
    if (c == '\n')
        return K_ENTER;
    if (c != K_ESC)
        return c;

    // Esc alone, or the start of the bytes of a key: ESC [ A for Up,
    // ESC [ 1 5 ~ for F5, ESC O P for F1.
    int d = read_byte_soon();
    if (d != '[' && d != 'O')
    {
        pushed_back = d;
        return K_ESC;
    }
    int e = read_byte_soon();
    switch (e)
    {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        default: break;
    }
    while (e >= '0' && e <= '9')
        e = read_byte_soon();
    return K_OTHER;
}

// The keys typed while the engine thought, for after its move.
#define AHEAD 16
static int ahead[AHEAD];
static int n_ahead;

static int wait_key(void)
{
    if (n_ahead)
    {
        int k = ahead[0];
        for (int i = 1; i < n_ahead; ++i)
            ahead[i - 1] = ahead[i];
        --n_ahead;
        return k;
    }
    int k;
    while ((k = poll_key()) == K_NONE)
        clock_tick();
    return k;
}

/***************************************************************
 * The game
 ***************************************************************/
static void clear_selection(void)
{
    selected = -1;
    for (int i = 0; i < MAX_SQUARES; ++i)
        targets[i] = 0;
}

// Writes the move, and the result of the game if it has ended, to the
// console.
static void log_move(move_t m)
{
    char buf[5];
    console_print("move ");
    console_print(engine_move_str(m, buf));
    console_putc('\n');
    if (engine_result() != ENGINE_PLAYING)
    {
        console_print("result ");
        console_print(status_text());
        console_putc('\n');
    }
}

static void play(move_t m)
{
    clock_tick();
    if (engine_play(m))
    {
        message = "The game is too long.";
        return;
    }
    log_move(m);
}

// Puts the cursor on the board, after a change of its size.
static void clamp_cursor(void)
{
    if (cur_file >= engine_files())
        cur_file = engine_files() - 1;
    if (cur_rank >= engine_ranks())
        cur_rank = engine_ranks() - 1;
}

static void new_game(void)
{
    engine_new_game();
    for (int i = 0; i < 2; ++i)
        clock_secs[i] = clock_rest[i] = 0;
    clock_on = 0;
    clock_last = platform_micros();
    have_info = 0;
    n_typed = 0;
    clear_selection();
    clamp_cursor();
}

// Takes back moves until it is the player's turn again, at least one.
static void undo(void)
{
    if (engine_game_plies() == 0)
    {
        message = "Nothing to take back.";
        return;
    }
    do
        engine_undo();
    while (engine_game_plies() > 0 && engine_position()->side != human);
    have_info = 0;
    clear_selection();
}

/***************************************************************
 * The engine's move
 ***************************************************************/

// Called by the search every 1024 nodes: Esc stops it. The other keys
// wait in ahead for after its move.
static int stop_search(void)
{
    clock_tick();
    if (!live)
        return 0;
    int k = poll_key();
    if (k == K_ESC)
        return 1;
    if (k != K_NONE && k != K_END && n_ahead < AHEAD)
        ahead[n_ahead++] = k;
    return 0;
}

static void think(void)
{
    clear_selection();
    thinking = 1;
    draw_all();
    engine_search(engine_position(), depth, stop_search, &info);
    have_info = 1;
    thinking = 0;
    if (info.move)
        play(info.move);
}

/***************************************************************
 * The keys
 ***************************************************************/

// Whether the side to move is the player's, in a game that goes on.
static int players_turn(void)
{
    return engine_result() == ENGINE_PLAYING && engine_position()->side == human;
}

// Makes the typed move, if it is legal.
static void enter_typed(void)
{
    typed[n_typed] = '\0';
    move_t m = players_turn() ? engine_parse_move(engine_position(), typed) : 0;
    if (m)
        play(m);
    else
    {
        fmt_str(fmt_str(msg_buf, "Not a legal move: "), typed);
        message = msg_buf;
    }
    n_typed = 0;
}

// Enter on the board: pick up the pawn under the cursor, or put the
// one picked up on the cursor's square.
static void enter_square(void)
{
    int sq = SQUARE(cur_file, cur_rank);
    if (!players_turn())
        return;

    if (selected >= 0 && targets[sq])
    {
        int from = selected;
        clear_selection();
        play(MOVE(from, sq));
        return;
    }
    if (sq == selected)
    {
        clear_selection();
        return;
    }
    clear_selection();
    move_t list[ENGINE_MAX_MOVES];
    int n = engine_moves(engine_position(), list);
    for (int i = 0; i < n; ++i)
        if (MOVE_FROM(list[i]) == sq)
        {
            selected = sq;
            targets[MOVE_TO(list[i])] = 1;
        }
    if (selected < 0)
        message = "No move from there.";
}

static void move_cursor(int k)
{
    int up = flipped ? -1 : 1;
    switch (k)
    {
        case K_UP:    cur_rank += up; break;
        case K_DOWN:  cur_rank -= up; break;
        case K_RIGHT: cur_file += up; break;
        case K_LEFT:  cur_file -= up; break;
        default: break;
    }
    if (cur_rank < 0) cur_rank = 0;
    if (cur_file < 0) cur_file = 0;
    clamp_cursor();
}

// Whether c can come next in the move being typed.
static int fits_typed(int c)
{
    switch (n_typed)
    {
        case 0: case 2: return c >= 'a' && c < 'a' + engine_files();
        case 1: case 3: return c >= '1' && c < '1' + engine_ranks();
        default:        return 0;
    }
}

// Handles a key. Returns 0 to quit.
static int handle_key(int k)
{
    message = "";
    if (k >= K_UP && k <= K_LEFT)
        move_cursor(k);
    else if (k == K_ESC)
    {
        n_typed = 0;
        clear_selection();
    }
    else if (k == K_BS || k == K_BS_CTRL)
    {
        if (n_typed)
            --n_typed;
    }
    else if (k == K_ENTER || k == ' ')
    {
        if (n_typed == 4)
            enter_typed();
        else if (n_typed)
            message = "Type a move such as a1a2.";
        else
            enter_square();
    }
    else if (fits_typed(k))
    {
        typed[n_typed++] = (char) k;
        clear_selection();
    }
    else if (n_typed)
        message = "Type a move such as a1a2.";
    else switch (k >= 'A' && k <= 'Z' ? k + 'a' - 'A' : k)
    {
        case 'q':
            // Leave a blank screen, for the monitor on the board, in
            // its colors, white on black, as nice_screen_clear writes
            // them. The end of the input keeps the screen instead, for
            // the tests.
            nice_poke(FONT_BASE, 0);
            nice_screen_clear();
            return 0;
        case 'u': undo(); break;
        case 'n': new_game(); break;
        case 'r': flipped = !flipped; break;
        case 's':
            human ^= 1;
            flipped = human == BLACK;
            clear_selection();
            break;
        case '+': if (depth < ENGINE_MAX_DEPTH) ++depth; break;
        case '-': if (depth > 1) --depth; break;
        case 'w':
            engine_set_size(engine_files() == ENGINE_MAX_FILES ? ENGINE_MIN_FILES : engine_files() + 1,
                            engine_ranks());
            new_game();
            break;
        case 'h':
            engine_set_size(engine_files(),
                            engine_ranks() == ENGINE_MAX_RANKS ? ENGINE_MIN_RANKS : engine_ranks() + 1);
            new_game();
            break;
        default: break;
    }
    return 1;
}

int main(void)
{
    live = platform_on_board();
    engine_seed(platform_seed());
    engine_set_size(8, 6);
    cur_file = 3;
    cur_rank = 0;
    font_init();
    new_game();

    while (1)
    {
        clock_switch();
        draw_all();
        if (engine_result() == ENGINE_PLAYING && engine_position()->side != human)
        {
            think();
            continue;
        }
        int k = wait_key();
        if (k == K_END || !handle_key(k))
            break;
    }
    set_cursor(0, 0, 0);
    char pos[ENGINE_POSITION_STR_SIZE];
    console_print("position ");
    console_print(engine_position_str(engine_position(), pos));
    console_putc('\n');
    return 0;
}
