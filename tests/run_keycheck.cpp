// Verify every incrementally-maintained Zobrist key against a from-scratch
// rebuild, at every node of a small tree.
//
// Nothing in the project did this. Perft counts nodes and never looks at a key;
// unmake_move restores BoardState by popping, so keys round-trip correctly even
// if update_keys attributes a piece to the wrong set. A bug there would stay
// invisible until something actually *read* pawnKey or nonPawnKey -- which
// correction history now does.
//
// Method: fen() round-trip. The FEN encodes board + side + castling + ep, which
// is exactly what determines all five keys, so a Position re-parsed from
// pos.fen() has keys rebuilt from scratch by set_state().
#include <cstdio>
#include <string>

#include "movegen.h"
#include "perft.h"  // move_to_uci
#include "position.h"

using namespace rogatia;

static long long nodes = 0, bad = 0;

static void check(const Position& pos, const std::string& trail) {
    Position fresh;
    fresh.set(pos.fen());
    ++nodes;

    struct { const char* name; Key a, b; } k[] = {
        {"main",     pos.key(),               fresh.key()},
        {"pawn",     pos.pawn_key(),          fresh.pawn_key()},
        {"major",    pos.major_key(),         fresh.major_key()},
        {"minor",    pos.minor_key(),         fresh.minor_key()},
        {"nonPawnW", pos.non_pawn_key(WHITE), fresh.non_pawn_key(WHITE)},
        {"nonPawnB", pos.non_pawn_key(BLACK), fresh.non_pawn_key(BLACK)},
    };
    for (auto& e : k)
        if (e.a != e.b) {
            if (++bad <= 10)
                std::printf("  MISMATCH %-9s incremental %016llx != rebuilt %016llx\n"
                            "    fen   %s\n    moves %s\n",
                            e.name, (unsigned long long) e.a, (unsigned long long) e.b,
                            pos.fen().c_str(), trail.c_str());
        }
}

static void walk(Position& pos, int depth, const std::string& trail) {
    check(pos, trail);
    if (depth == 0)
        return;
    for (Move m : MoveList<ALL>(pos)) {
        if (!pos.is_legal(m))
            continue;
        pos.make_move(m);
        walk(pos, depth - 1, trail + move_to_uci(m) + " ");
        pos.unmake_move(m);
    }
}

int main() {
    init();

    // The perft suite positions: castling, en passant, promotions, the lot.
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1",
    };

    for (const char* f : fens) {
        Position pos;
        if (!pos.set(f)) {
            std::printf("bad fen: %s\n", f);
            return 1;
        }
        walk(pos, 3, "");
    }

    std::printf("\n%lld nodes checked, %lld key mismatches\n", nodes, bad);
    if (bad)
        std::printf("FAIL -- an incremental key does not match a from-scratch rebuild\n");
    else
        std::printf("OK -- all five key sets are maintained correctly\n");
    return bad ? 1 : 0;
}
