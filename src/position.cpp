// Rogatia chess engine -- board representation.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "position.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace rogatia {

namespace {

constexpr char PieceChars[PIECE_NB + 1] = "-PNBRQK  pnbrqk";

Piece piece_from_char(char c) {
    for (int p = 0; p < PIECE_NB; ++p)
        if (PieceChars[p] == c && c != '-' && c != ' ')
            return Piece(p);
    return NO_PIECE;
}

constexpr const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

}  // namespace

void init() {
    init_bitboards();
    init_attacks();
    init_zobrist();
}

// ----------------------------------------------------------------- setup ---

void Position::clear() {
    for (int c = 0; c < COLOR_NB; ++c)
        byColor_[c] = 0;
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        byType_[pt] = 0;
    for (int s = 0; s < SQUARE_NB; ++s) {
        board_[s]        = NO_PIECE;
        castlingMask_[s] = ANY_CASTLING;
    }
    sideToMove_ = WHITE;
    gamePly_    = 0;
    st_         = BoardState{};
    history_.clear();
    history_.reserve(MAX_PLY + 64);
}

bool Position::set(const std::string& fen) {
    if (parse_fen(fen))
        return true;
    // StartFEN is known good, so this cannot recurse.  Callers that care get
    // `false`; callers that do not still get a position that is safe to use.
    parse_fen(StartFEN);
    return false;
}

bool Position::parse_fen(const std::string& fen) {
    clear();

    std::istringstream ss(fen);
    std::string        boardPart, stmPart, castlePart, epPart;
    int                halfmove = 0, fullmove = 1;

    if (!(ss >> boardPart >> stmPart >> castlePart >> epPart))
        return false;
    if (!(ss >> halfmove))
        halfmove = 0;
    if (!(ss >> fullmove))
        fullmove = 1;

    int file = FILE_A, rank = RANK_8;
    for (char c : boardPart) {
        if (c == '/') {
            if (file != FILE_NB || rank == RANK_1)
                return false;
            --rank;
            file = FILE_A;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > FILE_NB)
                return false;
        } else {
            const Piece pc = piece_from_char(c);
            // Guard the square before touching it: put_piece on an occupied
            // square would desynchronise board_ from the bitboards.
            if (pc == NO_PIECE || file >= FILE_NB || rank < RANK_1)
                return false;
            const Square s = make_square(File(file), Rank(rank));
            if (board_[s] != NO_PIECE)
                return false;
            put_piece(pc, s);
            ++file;
        }
    }
    if (rank != RANK_1 || file != FILE_NB)
        return false;

    if (stmPart != "w" && stmPart != "b")
        return false;
    sideToMove_ = (stmPart == "b") ? BLACK : WHITE;

    st_.castlingRights = NO_CASTLING;
    for (char c : castlePart) {
        switch (c) {
        case 'K': st_.castlingRights |= WHITE_OO; break;
        case 'Q': st_.castlingRights |= WHITE_OOO; break;
        case 'k': st_.castlingRights |= BLACK_OO; break;
        case 'q': st_.castlingRights |= BLACK_OOO; break;
        case '-': break;
        default: return false;
        }
    }

    // Movegen reads a right as "the rook is home"; a FEN that claims a right
    // without the pieces to back it would make it castle with thin air.
    for (Color c : {WHITE, BLACK}) {
        if (board_[relative_square(c, SQ_E1)] != make_piece(c, KING))
            st_.castlingRights &= ~rogatia::castling_rights(c);
        if (board_[relative_square(c, SQ_H1)] != make_piece(c, ROOK))
            st_.castlingRights &= ~king_side_right(c);
        if (board_[relative_square(c, SQ_A1)] != make_piece(c, ROOK))
            st_.castlingRights &= ~queen_side_right(c);
    }

    // One table, applied to both from- and to-square of every move: covers
    // king moves, rook moves and rook captures in a single AND.
    castlingMask_[SQ_E1] &= ~WHITE_CASTLING;
    castlingMask_[SQ_H1] &= ~WHITE_OO;
    castlingMask_[SQ_A1] &= ~WHITE_OOO;
    castlingMask_[SQ_E8] &= ~BLACK_CASTLING;
    castlingMask_[SQ_H8] &= ~BLACK_OO;
    castlingMask_[SQ_A8] &= ~BLACK_OOO;

    st_.epSquare = SQ_NONE;
    if (epPart.size() >= 2 && epPart[0] >= 'a' && epPart[0] <= 'h'
        && epPart[1] >= '1' && epPart[1] <= '8') {
        const Square ep = make_square(File(epPart[0] - 'a'), Rank(epPart[1] - '1'));
        // GUIs emit stale ep squares.  Keep only one a pawn could actually have
        // just double-pushed to -- otherwise movegen emits an en-passant
        // capture and make_move removes a piece that is not there.
        const Square victim = ep - pawn_push(sideToMove_);
        // The last clause is the same rule make_move applies: an ep square no
        // pawn can use is not part of the position, and the two paths have to
        // agree or the identical position hashes differently depending on
        // whether it arrived by FEN or by moves.
        if (relative_rank(sideToMove_, ep) == RANK_6 && board_[ep] == NO_PIECE
            && board_[ep + pawn_push(sideToMove_)] == NO_PIECE
            && board_[victim] == make_piece(~sideToMove_, PAWN)
            && (pawn_attacks_bb(~sideToMove_, ep) & pieces(sideToMove_, PAWN)))
            st_.epSquare = ep;
    }

    st_.rule50   = std::clamp(halfmove, 0, 100);
    st_.captured = NO_PIECE;
    gamePly_     = 2 * (std::max(fullmove, 1) - 1) + (sideToMove_ == BLACK ? 1 : 0);

    // Everything past this point dereferences the kings, so they must exist.
    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1)
        return false;
    if (byType_[PAWN] & (Rank1BB | Rank8BB))
        return false;

    set_state();

    // The side that just moved may not still be attackable: that position can
    // never arise and search would "win" by capturing the king.
    return !is_attacked(king_square(~sideToMove_), sideToMove_, pieces());
}

