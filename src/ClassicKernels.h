#pragma once
#include "ClassicCircuits.h"
#include <map>
#include <set>
namespace ims {
struct ClassicCoordLess { bool operator()(Coord a,Coord b) const { return a.y<b.y || (a.y==b.y && a.x<b.x); } };
// A finished board in compact form: per cell -1 unknown, 0..8 revealed digit, 9 fixed mine.
// The surrounding halo is fixed mines, as with Boundary::FixedHalo.
struct CellGrid {
    Coord origin; int width{},height{};
    std::vector<int8_t> cells;
    int8_t at(Coord c) const {int64_t x=c.x-origin.x,y=c.y-origin.y; return x<0 || y<0 || x>=width || y>=height?int8_t{9}:cells[static_cast<size_t>(y*width+x)];}
    Problem toProblem() const; // ordinary Problem with every fixed cell pinned (small grids)
};
// Construction-time geometry only. All compiled constraints are ordinary revealed numbers.
class ClassicPatch {
public:
    void variable(Coord p) { variables_.insert(p); }
    void count(Coord source,int mines,std::initializer_list<Coord> neighbours);
    void link(Coord a,Coord b,Coord clue); // An actual ordinary clue requiring one of the two mines.
    void wire(std::initializer_list<Coord> corners);
    std::array<Port,3> nor(Coord offset,bool mirrored=false,bool flipped=false); // A, B, Y. One-hot kernel plus physical inversions.
    std::array<Port,4> crossing(Coord offset); // A, B, Y=A, Z=B.
    Circuit finish(CircuitKind kind,std::vector<Port> inputs,std::vector<Port> outputs,int padding=2,bool emitScaffold=true) const;
    // Same adjacency certificate as finish(), emitted as a compact grid for large boards.
    CellGrid finishGrid(int padding=1) const;
    size_t clueCount() const {return counts_.size();}
private:
    struct Requirement { int mines; std::set<Coord,ClassicCoordLess> neighbours; };
    std::set<Coord,ClassicCoordLess> variables_;
    std::map<Coord,Requirement,ClassicCoordLess> counts_;
};
Circuit makeClassicKernelCircuit(CircuitKind kind);
}
