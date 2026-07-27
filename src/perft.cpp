// Rogatia chess engine -- perft.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "perft.h"

#include <iostream>

#include "movegen.h"

namespace rogatia {

std::string move_to_uci(Move m) {
    if (m == MOVE_NONE)
        return "0000";

    std::string s = square_name(from_sq(m)) + square_name(to_sq(m));
    if (type_of(m) == PROMOTION)
        s += " pnbrqk"[promotion_type(m)];
    return s;
}

std::uint64_t perft(Position& pos, int depth) {
    if (depth <= 0)
        return 1;

    const MoveList<ALL> moves(pos);

    // Bulk counting: the generator is pseudo-legal, so filter before counting.
    if (depth == 1) {
        std::uint64_t n = 0;
        for (Move m : moves)
            n += pos.is_legal(m);
        return n;
    }

    std::uint64_t nodes = 0;
    for (Move m : moves) {
        if (!pos.is_legal(m))
            continue;
        pos.make_move(m);
        nodes += perft(pos, depth - 1);
        pos.unmake_move(m);
    }
    return nodes;
}

std::uint64_t perft_divide(Position& pos, int depth, std::ostream& os) {
    if (depth <= 0)
        return 1;

    const MoveList<ALL> moves(pos);
    std::uint64_t       total = 0;

    for (Move m : moves) {
        if (!pos.is_legal(m))
            continue;

        std::uint64_t nodes = 1;
        if (depth > 1) {
            pos.make_move(m);
            nodes = perft(pos, depth - 1);
            pos.unmake_move(m);
        }

        os << move_to_uci(m) << ": " << nodes << '\n';
        total += nodes;
    }

    os << "\nNodes searched: " << total << '\n';
    return total;
}

std::uint64_t perft_divide(Position& pos, int depth) {
    return perft_divide(pos, depth, std::cout);
}

}  // namespace rogatia