std::string Position::fen() const {
    std::ostringstream ss;

    for (int r = RANK_8; r >= RANK_1; --r) {
        int gap = 0;
        for (int f = FILE_A; f <= FILE_H; ++f) {
            const Piece pc = board_[make_square(File(f), Rank(r))];
            if (pc == NO_PIECE) {
                ++gap;
            } else {
                if (gap)
                    ss << gap;
                gap = 0;
                ss << PieceChars[pc];
            }
        }
        if (gap)
            ss << gap;
        if (r != RANK_1)
            ss << '/';
    }

    ss << (sideToMove_ == WHITE ? " w " : " b ");

    if (!st_.castlingRights)
        ss << '-';
    else {
        if (st_.castlingRights & WHITE_OO)  ss << 'K';
        if (st_.castlingRights & WHITE_OOO) ss << 'Q';
        if (st_.castlingRights & BLACK_OO)  ss << 'k';
        if (st_.castlingRights & BLACK_OOO) ss << 'q';
    }

    ss << ' ' << square_name(st_.epSquare) << ' ' << st_.rule50 << ' '
       << (1 + (gamePly_ - (sideToMove_ == BLACK ? 1 : 0)) / 2);

    return ss.str();
}

// ----------------------------------------------------------- piece moves ---

void Position::update_keys(Piece pc, Square s) {
    const PieceType pt = type_of(pc);
    const Color     c  = color_of(pc);

    st_.key ^= zobrist::Psq[KEY_MAIN][pc][s];

    if (pt == PAWN)
        st_.pawnKey ^= zobrist::Psq[KEY_PAWN][pc][s];
    else
        st_.nonPawnKey[c] ^= zobrist::Psq[KEY_NONPAWN][pc][s];

    if (pt == ROOK || pt == QUEEN || pt == KING)
        st_.majorKey ^= zobrist::Psq[KEY_MAJOR][pc][s];

    if (pt == KNIGHT || pt == BISHOP || pt == KING)
        st_.minorKey ^= zobrist::Psq[KEY_MINOR][pc][s];
}

void Position::put_piece(Piece pc, Square s) {
    assert(board_[s] == NO_PIECE);
    board_[s] = pc;
    byColor_[color_of(pc)] |= square_bb(s);
    byType_[type_of(pc)] |= square_bb(s);
    update_keys(pc, s);
}

void Position::remove_piece(Square s) {
    const Piece pc = board_[s];
    assert(pc != NO_PIECE);
    byColor_[color_of(pc)] ^= square_bb(s);
    byType_[type_of(pc)] ^= square_bb(s);
    board_[s] = NO_PIECE;
    update_keys(pc, s);
}

