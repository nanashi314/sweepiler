#pragma once
#include "Constraints.h"
#include "ClassicKernels.h"
#include <cstdint>
#include <vector>
namespace ims {
// Repeated solving of one finite template whose boundary cells change every
// tick. The template's ordinary rules (revealed digits around unknown cells)
// are indexed once; a tick fixes the given cells and applies the ordinary
// count and subset deductions. Cells deduced this way are forced: no other
// ordinary model exists. Anything left undecided is passed to the Z3 solver.
class FrameSolver {
public:
    explicit FrameSolver(const Problem& templateProblem);
    explicit FrameSolver(CellGrid grid);
    struct Result {
        SolveStatus status{SolveStatus::Error};
        bool forced{};                 // every unknown cell decided by deduction alone
        std::vector<uint8_t> mine;     // per template cell: 1 mine, 0 safe (fixed cells included)
        size_t deduced{},undecided{};
        double elapsedMs{};
        std::string message;
    };
    // given: cell -> mine bit for unknown template cells (inputs and boundary).
    Result solve(const std::vector<std::pair<Coord,uint8_t>>& given) const;
    const CellGrid& grid() const {return grid_;}
    size_t unknownCount() const {return unknowns_.size();}
    // Template cell index (row-major over the problem core), or -1.
    int64_t cellIndex(Coord c) const;
    bool fixedMine(size_t cell) const {return fixed_[cell]==1;}
    int digit(size_t cell) const {return digit_[cell];}  // -1 if not a revealed digit
    bool unknown(size_t cell) const {return fixed_[cell]<0;}
    int64_t unknownIndex(size_t cell) const {return unknownOf_[cell];}   // -1 for fixed cells
    const std::vector<uint32_t>& unknownCells() const {return unknowns_;}
private:
    void index();
    CellGrid grid_;
    int width_{},height_{};
    std::vector<int8_t> fixed_;          // per cell: 1 mine, 0 safe digit, -1 unknown
    std::vector<int8_t> digit_;
    std::vector<uint8_t> baseMine_;      // fixed mines, the start of every model
    std::vector<uint32_t> ruleDigit_;    // rule -> its digit cell
    std::vector<uint32_t> forcedAtStart_; // rules that force their cells before any input
    std::vector<uint32_t> unknowns_;     // cell indices of unknown cells
    std::vector<int32_t> unknownOf_;     // cell -> unknown index or -1
    struct Rule {int need; uint32_t begin,count;};
    std::vector<Rule> rules_;
    std::vector<uint32_t> ruleCells_;    // unknown indices per rule
    std::vector<uint32_t> cellRuleBegin_,cellRules_; // unknown -> rules
    std::vector<uint32_t> nearBegin_,near_;          // rule -> rules sharing a cell
};
}
