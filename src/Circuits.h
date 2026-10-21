#pragma once
#include "Constraints.h"
#include <array>
#include <optional>
namespace ims {
enum class CircuitKind { Wire, Not, Nor, Fanout, Bend, Crossover, WireNotNor, RoutedNor, NorSR, NorSRStep, ClassicWire, ClassicNot, ClassicBend, ClassicFanout, ClassicNor, ClassicCross, ClassicWireNotNor, ClassicRoutedNor, ClassicSR, ClassicSRStep };
const char* circuitName(CircuitKind kind);
struct Port { std::string name; Coord coord; };
struct CircuitPart { std::string name; Bounds bounds; };
struct Circuit {
    CircuitKind kind{};
    std::string provenance;
    Problem problem; // Scaffold only: NO inputs or expected outputs are baked in.
    std::vector<Port> inputs,outputs;
    std::vector<Coord> signals;
    std::vector<CircuitPart> parts; // Display metadata only; never consumed by the solver.
};
Circuit makeCircuit(CircuitKind kind);
void pinPort(Problem& problem,const Port& port,int bit);
Circuit translated(Circuit circuit,Coord offset);
Circuit rotated(Circuit circuit); // 90 degrees about (0,0).
struct CircuitCheck { unsigned inputs{},outputs{}; bool expectedSat{}; SolveStatus actual{}; std::string detail; };
struct CircuitReport {
    std::vector<CircuitCheck> checks;
    bool passed{},cancelled{};
};
// Boolean expressions occur ONLY in this reference oracle, never in generation/solving/rendering.
unsigned referenceOutput(CircuitKind kind,unsigned inputBits);
// The cross-coupled NOR latch has two stable states on S=R=0.
bool referenceAccepts(CircuitKind kind,unsigned inputBits,unsigned outputBits);
CircuitReport verifyCircuit(const Circuit& circuit,std::stop_token stop={});
}