void Position::move_piece(Square from, Square to) {
    const Piece    pc     = board_[from];
    const Bitboard fromTo = square_bb(from) | square_bb(to);
    assert(pc != NO_PIECE && board_[to] == NO_PIECE);
    byColor_[color_of(pc)] ^= fromTo;
    byType_[type_of(pc)] ^= fromTo;
    board_[from] = NO_PIECE;
    board_[to]   = pc;
    update_keys(pc, from);
    update_keys(pc, to);
}

// ---------------------------------------------------------------- state ----

void Position::set_state() {
    st_.key      = 0;
    st_.pawnKey  = 0;
    st_.majorKey = 0;
    st_.minorKey = 0;
    st_.nonPawnKey[WHITE] = st_.nonPawnKey[BLACK] = 0;

    for (int s = 0; s < SQUARE_NB; ++s)
        if (board_[s] != NO_PIECE)
            update_keys(board_[s], Square(s));

    if (sideToMove_ == BLACK)
        st_.key ^= zobrist::Side;

    st_.key ^= zobrist::Castling[st_.castlingRights];

    if (st_.epSquare != SQ_NONE)
        st_.key ^= zobrist::EnPassant[file_of(st_.epSquare)];

    compute_checkers_and_blockers();
}

Bitboard Position::slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const {
    Bitboard blockers = 0;
    pinners           = 0;

    // Sliders that would hit s on an empty board.
    Bitboard snipers = ((pseudo_attacks(ROOK, s) & pieces(ROOK, QUEEN))
                        | (pseudo_attacks(BISHOP, s) & pieces(BISHOP, QUEEN)))
                       & sliders;
    const Bitboard occ = pieces() ^ snipers;

    while (snipers) {
        const Square   sniperSq = pop_lsb(snipers);
        const Bitboard b        = between_bb(s, sniperSq) & occ;

        if (b && !more_than_one(b)) {
            blockers |= b;
            if (b & pieces(color_of(board_[s])))
                pinners |= square_bb(sniperSq);
        }
    }
    return blockers;
}

void Position::compute_checkers_and_blockers() {
    const Color us = sideToMove_;
    st_.checkers   = attackers_to(king_square(us), pieces()) & pieces(~us);

    // Side to move only.  is_legal() is the single consumer of blockers_ and it
    // never asks for the other colour; pinned() and gives_check() have no
    // callers anywhere.  This runs on every make_move, so the second pass was a
    // full slider_blockers -- two pseudo-attack lookups and a sniper loop --
    // computed, copied into BoardState and popped again without ever being read.
    //
    // The consequence is that blockers_[~us] and pinners_[~us] hold the values
    // from the previous ply and are NOT valid for this position.  Anything that
    // wants them back -- a gives_check() fast path for check extensions, say --
    // has to restore the second pass with it.
    st_.blockers[us] = slider_blockers(pieces(~us), king_square(us), st_.pinners[us]);
}

Bitboard Position::attackers_to(Square s, Bitboard occ) const {
    return (pawn_attacks_bb(BLACK, s) & pieces(WHITE, PAWN))
         | (pawn_attacks_bb(WHITE, s) & pieces(BLACK, PAWN))
         | (attacks<KNIGHT>(s) & byType_[KNIGHT])
         | (attacks<KING>(s) & byType_[KING])
         | (attacks<BISHOP>(s, occ) & pieces(BISHOP, QUEEN))
         | (attacks<ROOK>(s, occ) & pieces(ROOK, QUEEN));
}

bool Position::is_attacked(Square s, Color by, Bitboard occ) const {
    return (attackers_to(s, occ) & byColor_[by]) != 0;
}

// -------------------------------------------------------------- legality ---

