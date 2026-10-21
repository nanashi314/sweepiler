#pragma once
#include "Board.h"
#include "Rules.h"
#include <stop_token>
#include <vector>
namespace ims {
enum class Boundary { FreeHalo, FixedHalo };
struct Pin { Coord coord; std::string symbol; };
struct Exclusion { Coord coord; std::string symbol; }; // An input/probe assumption, not a local rule.
struct Problem {
    Coord origin{}; // Top-left cell whose local rule is enforced.
    int width{5},height{5};
    RuleTable rules{RuleTable::example1()};
    Boundary boundary{Boundary::FreeHalo};
    int haloSymbol{};
    std::vector<Pin> pins; // May address core or halo, never outside the snapshot.
    std::vector<Exclusion> exclusions; // E.g. a known-safe ordinary cell excludes '*', not a particular number.
    unsigned timeoutMs{3000}; // Whole solve budget, including encoding and checking.
    unsigned resourceLimit{}; // 0 = Z3 default; used by deterministic UNKNOWN tests.
    bool propagate{true}; // Sound local cardinality propagation; false tests the plain encoding.
    bool compactClassic{true}; // Exact mine-only encoding for the unmodified ordinary rules; false retains generic one-hot encoding for comparison.
};
struct Layout {
    Coord min{},max{}; // Core plus one-cell halo.
    int width{},height{};
    size_t size() const { return static_cast<size_t>(width)*height; }
    bool contains(Coord p) const;
    bool core(int x,int y) const { return x>0 && y>0 && x<width-1 && y<height-1; }
    size_t index(Coord p) const;
    Coord coord(int x,int y) const;
};
// Rejects coordinate overflow and resource-exhausting requests before allocation.
std::string makeLayout(const Problem& problem,Layout& layout);
enum class SolveStatus { Sat, Unsat, Unknown, Cancelled, Invalid, Error };
const char* statusName(SolveStatus status);
struct SolveResult {
    SolveStatus status{SolveStatus::Error};
    std::string message;
    Layout layout;
    std::vector<uint16_t> labels; // One symbol index per core+halo cell, row-major.
    size_t booleanVariables{},localConstraints{};
    double elapsedMs{};
    bool modelChecked{};
};
// Independent integer-count validator, with no dependency on Z3.
std::string verifyModel(const Problem& problem,const std::vector<uint16_t>& labels);
SolveResult solve(const Problem& problem,std::stop_token stop={});
}
