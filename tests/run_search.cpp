// Rogatia -- search sanity checks.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Makefile is owned elsewhere and has no target for this, so build it by
// hand alongside the core objects:
//
//   g++ -std=c++20 -O2 -march=native -DNDEBUG -I. -o build/run_search
//   tests/run_search.cpp plus every src/*.cpp except main.cpp
//
// Exit code 0 iff every check passed.
#include <cstdio>
#include <string>
#include <vector>

#include "../src/movegen.h"
#include "../src/perft.h"
#include "../src/position.h"
#include "../src/search.h"
#include "../src/tt.h"

using namespace rogatia;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %-4s %s\n", ok ? "OK" : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

int legal_move_count(const Position& pos) {
    int n = 0;
    for (Move m : MoveList<ALL>(pos))
        n += pos.is_legal(m);
    return n;
}

std::string pv_text(const std::vector<Move>& pv) {
    std::string s;
    for (Move m : pv)
        s += move_to_uci(m) + ' ';
    return s;
}

// A PV is only meaningful if every move in it is playable in turn.
bool pv_is_legal(const std::string& fen, const std::vector<Move>& pv) {
    Position pos;
    pos.set(fen);
    for (Move m : pv) {
        bool found = false;
        for (Move g : MoveList<ALL>(pos))
            found |= (g == m && pos.is_legal(m));
        if (!found)
            return false;
        pos.make_move(m);
    }
    return true;
}

// Announcing a mate is only worth anything if playing the line out reaches one.
// The announced distance is checked against the PV rather than against a number
// written here, so the test validates the engine instead of trusting it.
void mate_test(const std::string& fen, int maxPlies, int depth) {
    TT.clear();
    Position pos;
    pos.set(fen);

    const search::Result r = search::search_fixed_depth(pos, depth);

    const bool isMate = is_mate_score(r.score) && r.score > 0;
    const int  plies  = isMate ? VALUE_MATE - r.score : -1;

    std::printf("%s\n  score %d (mate in %d plies)  pv %s\n", fen.c_str(), r.score, plies,
                pv_text(r.pv).c_str());

    check(isMate && plies <= maxPlies, "finds a mate within " + std::to_string(maxPlies) + " plies");
    check(pv_is_legal(fen, r.pv), "pv is legal");
    check(int(r.pv.size()) == plies, "pv length matches the announced distance");

    // Walk the PV and confirm the last position really is checkmate.
    Position end;
    end.set(fen);
    for (Move m : r.pv)
        end.make_move(m);
    check(end.in_check() && legal_move_count(end) == 0, "pv ends in checkmate");
}

// A full game engine-vs-engine.  Nothing about the result matters; the point is
// that every move is legal and the game terminates.
void self_play(int depth, int maxPlies) {
    search::clear();
    Position pos;
    pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    int  ply    = 0;
    bool legal  = true;
    std::string result = "adjudicated (ply limit)";

    for (; ply < maxPlies; ++ply) {
        if (legal_move_count(pos) == 0) {
            result = pos.in_check() ? "checkmate" : "stalemate";
            break;
        }
        if (pos.is_game_draw()) {
            result = "draw by repetition or 50-move";
            break;
        }

        const search::Result r = search::search_fixed_depth(pos, depth);

        bool found = false;
        for (Move m : MoveList<ALL>(pos))
            found |= (m == r.best && pos.is_legal(m));
        if (!found) {
            legal  = false;
            result = "ILLEGAL MOVE " + move_to_uci(r.best);
            break;
        }
        pos.make_move(r.best);
    }

    std::printf("  %d plies, %s\n", ply, result.c_str());
    check(legal, "self-play produced only legal moves");
    check(pos.pos_is_ok(), "board is consistent after the game");
}