bool Position::is_pseudo_legal(Move m) const {
    if (!is_ok(m))
        return false;

    const Color    us   = sideToMove_;
    const Square   from = from_sq(m);
    const Square   to   = to_sq(m);
    const MoveType mt   = type_of(m);
    const Piece    pc   = board_[from];

    if (pc == NO_PIECE || color_of(pc) != us)
        return false;

    // The promotion field is only defined for a PROMOTION move.  Requiring it
    // to be zero otherwise makes the set of moves accepted here exactly the set
    // generate() emits, which is what makes the two testable against each other.
    if (mt != PROMOTION && (int(m) >> 14) != 0)
        return false;

    const PieceType pt = type_of(pc);

    if (mt == CASTLING) {
        // `to` is the king's landing square, which is empty, so the generic
        // "not onto our own piece" test below must not run before this branch.
        if (pt != KING || st_.checkers || from != relative_square(us, SQ_E1))
            return false;
        const bool kingSide = to > from;
        if (to != relative_square(us, kingSide ? SQ_G1 : SQ_C1))
            return false;
        if (!can_castle(kingSide ? king_side_right(us) : queen_side_right(us)))
            return false;
        // Rights are kept honest by set() and castlingMask_, but the path is
        // position state and must be re-tested.
        return !(pieces() & between_bb(from, relative_square(us, kingSide ? SQ_H1 : SQ_A1)));
    }

    if (pieces(us) & square_bb(to))
        return false;
    // A move that captures a king cannot exist; letting one through would
    // leave the board kingless and every later king_square() reading lsb(0).
    if (type_of(board_[to]) == KING)
        return false;

    if (mt == EN_PASSANT) {
        return pt == PAWN && st_.epSquare != SQ_NONE && to == st_.epSquare
            && (pawn_attacks_bb(us, from) & square_bb(to))
            && board_[Square(to - pawn_push(us))] == make_piece(~us, PAWN);
    }

    if (pt == PAWN) {
        // Reaching the last rank is a promotion and nothing else; anywhere
        // earlier cannot be one.
        if ((relative_rank(us, to) == RANK_8) != (mt == PROMOTION))
            return false;

        const Square up = from + pawn_push(us);
        if (pawn_attacks_bb(us, from) & square_bb(to))
            return (pieces(~us) & square_bb(to)) != 0;
        if (to == up)
            return board_[to] == NO_PIECE;
        return to == Square(up + pawn_push(us)) && relative_rank(us, from) == RANK_2
            && board_[to] == NO_PIECE && board_[up] == NO_PIECE;
    }

    if (mt == PROMOTION)
        return false;

    return (attacks(pt, from, pieces()) & square_bb(to)) != 0;
}

