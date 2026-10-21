#pragma once
#include "Circuits.h"
namespace ims {
inline constexpr std::array classicKinds{CircuitKind::ClassicWire,CircuitKind::ClassicNot,CircuitKind::ClassicBend,CircuitKind::ClassicFanout,CircuitKind::ClassicNor,CircuitKind::ClassicCross,CircuitKind::ClassicWireNotNor,CircuitKind::ClassicRoutedNor,CircuitKind::ClassicSR,CircuitKind::ClassicSRStep};
bool isClassicCircuit(CircuitKind kind);
Circuit makeClassicCircuit(CircuitKind kind);
// Safe excludes a mine without asserting its neighbouring mine count.
void pinClassicPort(Problem& problem,const Port& port,int bit);
int classicInput(const Board& board,Coord coord);
void setClassicInput(Board& board,Coord coord,int bit); // -1 removes the assumption.
int classicBit(const std::string& symbol); // -1 unknown, 0 safe number, 1 mine.
}
