#pragma once
#include "Constraints.h"
#include "ClassicKernels.h"
#include <string>
#include <vector>
namespace ims {
struct GridInfo {
    int width{},height{};
    size_t gates{},nets{},links{},crossings{},adapters{},retries{};
    double routeMs{};
};
// Two-dimensional placement with lattice maze routing. Geometry only: the result
// is ordinary numbered clues, fixed mines and unknown cells, checked again by
// ClassicPatch::finish. Callers choose kernel slots and edge ports.
class GridLayout {
public:
    struct Signal {int net{-1};bool inverted{};};
    enum class Side {Left,Right,Up,Down};
    // A net driven by an unknown port cell; routes leave the port towards exit.
    int input(Coord port,Side exit);
    // NOR kernel with origin slot (even coordinates). Occupies x-7..x+9, y..y+6;
    // inputs enter at x-8 (y+2, y+4) and the output leaves at x+10, y+4.
    int nor(Signal a,Signal b,Coord slot);
    // Port cell carrying s. outward points from the stub to the port.
    void output(Signal s,Coord port,Side outward);
    // Keeps routes strictly between the two port columns, so frames can share them.
    void frameColumns(int64_t left,int64_t right) {frameLeft_=left; frameRight_=right; framed_=true;}
    Problem compile(GridInfo* info=nullptr) const {return compileGrid(info).toProblem();}
    CellGrid compileGrid(GridInfo* info=nullptr) const;
    size_t gateCount() const {return gates_.size();}
    size_t netCount() const {return drivers_.size();}
private:
    struct Driver {Coord at;Side exit;int gate{-1};};
    struct Gate {Signal a,b;Coord slot;int net{-1};};
    struct Output {Signal s;Coord port;Side outward;};
    std::vector<Driver> drivers_;
    std::vector<Gate> gates_;
    std::vector<Output> outputs_;
    int64_t frameLeft_{},frameRight_{}; bool framed_{};
};
}