// is_pseudo_legal exists so a TT move probed from a colliding key cannot put
// the board in an impossible state.  That path fires roughly once per 65536
// entries, so normal play will not exercise it -- the only way to trust it is
// to enumerate.  Every one of the 65536 possible 16-bit moves must be accepted
// if and only if generate() emits it.
void pseudo_legal_exhaustive(const std::string& fen) {
    Position pos;
    if (!pos.set(fen)) {
        check(false, "FEN accepted: " + fen);
        return;
    }

    bool generated[1 << 16] = {};
    for (Move m : MoveList<ALL>(pos))
        generated[std::uint16_t(m)] = true;

    int falseAccept = 0, falseReject = 0;
    for (int i = 0; i < (1 << 16); ++i) {
        const Move m  = Move(std::uint16_t(i));
        const bool ok = pos.is_pseudo_legal(m);
        if (ok && !generated[i])
            ++falseAccept;
        if (!ok && generated[i])
            ++falseReject;
    }

    std::printf("  %s\n", fen.c_str());
    check(falseAccept == 0,
          "  accepts nothing generate() does not (" + std::to_string(falseAccept) + " extra)");
    check(falseReject == 0,
          "  accepts everything generate() does (" + std::to_string(falseReject) + " missing)");
}

}  // namespace

int main() {
    init();
    TT.resize(16);

    std::printf("== is_pseudo_legal vs generate, all 65536 encodings ==\n");
    pseudo_legal_exhaustive("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    // Kiwipete: both castlings, pins, and a dense middlegame.
    pseudo_legal_exhaustive("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    // En passant available.
    pseudo_legal_exhaustive("rnbqkb1r/ppp1pppp/5n2/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
    // Pawns on the 7th: promotions, including captures.
    pseudo_legal_exhaustive("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
    // Castling rights held but the king's landing square is occupied.
    pseudo_legal_exhaustive("r3k2r/8/8/8/8/8/8/R3K1nR w KQkq - 0 1");
    // In check: only evasions are legal, but pseudo-legality is unaffected.
    pseudo_legal_exhaustive("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    // Black to move, black castling rights.
    pseudo_legal_exhaustive("r3k2r/pppq1ppp/2npbn2/2b1p3/2B1P3/2NPBN2/PPPQ1PPP/R3K2R b KQkq - 6 8");


    std::printf("== mate detection ==\n");
    // Mate in one: back rank, and queen-and-king against a cornered king.
    mate_test("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", 1, 4);
    mate_test("7k/8/6K1/8/8/8/8/1Q6 w - - 0 1", 1, 4);
    // Mate in two: the rook needs one waiting move first.
    mate_test("7k/8/5K2/8/8/8/8/R7 w - - 0 1", 3, 8);
    mate_test("8/8/8/8/8/1k6/7r/K7 b - - 0 1", 3, 8);

    std::printf("\n== start position, depth 8 ==\n");
    {
        TT.clear();
        Position pos;
        pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        const search::Result r = search::search_fixed_depth(pos, 8);
        std::printf("  best %s  score %d  nodes %llu\n  pv %s\n", move_to_uci(r.best).c_str(),
                    r.score, (unsigned long long) r.nodes, pv_text(r.pv).c_str());

        check(pv_is_legal("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", r.pv),
              "pv is legal");
        // A blunder shows up as a score far off equality; the start position is
        // worth a fraction of a pawn to White and nothing more.
        check(r.score > -60 && r.score < 120, "score stays near equality (no material blunder)");
    }

    std::printf("\n== malformed FEN handling ==\n");
    {
        Position pos;
        check(!pos.set("this is not a fen"), "garbage rejected");
        check(pos.pos_is_ok(), "  and leaves a usable board");
        check(!pos.set("8/8/8/8/8/8/8/8 w - - 0 1"), "kingless rejected");
        check(!pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQ1BNR w KQkq - 0 1"),
              "missing white king rejected");
        check(pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e6 0 1"),
              "stale ep square accepted");
        check(pos.ep_square() == SQ_NONE, "  and scrubbed");
        // The side NOT to move standing in check can never have arisen.
        check(!pos.set("4k3/8/8/8/8/4R3/8/4K3 w - - 0 1"), "side-not-to-move in check rejected");
        check(pos.set("4k3/8/8/8/8/8/8/4KQ2 b - - 0 1"), "legal position accepted");
    }

    std::printf("\n== null move round trip ==\n");
    {
        Position pos;
        pos.set("r1bqk2r/2p1bppp/p1np1n2/1p2p3/4P3/1B3N2/PPPP1PPP/RNBQR1K1 w kq - 0 8");
        const std::string before = pos.fen();
        const Key         key    = pos.key();
        pos.make_null_move();
        check(pos.side_to_move() == BLACK, "side flipped");
        pos.unmake_null_move();
        check(pos.fen() == before && pos.key() == key, "state restored exactly");
    }

    std::printf("\n== self-play, depth 4 ==\n");
    self_play(4, 300);

    std::printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
