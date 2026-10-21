#pragma once
#include "NorNetlist.h"
#include <cstdint>
#include <string>
#include <vector>
namespace ims {
// A clocked machine for chained frames: one tick as a NOR netlist.
// Inputs: state bits, then keys. Outputs: next state bits (same order), then
// display pixels row-major.
struct FrameCircuit {
    std::string name;
    NorNetlist net;
    size_t stateBits{};
    std::vector<std::string> keys;
    std::vector<uint8_t> keyPress;   // 1: counts once per press, 0: while held
    int displayWidth{},displayHeight{};
    std::vector<uint8_t> initial;    // starting state bits
    bool hinted{};                   // gates carry spatial placement hints
};
}
