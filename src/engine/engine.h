/***************************************************************
 * engine.h: the engine's interface.
 *
 * The engine does no I/O, reads no clock, and calls no malloc, so the
 * same files build for NICE and for the host.
 *
 * The board has 4 to 12 files and 4 to 6 ranks, set by
 * engine_set_size, for every position until the next call. A square
 * is rank * 16 + file, from 0 (a1) up: rank r of a side's pawns is
 * bits 16 * r to 16 * r + 11 of a bitboard of 96 bits, kept in three
 * 32-bit words, two ranks to a word. The bits of files and ranks
 * beyond the board are always 0.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>

#define ENGINE_MIN_FILES 4
#define ENGINE_MAX_FILES 12
#define ENGINE_MIN_RANKS 4
#define ENGINE_MAX_RANKS 6

// The most moves of a position: each of at most 12 pawns of a side
// moves forward or takes to either side.
#define ENGINE_MAX_MOVES (3 * ENGINE_MAX_FILES)

// The most plies of a game: each pawn moves forward at most
// ranks - 1 times, and the game ends before that.
#define ENGINE_MAX_PLIES (2 * ENGINE_MAX_FILES * (ENGINE_MAX_RANKS - 1))

enum { WHITE = 0, BLACK = 1 };

typedef struct
{
    uint32_t pawns[2][3]; // [WHITE] and [BLACK], ranks 2i and 2i + 1 in word i
    int side;             // WHITE or BLACK, the side to move
} position_t;

// A move: the square it is from in the low byte, and the square it is
// to in the high byte. 0 is no move.
typedef uint16_t move_t;

#define MOVE(from, to) ((move_t) ((from) | (to) << 8))
#define MOVE_FROM(m)   ((m) & 0xff)
#define MOVE_TO(m)     ((m) >> 8)
#define SQUARE(file, rank) ((rank) * 16 + (file))
#define SQUARE_FILE(sq)    ((sq) & 15)
#define SQUARE_RANK(sq)    ((sq) >> 4)

/***************************************************************
 * board.c
 ***************************************************************/

// Sets the size of the board. Returns 0, or -1 if it is out of range,
// when the size stays as it was. The size is 8x6 at first.
int engine_set_size(int files, int ranks);
int engine_files(void);
int engine_ranks(void);

// The position at the start: White's pawns on rank 1, Black's on the
// last, and White to move.
void engine_start(position_t *p);

// The pawn on a square: WHITE, BLACK, or -1 if it is empty.
int engine_pawn(const position_t *p, int file, int rank);

/***************************************************************
 * movegen.c
 ***************************************************************/

// Writes the legal moves of the side to move to list, and returns how
// many. It is 0 when the game is over: when a pawn of the opponent is
// on the side's back rank, or the side has no move, which includes
// having no pawns. In both cases the side to move has lost.
//
// The moves come in this order: those to the last rank, then the
// other captures, then the other moves forward.
int engine_moves(const position_t *p, move_t *list);

// Whether the side to move has lost already, as a pawn of the opponent
// is on its back rank. engine_moves returns 0 then too; this is the
// cheaper test.
int engine_touchdown(const position_t *p);

/***************************************************************
 * makemove.c
 ***************************************************************/

// Writes to out the position after the move m, one of
// engine_moves(p). out may be p.
void engine_make(const position_t *p, move_t m, position_t *out);

/***************************************************************
 * notation.c
 ***************************************************************/

// Writes the move as a string such as "b1b2" to s, of at least 5
// bytes, and returns s.
char *engine_move_str(move_t m, char *s);

// The legal move of p that the string names, such as "b1b2", or 0.
move_t engine_parse_move(const position_t *p, const char *s);

// A position as text: its ranks from the last down to rank 1, divided
// by '/', each a square to a character from file a on, X for White's
// pawn, O for Black's, and . for empty (touchdown_db's letters), then
// a space and the side to move, x or o. The start of 4x4 is
// "OOOO/..../..../XXXX x".
//
// engine_set_position reads it, and sets the size of the board from
// it. It returns 0, or -1 if the text is not a position, when the
// size and *p stay as they were.
int engine_set_position(position_t *p, const char *s);

// Writes p as such text to s, of at least
// (ENGINE_MAX_FILES + 1) * ENGINE_MAX_RANKS + 2 bytes, and returns s.
char *engine_position_str(const position_t *p, char *s);
#define ENGINE_POSITION_STR_SIZE ((ENGINE_MAX_FILES + 1) * ENGINE_MAX_RANKS + 2)

/***************************************************************
 * game.c: the game, as the front ends see it.
 ***************************************************************/

// The most moves of a game the engine keeps. A game from the start
// has at most ENGINE_MAX_PLIES; one from a position set up with more
// pawns could have more, and engine_play refuses those beyond.
#define ENGINE_MAX_GAME (2 * ENGINE_MAX_PLIES)

enum { ENGINE_PLAYING, ENGINE_WHITE_WINS, ENGINE_BLACK_WINS };

// A new game, from the start, at the size of the board.
void engine_new_game(void);

// A new game from a position as text (engine_set_position), which
// sets the size too. Returns 0, or -1 if the text is not a position,
// when the game stays as it was.
int engine_game_set_position(const char *s);

// The current position of the game.
const position_t *engine_position(void);

// The moves of the game so far, and the i-th of them, from 0.
int engine_game_plies(void);
move_t engine_game_move(int i);

// Makes the move m, one of engine_moves(engine_position()). Returns 0,
// or -1 if the game is too long, when it is not made.
int engine_play(move_t m);

// Takes back the last move. Returns 0, or -1 if there is none.
int engine_undo(void);

// ENGINE_PLAYING, or who has won.
int engine_result(void);

/***************************************************************
 * search.c: negamax with alpha-beta pruning, to a fixed depth.
 ***************************************************************/

// The deepest search, in plies.
#define ENGINE_MAX_DEPTH 32

// A score, from the side to move: ENGINE_WIN - N is a win in N plies,
// -(ENGINE_WIN - N) a loss in N plies, and 0 Even: neither is proved
// within the depth.
#define ENGINE_WIN 10000

typedef struct
{
    move_t move;                   // The move to play; 0 if the game is over
    int score;                     // Of the move
    int depth;                     // Searched
    uint32_t nodes;                // Searched
    move_t pv[ENGINE_MAX_DEPTH];   // The principal variation, from move
    int pv_len;
    int stopped;                   // Whether stop ended it early
} engine_search_t;

// The seed of the choice at random among the Even moves.
void engine_seed(uint32_t seed);

// Searches p to depth plies, 1 to ENGINE_MAX_DEPTH, and writes the
// result to r. It calls stop, if not 0, every 1024 nodes, and ends
// early when it returns nonzero: then r has the best move of the root
// moves searched, or the first move.
void engine_search(const position_t *p, int depth, int (*stop)(void), engine_search_t *r);

// Writes a score as "Even", "Win in N", or "Loss in N" (N in plies)
// to s, of at least 16 bytes, and returns s.
char *engine_score_str(int score, char *s);

/***************************************************************
 * perft.c
 ***************************************************************/

// The number of the move tree's paths of depth plies, from p. A path
// that ends early, as the game ends, does not count.
uint64_t engine_perft(const position_t *p, int depth);

#endif // ENGINE_H