bool Position::is_legal(Move m) const {
    assert(is_ok(m));

    const Color    us   = sideToMove_;
    const Color    them = ~us;
    const Square   from = from_sq(m);
    const Square   to   = to_sq(m);
    const MoveType mt   = type_of(m);
    const Square   ksq  = king_square(us);

    if (mt == EN_PASSANT) {
        // Two pieces leave the board at once, which can uncover a rook or
        // queen on the rank.  Test the real post-move occupancy; anything
        // cleverer than this is where engines get en passant wrong.
        const Square   capsq  = to - pawn_push(us);
        const Bitboard occ    = (pieces() ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
        const Bitboard theirs = pieces(them) ^ square_bb(capsq);
        return !(attackers_to(ksq, occ) & theirs);
    }

    if (mt == CASTLING) {
        // Movegen already checked rights and an empty path.  The king may not
        // start, pass through, or land on an attacked square.
        const Direction d = (to > from) ? EAST : WEST;
        for (Square s = from;; s += d) {
            if (is_attacked(s, them, pieces()))
                return false;
            if (s == to)
                break;
        }
        return true;
    }

    if (type_of(board_[from]) == KING)
        // Take the king off the board first, or it shadows the very ray it is
        // trying to run away along.
        return !(attackers_to(to, pieces() ^ square_bb(from)) & pieces(them));

    // Generation is naive when in check (everything, then filtered here), so a
    // non-king move must additionally answer the check: capture the checker or
    // interpose.  Double check leaves only king moves, handled above.
    if (st_.checkers) {
        if (more_than_one(st_.checkers))
            return false;
        const Square checker = lsb(st_.checkers);
        if (!((between_bb(ksq, checker) | square_bb(checker)) & square_bb(to)))
            return false;
    }

    // Otherwise legal unless the piece is pinned and leaves the pin ray.
    return !(blockers_for_king(us) & square_bb(from)) || aligned(from, to, ksq);
}

bool Position::gives_check(Move m) const {
    assert(is_ok(m));

    const Color    us   = sideToMove_;
    const Color    them = ~us;
    const Square   from = from_sq(m);
    const Square   to   = to_sq(m);
    const MoveType mt   = type_of(m);
    const Square   ksq  = king_square(them);

    if (mt == CASTLING) {
        const bool   kingSide = to > from;
        const Square rfrom = relative_square(us, kingSide ? SQ_H1 : SQ_A1);
        const Square rto   = relative_square(us, kingSide ? SQ_F1 : SQ_D1);
        const Bitboard after =
            (pieces() ^ square_bb(from) ^ square_bb(rfrom)) | square_bb(to) | square_bb(rto);
        return (attacks<ROOK>(rto, after) & square_bb(ksq)) != 0;
    }

    PieceType pt    = type_of(board_[from]);
    Bitboard  after = (pieces() ^ square_bb(from)) | square_bb(to);
    if (mt == EN_PASSANT)
        after ^= square_bb(to - pawn_push(us));
    if (mt == PROMOTION)
        pt = promotion_type(m);

    // Direct check from the destination square.
    const Bitboard direct = (pt == PAWN) ? pawn_attacks_bb(us, to) : attacks(pt, to, after);
    if (direct & square_bb(ksq))
        return true;

    // Discovered check through the square(s) the move vacated.  The mover is
    // excluded from the slider sets because the piece bitboards still list it
    // on `from`, which is now empty in `after`.
    const Bitboard ourSliders = pieces(us) ^ square_bb(from);
    return (attacks<ROOK>(ksq, after) & ourSliders & pieces(ROOK, QUEEN))
        || (attacks<BISHOP>(ksq, after) & ourSliders & pieces(BISHOP, QUEEN));
}

int Position::repetitions(int stopAt) const {
    // Same side to move means an even number of plies back, and no repetition
    // can cross an irreversible move, so rule50 bounds the search.
    const int base  = int(history_.size());
    const int end   = std::min(std::min(st_.rule50, st_.pliesFromNull), base);
    int       found = 0;

    for (int i = 4; i <= end; i += 2)
        if (history_[base - i].key == st_.key && ++found >= stopAt)
            break;

    return found;
}

bool Position::is_draw_for_search() const {
    // ponytail: ignores the "unless it is checkmate" corner of the 50-move
    // rule; costs a mate score in a position that is already drawn-ish.
    // Upgrade path: return the mate score when in check with no legal moves.
    if (st_.rule50 >= 100)
        return true;

    // One repetition, not two.  Inside the search a position already seen is
    // one the opponent can force back to, so scoring it as a draw is sound and
    // prunes far more than waiting for the third occurrence.
    return repetitions(1) >= 1;
}

bool Position::is_game_draw() const {
    return st_.rule50 >= 100 || repetitions(2) >= 2;
}

// ----------------------------------------------------------- make/unmake ---

void Position::make_move(Move m) {
    assert(is_ok(m));

    history_.push_back(st_);

    const Color    us   = sideToMove_;
    const Color    them = ~us;
    const Square   from = from_sq(m);
    const Square   to   = to_sq(m);
    const MoveType mt   = type_of(m);
    const Piece    pc   = board_[from];

    st_.captured = (mt == EN_PASSANT) ? make_piece(them, PAWN)
                 : (mt == CASTLING)   ? NO_PIECE
                                      : board_[to];
    ++st_.rule50;
    ++st_.pliesFromNull;

    if (st_.epSquare != SQ_NONE) {
        st_.key ^= zobrist::EnPassant[file_of(st_.epSquare)];
        st_.epSquare = SQ_NONE;
    }

    if (mt == CASTLING) {
        const bool   kingSide = to > from;
        const Square rfrom    = relative_square(us, kingSide ? SQ_H1 : SQ_A1);
        const Square rto      = relative_square(us, kingSide ? SQ_F1 : SQ_D1);

        remove_piece(from);
        remove_piece(rfrom);
        put_piece(pc, to);
        put_piece(make_piece(us, ROOK), rto);
    } else {
        if (st_.captured != NO_PIECE) {
            const Square capsq = (mt == EN_PASSANT) ? Square(to - pawn_push(us)) : to;
            remove_piece(capsq);
            st_.rule50 = 0;
        }

        move_piece(from, to);

        if (mt == PROMOTION) {
            remove_piece(to);
            put_piece(make_piece(us, promotion_type(m)), to);
        }
    }

    const int newRights = st_.castlingRights & castlingMask_[from] & castlingMask_[to];
    if (newRights != st_.castlingRights) {
        st_.key ^= zobrist::Castling[st_.castlingRights] ^ zobrist::Castling[newRights];
        st_.castlingRights = newRights;
    }

    if (type_of(pc) == PAWN) {
        st_.rule50 = 0;
        // Only record the square when a pawn can actually take there.  An
        // unusable ep right is not part of the position, and hashing it splits
        // one position into two keys: the table stops matching them, and
        // repetitions()  -- which compares keys -- stops seeing them as equal.
        if ((int(to) ^ int(from)) == 16) {
            const Square ep = Square((int(from) + int(to)) / 2);
            if (pawn_attacks_bb(us, ep) & pieces(them, PAWN)) {
                st_.epSquare = ep;
                st_.key ^= zobrist::EnPassant[file_of(ep)];
            }
        }
    }

    sideToMove_ = them;
    st_.key ^= zobrist::Side;
    ++gamePly_;

    compute_checkers_and_blockers();
}

void Position::unmake_move(Move m) {
    assert(is_ok(m));
    assert(!history_.empty());

    sideToMove_ = ~sideToMove_;

    const Color    us   = sideToMove_;
    const Square   from = from_sq(m);
    const Square   to   = to_sq(m);
    const MoveType mt   = type_of(m);

    if (mt == CASTLING) {
        const bool   kingSide = to > from;
        const Square rfrom    = relative_square(us, kingSide ? SQ_H1 : SQ_A1);
        const Square rto      = relative_square(us, kingSide ? SQ_F1 : SQ_D1);

        remove_piece(to);
        remove_piece(rto);
        put_piece(make_piece(us, KING), from);
        put_piece(make_piece(us, ROOK), rfrom);
    } else {
        if (mt == PROMOTION) {
            remove_piece(to);
            put_piece(make_piece(us, PAWN), to);
        }

        move_piece(to, from);

        if (st_.captured != NO_PIECE) {
            const Square capsq = (mt == EN_PASSANT) ? Square(to - pawn_push(us)) : to;
            put_piece(st_.captured, capsq);
        }
    }

    // Everything the move destroyed comes back by popping, never by recomputing.
    st_ = history_.back();
    history_.pop_back();
    --gamePly_;
}

void Position::make_null_move() {
    assert(!st_.checkers);

    history_.push_back(st_);

    if (st_.epSquare != SQ_NONE) {
        st_.key ^= zobrist::EnPassant[file_of(st_.epSquare)];
        st_.epSquare = SQ_NONE;
    }

    st_.captured      = NO_PIECE;
    st_.pliesFromNull = 0;
    ++st_.rule50;

    sideToMove_ = ~sideToMove_;
    st_.key ^= zobrist::Side;
    ++gamePly_;

    // No piece moved, so checkers is empty by construction -- but blockers are
    // keyed on the side to move and the pin sets are symmetric, so a plain
    // recompute is both correct and cheap enough at null-move frequency.
    compute_checkers_and_blockers();
}

void Position::unmake_null_move() {
    assert(!history_.empty());

    sideToMove_ = ~sideToMove_;
    st_         = history_.back();
    history_.pop_back();
    --gamePly_;
}

// ---------------------------------------------------------------- debug ----

std::string Position::to_string() const {
    std::string out = "+---+---+---+---+---+---+---+---+\n";
    for (int r = RANK_8; r >= RANK_1; --r) {
        for (int f = FILE_A; f <= FILE_H; ++f) {
            out += "| ";
            const Piece pc = board_[make_square(File(f), Rank(r))];
            out += (pc == NO_PIECE) ? ' ' : PieceChars[pc];
            out += ' ';
        }
        out += "| ";
        out += char('1' + r);
        out += "\n+---+---+---+---+---+---+---+---+\n";
    }
    out += "  a   b   c   d   e   f   g   h\n\nFen: " + fen() + "\n";
    return out;
}

bool Position::pos_is_ok() const {
    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1)
        return false;
    if ((byColor_[WHITE] & byColor_[BLACK]) != 0)
        return false;

    Bitboard all = 0;
    for (int pt = PAWN; pt <= KING; ++pt) {
        if (all & byType_[pt])
            return false;
        all |= byType_[pt];
    }
    if (all != pieces())
        return false;

    for (int s = 0; s < SQUARE_NB; ++s) {
        const Piece pc = board_[s];
        if (pc == NO_PIECE) {
            if (pieces() & square_bb(Square(s)))
                return false;
        } else if (!(pieces(color_of(pc), type_of(pc)) & square_bb(Square(s)))) {
            return false;
        }
    }
    return true;
}

}  // namespace rogatia
