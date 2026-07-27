// Rogatia chess engine -- UCI protocol.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ROGATIA_UCI_H
#define ROGATIA_UCI_H

namespace rogatia {

// Reads UCI commands from stdin until EOF or `quit`.
void uci_loop();

}  // namespace rogatia

#endif  // ROGATIA_UCI_H
