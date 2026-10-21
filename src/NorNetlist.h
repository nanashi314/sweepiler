#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>
namespace ims {
// A NOR network with free inversion on every wire, as the ordinary routers
// realise it (an inverted input is a phase choice, not a gate).
struct NorSignal {
    int id{-1};        // -1 constant, 0..inputs-1 primary inputs, then gates
    bool inverted{};   // for the constant: inverted = true means 1
    bool operator==(const NorSignal&) const=default;
    NorSignal operator!() const {return {id,!inverted};}
    bool constant() const {return id<0;}
};
struct NorNetlist {
    std::vector<std::string> inputs;
    std::vector<std::array<NorSignal,2>> gates;      // gate g is signal id inputs.size()+g
    std::vector<std::pair<float,float>> hints;       // per gate, a placement hint
    std::vector<std::pair<std::string,NorSignal>> outputs;
    int gateId(size_t gate) const {return static_cast<int>(inputs.size()+gate);}
    // Evaluation for tests and reference comparison only; generation and solving never use it.
    std::vector<uint8_t> evaluate(const std::vector<uint8_t>& inputBits) const;
    uint8_t value(const std::vector<uint8_t>& signals,NorSignal s) const {return static_cast<uint8_t>((s.constant()?0:signals[static_cast<size_t>(s.id)])^static_cast<int>(s.inverted));}
};
// Builds a netlist with constant folding and structural sharing.
class NorBuilder {
public:
    NorSignal input(std::string name) {net_.inputs.push_back(std::move(name)); return {static_cast<int>(net_.inputs.size()-1),false};}
    static NorSignal constant(bool v) {return {-1,v};}
    NorSignal nor(NorSignal a,NorSignal b);
    NorSignal orGate(NorSignal a,NorSignal b) {return !nor(a,b);}
    NorSignal andGate(NorSignal a,NorSignal b) {return nor(!a,!b);}
    NorSignal xorGate(NorSignal a,NorSignal b);
    NorSignal mux(NorSignal select,NorSignal whenZero,NorSignal whenOne) {if(whenZero==whenOne) return whenZero; return orGate(andGate(!select,whenZero),andGate(select,whenOne));}
    NorSignal any(const std::vector<NorSignal>& v);   // balanced OR
    NorSignal all(const std::vector<NorSignal>& v);   // balanced AND
    // OR as a chain; each step is placed at the hint of the term it adds, so a
    // reduction over a grid of cells needs only neighbour wires.
    NorSignal anyChain(const std::vector<NorSignal>& v,const std::vector<std::pair<float,float>>& at);
    void output(std::string name,NorSignal s) {net_.outputs.push_back({std::move(name),s});}
    void hint(float x,float y) {hx_=x; hy_=y;}
    NorNetlist finish() {return std::move(net_);}
    size_t gates() const {return net_.gates.size();}
private:
    NorNetlist net_;
    std::map<std::pair<std::pair<int,bool>,std::pair<int,bool>>,int> shared_;
    float hx_{},hy_{};
};
}
