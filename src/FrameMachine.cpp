#include "FrameMachine.h"
#include <stdexcept>
namespace ims {
FrameMachine::FrameMachine(std::shared_ptr<const MachineFrame> frame,size_t keep):frame_(std::move(frame)),solver_(frame_->grid),keep_(keep==0?0:std::max<size_t>(keep,2)) {
    if(frame_->circuit.initial.size()!=frame_->stateIn.size()) throw std::invalid_argument("Starting state does not match the frame");
    inputCell_.assign(frame_->grid.cells.size(),0);
    for(auto c:frame_->keys) inputCell_[static_cast<size_t>(solver_.cellIndex(c))]=1;
    for(auto c:frame_->stateIn) inputCell_[static_cast<size_t>(solver_.cellIndex(c))]=2;
}
void FrameMachine::reset() {frames_.clear(); next_=0;}
FrameMachine::Frame FrameMachine::compute(const std::vector<uint8_t>& keys) const {
    const auto& f=*frame_;
    if(keys.size()!=f.keys.size()) throw std::invalid_argument("Key count does not match the frame");
    std::vector<std::pair<Coord,uint8_t>> given;
    // The shared column: the previous frame's forced output cells, or the starting state.
    for(size_t i=0;i<f.stateIn.size();++i) {
        uint8_t v=frames_.empty()?f.circuit.initial[i]:mine(frames_.back(),static_cast<size_t>(solver_.cellIndex(f.stateOut[i])));
        given.push_back({f.stateIn[i],v});
    }
    for(size_t i=0;i<keys.size();++i) given.push_back({f.keys[i],static_cast<uint8_t>(keys[i]&1)});
    auto r=solver_.solve(given);
    if(r.status!=SolveStatus::Sat || !r.forced) throw std::runtime_error("Frame not forced: "+r.message);
    Frame fr; fr.index=next_; fr.keys=keys; fr.forced=r.forced;
    const auto& unknowns=solver_.unknownCells(); fr.mines.assign((unknowns.size()+63)/64,0);
    for(size_t u=0;u<unknowns.size();++u) if(r.mine[unknowns[u]]) fr.mines[u/64]|=uint64_t{1}<<(u%64); fr.deduced=r.deduced; fr.solveMs=r.elapsedMs;
    return fr;
}
void FrameMachine::commit(Frame fr) {
    if(fr.index!=next_) throw std::logic_error("Frames must be committed in order");
    ++next_; frames_.push_back(std::move(fr));
    while(keep_ && frames_.size()>keep_) frames_.pop_front();
}
bool FrameMachine::mine(const Frame& fr,size_t cell) const {
    if(solver_.fixedMine(cell)) return true;
    int64_t u=solver_.unknownIndex(cell); if(u<0) return false;
    return (fr.mines[static_cast<size_t>(u)/64]>>(static_cast<size_t>(u)%64))&1;
}
std::vector<uint8_t> FrameMachine::display() const {
    std::vector<uint8_t> d(frame_->display.size(),0);
    if(frames_.empty()) return d;
    for(size_t i=0;i<d.size();++i) d[i]=mine(frames_.back(),static_cast<size_t>(solver_.cellIndex(frame_->display[i])));
    return d;
}
std::vector<uint8_t> FrameMachine::state() const {
    if(frames_.empty()) return frame_->circuit.initial;
    std::vector<uint8_t> bits(frame_->stateOut.size());
    for(size_t i=0;i<bits.size();++i) bits[i]=mine(frames_.back(),static_cast<size_t>(solver_.cellIndex(frame_->stateOut[i])));
    return bits;
}
const FrameMachine::Frame* FrameMachine::frameAt(Coord c,int64_t& localX) const {
    if(frames_.empty()) return nullptr;
    // Frame k spans [k*stride+left, k*stride+right]; on a shared column the
    // newer frame wins, and the oldest also shows its left margin.
    const int64_t first=frames_.front().index,last=frames_.back().index,stride=frame_->stride;
    int64_t rel=c.x-frame_->left,k=rel>=0?rel/stride:-((-rel+stride-1)/stride);
    if(k>last && c.x==frameX(last)+frame_->right) k=last;
    if(k<first && c.x>=frameX(first)+frame_->grid.origin.x) k=first;
    if(k<first || k>last) return nullptr;
    int64_t x=c.x-frameX(k);
    if(x>frame_->right || x<(k==first?frame_->grid.origin.x:frame_->left)) return nullptr;
    localX=x; return &frames_[static_cast<size_t>(k-first)];
}
FrameMachine::Cell FrameMachine::cell(Coord c,int* digit) const {
    int64_t x=0; const Frame* fr=frameAt(c,x); if(!fr) return Cell::Outside;
    auto index=solver_.cellIndex({x,c.y}); if(index<0) return Cell::Outside;
    auto i=static_cast<size_t>(index);
    if(solver_.fixedMine(i)) return Cell::FixedMine;
    if(solver_.digit(i)>=0) {if(digit) *digit=solver_.digit(i); return Cell::Digit;}
    bool input=inputCell_[i]==1 || (inputCell_[i]==2 && fr->index==frames_.front().index);
    bool m=mine(*fr,i);
    if(input) return m?Cell::InputMine:Cell::InputSafe;
    return m?Cell::Mine:Cell::Covered;
}
Bounds FrameMachine::bounds() const {
    const auto& g=frame_->grid;
    int64_t first=frames_.empty()?0:frames_.front().index,last=frames_.empty()?0:frames_.back().index;
    return {{frameX(first)+g.origin.x,g.origin.y},{frameX(last)+frame_->right,g.origin.y+g.height-1}};
}
}
