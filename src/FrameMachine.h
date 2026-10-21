#pragma once
#include "FrameSolver.h"
#include "MachineFrame.h"
#include <algorithm>
#include <deque>
#include <memory>
namespace ims {
// Frames of one run laid side by side on an ordinary Minesweeper board.
// Frame k occupies [k*stride, k*stride+stride] in x; its right port column is
// frame k+1's left port column, so the state crosses between frames through
// the same unknown cells. Each tick adds a frame whose key cells are mines for
// pressed keys and safe otherwise, then solves only that frame with the shared
// column taken from the previous frame's forced model. The oldest kept frame's
// left column is the starting state. A kept frame stores one bit per unknown
// template cell, so long runs stay on the board.
class FrameMachine {
public:
    // keep: how many frames stay on the board (0 keeps all).
    FrameMachine(std::shared_ptr<const MachineFrame> frame,size_t keep=4);
    struct Frame {
        int64_t index{};
        std::vector<uint8_t> keys;
        std::vector<uint64_t> mines;  // per unknown template cell, packed
        bool forced{};
        size_t deduced{};
        double solveMs{};
    };
    void reset();
    void setKeep(size_t keep) {keep_=keep==0?0:std::max<size_t>(keep,2);}
    // Solves the next frame; throws if it is not forced. compute() only reads
    // the newest frame, so it may run while another thread draws.
    Frame compute(const std::vector<uint8_t>& keys) const;
    void commit(Frame frame);
    const Frame& step(const std::vector<uint8_t>& keys) {commit(compute(keys)); return frames_.back();}
    const std::deque<Frame>& frames() const {return frames_;}
    const MachineFrame& frame() const {return *frame_;}
    int64_t ticks() const {return next_;}
    // Read from the newest frame's model at the display and state ports.
    std::vector<uint8_t> display() const;
    std::vector<uint8_t> state() const;
    // Board appearance of a global cell for drawing.
    enum class Cell : int8_t {Outside,Digit,FixedMine,Mine,Covered,InputMine,InputSafe};
    Cell cell(Coord c,int* digit=nullptr) const;
    int64_t frameX(int64_t index) const {return index*frame_->stride;}
    // Whether template cell `cell` is a mine in the frame.
    bool mine(const Frame& fr,size_t cell) const;
    size_t frameBytes() const {return (solver_.unknownCount()+63)/64*8;}
    Bounds bounds() const;
private:
    const Frame* frameAt(Coord c,int64_t& localX) const;
    std::shared_ptr<const MachineFrame> frame_;
    FrameSolver solver_;
    size_t keep_;
    std::deque<Frame> frames_;
    int64_t next_{};
    std::vector<uint8_t> inputCell_;  // 1 key cell (given each tick), 2 state-in cell (given for the oldest)
};
}
