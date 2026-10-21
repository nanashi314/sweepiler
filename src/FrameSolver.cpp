#include "FrameSolver.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
namespace ims {
FrameSolver::FrameSolver(const Problem& p) {
    if(p.rules.symbols!=RuleTable::classic().symbols || p.boundary!=Boundary::FixedHalo) throw std::invalid_argument("Frame templates use ordinary rules with a fixed halo");
    CellGrid g; g.origin=p.origin; g.width=p.width; g.height=p.height;
    if(p.width<1 || p.height<1 || static_cast<int64_t>(p.width)*p.height>256ll*1024*1024) throw std::invalid_argument("Frame template size");
    g.cells.assign(static_cast<size_t>(p.width)*p.height,-1);
    for(const auto& pin:p.pins) {
        int64_t x=pin.coord.x-p.origin.x,y=pin.coord.y-p.origin.y; if(x<0 || y<0 || x>=p.width || y>=p.height) continue;
        int s=p.rules.index(pin.symbol); if(s<0) throw std::invalid_argument("Unknown template symbol");
        g.cells[static_cast<size_t>(y*p.width+x)]=static_cast<int8_t>(s);
    }
    grid_=std::move(g); index();
}
FrameSolver::FrameSolver(CellGrid grid):grid_(std::move(grid)) {index();}
void FrameSolver::index() {
    width_=grid_.width; height_=grid_.height;
    const size_t n=static_cast<size_t>(width_)*height_;
    fixed_.assign(n,-1); digit_.assign(n,-1);
    for(size_t i=0;i<n;++i) {int8_t c=grid_.cells[i]; if(c==9) fixed_[i]=1; else if(c>=0) {fixed_[i]=0; digit_[i]=c;}}
    baseMine_.assign(n,0); for(size_t i=0;i<n;++i) baseMine_[i]=fixed_[i]==1;
    // Unknown cells and rules are numbered tile by tile (32 x 32 cells), so a
    // wire running in any direction stays in nearby memory while it propagates.
    constexpr int tile=32;
    auto inTiles=[&](auto&& f){
        for(int ty=0;ty<height_;ty+=tile) for(int tx=0;tx<width_;tx+=tile)
            for(int y=ty;y<std::min(ty+tile,height_);++y) for(int x=tx;x<std::min(tx+tile,width_);++x) f(x,y);
    };
    unknownOf_.assign(n,-1);
    inTiles([&](int x,int y){size_t i=static_cast<size_t>(y)*width_+x; if(fixed_[i]<0) {unknownOf_[i]=static_cast<int32_t>(unknowns_.size()); unknowns_.push_back(static_cast<uint32_t>(i));}});
    auto isMine=[&](int x,int y){
        // Outside the core is the fixed halo of mines (FixedHalo) or free (FreeHalo).
        if(x<0 || y<0 || x>=width_ || y>=height_) return 1; // fixed halo of mines
        return static_cast<int>(fixed_[static_cast<size_t>(y)*width_+x]);
    };
    std::vector<std::vector<uint32_t>> byCell(unknowns_.size());
    inTiles([&](int x,int y) {
        size_t i=static_cast<size_t>(y)*width_+x; if(digit_[i]<0) return;
        int need=digit_[i]; std::vector<uint32_t> cells;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
            int v=isMine(x+dx,y+dy);
            if(v==1) --need;
            else if(v<0) {
                if(x+dx<0 || y+dy<0 || x+dx>=width_ || y+dy>=height_) throw std::invalid_argument("Template digits see a free halo");
                cells.push_back(static_cast<uint32_t>(unknownOf_[static_cast<size_t>(y+dy)*width_+x+dx]));
            }
        }
        if(need<0 || need>static_cast<int>(cells.size())) throw std::invalid_argument("Template digit cannot be satisfied");
        if(cells.empty()) return;
        uint32_t id=static_cast<uint32_t>(rules_.size());
        if(need==0 || need==static_cast<int>(cells.size())) forcedAtStart_.push_back(id);
        rules_.push_back({need,static_cast<uint32_t>(ruleCells_.size()),static_cast<uint32_t>(cells.size())}); ruleDigit_.push_back(static_cast<uint32_t>(i));
        for(auto c:cells) {ruleCells_.push_back(c); byCell[c].push_back(id);}
    });
    cellRuleBegin_.push_back(0);
    for(auto& v:byCell) {cellRules_.insert(cellRules_.end(),v.begin(),v.end()); cellRuleBegin_.push_back(static_cast<uint32_t>(cellRules_.size()));}
    nearBegin_.push_back(0);
    for(uint32_t r=0;r<rules_.size();++r) {
        std::vector<uint32_t> near;
        for(uint32_t k=0;k<rules_[r].count;++k) {
            uint32_t c=ruleCells_[rules_[r].begin+k];
            for(uint32_t j=cellRuleBegin_[c];j<cellRuleBegin_[c+1];++j) if(cellRules_[j]!=r) near.push_back(cellRules_[j]);
        }
        std::sort(near.begin(),near.end()); near.erase(std::unique(near.begin(),near.end()),near.end());
        near_.insert(near_.end(),near.begin(),near.end()); nearBegin_.push_back(static_cast<uint32_t>(near_.size()));
    }
}
int64_t FrameSolver::cellIndex(Coord c) const {
    int64_t x=c.x-grid_.origin.x,y=c.y-grid_.origin.y;
    if(x<0 || y<0 || x>=width_ || y>=height_) return -1;
    return y*width_+x;
}
FrameSolver::Result FrameSolver::solve(const std::vector<std::pair<Coord,uint8_t>>& given) const {
    auto start=std::chrono::steady_clock::now(); Result r;
    std::vector<int8_t> value(unknowns_.size(),-1);
    // Per rule: mines still needed and cells still open, kept up to date on
    // every assignment, so a rule is scanned only when it forces its cells.
    // One 4-byte record per rule keeps a rule's whole state in one memory access.
    struct RuleState {int8_t need,open; uint8_t queued,waiting;};
    std::vector<RuleState> rs(rules_.size());
    for(size_t q=0;q<rules_.size();++q) rs[q]={static_cast<int8_t>(rules_[q].need),static_cast<int8_t>(rules_[q].count),0,0};
    std::vector<uint32_t> queue,later; queue.reserve(rules_.size()/4); later.reserve(rules_.size()/8);
    bool conflict=false; size_t assigned=0;
    auto assign=[&](uint32_t u,int8_t v){
        if(value[u]>=0) {if(value[u]!=v) conflict=true; return;}
        value[u]=v; ++assigned;
        for(uint32_t j=cellRuleBegin_[u];j<cellRuleBegin_[u+1];++j) {
            uint32_t q=cellRules_[j]; auto& R=rs[q]; --R.open; if(v) --R.need;
            if(R.need<0 || R.need>R.open) {conflict=true; continue;}
            if(R.open==0) continue;
            if(R.need==0 || R.need==R.open) {if(!R.queued) {R.queued=1; queue.push_back(q);}}
            else if(!R.waiting) {R.waiting=1; later.push_back(q);}
        }
    };
    for(uint32_t q:forcedAtStart_) {rs[q].queued=1; queue.push_back(q);}
    for(const auto& [c,bit]:given) {
        auto i=cellIndex(c); if(i<0 || unknownOf_[static_cast<size_t>(i)]<0) throw std::invalid_argument("Given cell is not an unknown template cell");
        assign(static_cast<uint32_t>(unknownOf_[static_cast<size_t>(i)]),static_cast<int8_t>(bit&1));
    }
    // Ordinary deductions only: the count rule on one digit, and the subset rule
    // on two digits whose unknown sets contain one another.
    auto state=[&](uint32_t rule,std::vector<uint32_t>& cells){
        const auto& R=rules_[rule]; cells.clear();
        for(uint32_t k=0;k<R.count;++k) {uint32_t c=ruleCells_[R.begin+k]; if(value[c]<0) cells.push_back(c);}
        return static_cast<int>(rs[rule].need);
    };
    // Counting runs to a fixpoint first; the costlier subset rule is tried only
    // on rules still open then, and anything it decides restarts counting.
    std::vector<uint32_t> openA,openB,diff;
    size_t head=0;
    for(;;) {
        for(;head<queue.size() && !conflict;++head) {
            uint32_t q=queue[head]; auto& S=rs[q]; S.queued=0;
            if(S.open==0) continue;
            if(S.need!=0 && S.need!=S.open) {if(!S.waiting) {S.waiting=1; later.push_back(q);} continue;}
            int8_t v=S.need==0?0:1;
            const auto& R=rules_[q];
            for(uint32_t k=0;k<R.count && !conflict;++k) {uint32_t c=ruleCells_[R.begin+k]; if(value[c]<0) assign(c,v);}
        }
        if(conflict || later.empty()) break;
        queue.clear(); head=0;
        auto pending=std::move(later); later.clear(); const size_t before=assigned;
        for(uint32_t q:pending) {
            rs[q].waiting=0; if(conflict) break;
            if(rs[q].open==0) continue;
            int left=state(q,openA);
            if(left==0 || left==static_cast<int>(openA.size())) {if(!rs[q].queued) {rs[q].queued=1; queue.push_back(q);} continue;}
            for(uint32_t j=nearBegin_[q];j<nearBegin_[q+1] && !conflict;++j) {
                uint32_t o=near_[j];
                // A subset step decides something only if the counts already say
                // so: the larger rule's extra cells are all mines or all safe (or
                // impossible). Check that before looking at any cell.
                auto decides=[&](uint32_t small,uint32_t large){
                    int extra=rs[large].open-rs[small].open,d=rs[large].need-rs[small].need;
                    return rs[small].open>0 && extra>0 && (d<=0 || d>=extra);
                };
                if(rs[o].open==0 || !(decides(q,o) || decides(o,q))) continue;
                int otherLeft=state(o,openB);
                if(openB.empty()) continue;
                auto subset=[&](const std::vector<uint32_t>& small,int smallLeft,const std::vector<uint32_t>& large,int largeLeft){
                    if(small.size()>=large.size()) return false;
                    for(auto c:small) if(std::find(large.begin(),large.end(),c)==large.end()) return false;
                    diff.clear(); for(auto c:large) if(std::find(small.begin(),small.end(),c)==small.end()) diff.push_back(c);
                    int d=largeLeft-smallLeft;
                    if(d<0 || d>static_cast<int>(diff.size())) {conflict=true; return true;}
                    if(d==0 || d==static_cast<int>(diff.size())) {for(auto c:diff) assign(c,static_cast<int8_t>(d?1:0)); return true;}
                    return false;
                };
                if(subset(openA,left,openB,otherLeft) || subset(openB,otherLeft,openA,left)) {if(!rs[q].queued) {rs[q].queued=1; queue.push_back(q);} break;}
            }
        }
        if(assigned==before) break;   // no subset step decided anything: a fixpoint
    }
    r.mine=baseMine_;
    if(conflict) {r.status=SolveStatus::Unsat; r.message="Ordinary deductions reached a contradiction"; r.elapsedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); return r;}
    for(size_t u=0;u<unknowns_.size();++u) {if(value[u]>=0) ++r.deduced; else ++r.undecided;}
    if(r.undecided) {
        // Fall back to the full ordinary solver with every deduction fixed.
        Problem p=grid_.toProblem();
        for(size_t u=0;u<unknowns_.size();++u) if(value[u]>=0) {
            size_t i=unknowns_[u]; Coord c{grid_.origin.x+static_cast<int64_t>(i%static_cast<size_t>(width_)),grid_.origin.y+static_cast<int64_t>(i/static_cast<size_t>(width_))};
            if(value[u]) p.pins.push_back({c,"*"}); else p.exclusions.push_back({c,"*"});
        }
        auto s=ims::solve(p);
        r.status=s.status; r.message=s.message;
        if(s.status==SolveStatus::Sat) for(size_t u=0;u<unknowns_.size();++u) {
            size_t i=unknowns_[u]; Coord c{grid_.origin.x+static_cast<int64_t>(i%static_cast<size_t>(width_)),grid_.origin.y+static_cast<int64_t>(i/static_cast<size_t>(width_))};
            r.mine[i]=s.labels[s.layout.index(c)]==9;
        }
        r.elapsedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); return r;
    }
    for(size_t u=0;u<unknowns_.size();++u) r.mine[unknowns_[u]]=static_cast<uint8_t>(value[u]);
    // Independent count check of every revealed digit next to an unknown cell
    // (the others see only fixed cells and were checked when indexing).
    for(uint32_t i32:ruleDigit_) {
        size_t i=i32; int x=static_cast<int>(i%static_cast<size_t>(width_)),y=static_cast<int>(i/static_cast<size_t>(width_)); int mines=0;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
            int nx=x+dx,ny=y+dy;
            if(nx<0 || ny<0 || nx>=width_ || ny>=height_) mines+=1; else mines+=r.mine[static_cast<size_t>(ny)*width_+nx];
        }
        if(mines!=digit_[i]) {r.status=SolveStatus::Error; r.message="Deduced model fails a digit count"; return r;}
    }
    r.status=SolveStatus::Sat; r.forced=true; r.message="Every cell forced by ordinary deductions";
    r.elapsedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return r;
}
}
