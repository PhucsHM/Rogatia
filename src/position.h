// Rogatia chess engine -- board representation.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_POSITION_H
#define ROGATIA_POSITION_H

#include <string>
#include <vector>

#include "attacks.h"
#include "bitboard.h"
#include "types.h"
#include "zobrist.h"

namespace rogatia {

// Everything about a position that a move destroys and cannot be rebuilt from
// the board alone.  make_move pushes a copy, unmake_move pops it back --
// nothing here is ever recomputed on the way out.
struct BoardState {
    int      castlingRights = NO_CASTLING;
    Square   epSquare       = SQ_NONE;
    int      rule50         = 0;
    Piece    captured       = NO_PIECE;
    // Plies since the last null move.  A null move is not a legal continuation,
    // so nothing before one can be repeated by any sequence of real moves --
    // repetitions() must not look past it.
    int      pliesFromNull  = 0;

    Key key       = 0;
    Key pawnKey   = 0;
    Key majorKey  = 0;
    Key minorKey  = 0;
    Key nonPawnKey[COLOR_NB] = {0, 0};

    Bitboard checkers    = 0;
    Bitboard blockers[COLOR_NB] = {0, 0};  // pieces (either colour) shielding king of [c]
    Bitboard pinners[COLOR_NB]  = {0, 0};  // enemy sliders doing the pinning against [c]
};

class Position {
public:
    Position() { clear(); }

    // ------------------------------------------------------------- setup --
    void clear();
    // Returns false and loads the start position if `fen` is malformed or
    // describes an illegal board.  UCI feeds arbitrary user input here, so a
    // rejected FEN must leave a usable position rather than a kingless one.
    bool set(const std::string& fen);
    std::string fen() const;

    // ------------------------------------------------------------ pieces --
    Piece    piece_on(Square s) const { return board_[s]; }
    bool     empty(Square s) const { return board_[s] == NO_PIECE; }
    Bitboard pieces() const { return byColor_[WHITE] | byColor_[BLACK]; }
    Bitboard pieces(Color c) const { return byColor_[c]; }
    Bitboard pieces(PieceType pt) const { return byType_[pt]; }
    Bitboard pieces(PieceType a, PieceType b) const { return byType_[a] | byType_[b]; }
    Bitboard pieces(Color c, PieceType pt) const { return byColor_[c] & byType_[pt]; }
    Bitboard pieces(Color c, PieceType a, PieceType b) const {
        return byColor_[c] & (byType_[a] | byType_[b]);
    }
    Square   king_square(Color c) const { return lsb(pieces(c, KING)); }
    int      count(Color c, PieceType pt) const { return popcount(pieces(c, pt)); }

    // ------------------------------------------------------------- state --
    Color  side_to_move() const { return sideToMove_; }
    int    castling_rights() const { return st_.castlingRights; }
    bool   can_castle(int cr) const { return (st_.castlingRights & cr) != 0; }
    Square ep_square() const { return st_.epSquare; }
    int    rule50_count() const { return st_.rule50; }
    int    game_ply() const { return gamePly_; }

    Key key() const { return st_.key; }
    Key pawn_key() const { return st_.pawnKey; }
    Key major_key() const { return st_.majorKey; }
    Key minor_key() const { return st_.minorKey; }
    Key non_pawn_key(Color c) const { return st_.nonPawnKey[c]; }

    Bitboard checkers() const { return st_.checkers; }
    // Side to move only, and deliberately not parameterised by colour.
    // compute_checkers_and_blockers() stops after one pass, so the other
    // colour's entry holds the PREVIOUS ply's values.  A `Color` parameter here
    // would be correct for one argument and silently wrong for the other, which
    // is a landmine rather than an API -- and one Phase 7 walks straight past,
    // since a gives_check() fast path for check extensions is exactly the
    // "optimisation" that would reach for it.  Restoring the second pass is the
    // price of getting the parameter back.
    Bitboard blockers_for_king() const { return st_.blockers[sideToMove_]; }
    Bitboard pinned() const { return st_.blockers[sideToMove_] & byColor_[sideToMove_]; }
    bool     in_check() const { return st_.checkers != 0; }

    // ------------------------------------------------------------ queries --
    Bitboard attackers_to(Square s, Bitboard occ) const;
    Bitboard attackers_to(Square s) const { return attackers_to(s, pieces()); }
    bool     is_attacked(Square s, Color by, Bitboard occ) const;

    // Validates a bare 16-bit move against this position from scratch.  Unlike
    // is_legal it assumes NOTHING about where the move came from, so a TT move
    // or killer probed from a different position is safe to pass here.
    bool is_pseudo_legal(Move m) const;
    // Assumes m came out of generate() (or passed is_pseudo_legal): only checks
    // that our own king is not left in check.
    bool is_legal(Move m) const;
    bool gives_check(Move m) const;
    bool is_capture(Move m) const {
        return (!empty(to_sq(m)) && type_of(m) != CASTLING) || type_of(m) == EN_PASSANT;
    }

    // 50-move rule plus a SINGLE repetition.  Treating the first repetition as
    // a draw prunes whole subtrees and is worth Elo, but it is not the rule of
    // chess -- never use this to decide a game result, only inside search.
    bool is_draw_for_search() const;
    // The actual rule: three occurrences of the position, or 50 moves.
    bool is_game_draw() const;

    // ------------------------------------------------------------- moving --
    void make_move(Move m);
    void unmake_move(Move m);
    // Pass the turn.  Illegal in chess, used by null-move pruning; the caller
    // is responsible for never doing it while in check.
    void make_null_move();
    void unmake_null_move();

    // -------------------------------------------------------------- debug --
    std::string to_string() const;
    bool        pos_is_ok() const;

private:
    bool parse_fen(const std::string& fen);
    // Number of earlier occurrences of the current position within the 50-move
    // window.  Stops early once `stopAt` have been found.
    int  repetitions(int stopAt) const;
    void put_piece(Piece pc, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    void update_keys(Piece pc, Square s);   // XOR pc@s into every key set
    void set_state();                       // recompute checkers / blockers / keys
    void compute_checkers_and_blockers();
    Bitboard slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const;

    Bitboard byColor_[COLOR_NB];
    Bitboard byType_[PIECE_TYPE_NB];
    Piece    board_[SQUARE_NB];

    Color sideToMove_;
    int   gamePly_;

    // AND mask applied to castling rights for the from- and to-square of every
    // move.  One table covers king moves, rook moves and rook captures.
    int castlingMask_[SQUARE_NB];

    BoardState              st_;
    std::vector<BoardState> history_;
};

// Initialises bitboard geometry, attack tables and Zobrist keys.
// Must be called once before any Position is used.
void init();

}  // namespace rogatia

#endif  // ROGATIA_POSITION_H
