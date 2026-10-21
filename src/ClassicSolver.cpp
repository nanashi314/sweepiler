#include "Constraints.h"
#include <z3++.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>
namespace ims {
// Exact ordinary rules only. For a safe core cell, its digit is necessarily the
// count of adjacent mines. Eliminating that digit variable leaves one mine bit.
// No circuit identities, gates, input meanings or state updates enter this solver.
SolveResult solveClassic(const Problem& p,std::stop_token stop) {
    auto start=std::chrono::steady_clock::now(); SolveResult r;
    auto elapsed=[&]{return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
    auto done=[&](SolveStatus s,std::string message){r.status=s; r.message=std::move(message); r.elapsedMs=elapsed(); return r;};
    if(auto error=makeLayout(p,r.layout);!error.empty()) return done(SolveStatus::Invalid,error);
    if(stop.stop_requested()) return done(SolveStatus::Cancelled,"Cancelled");
    try {
        z3::context ctx; std::stop_callback interrupt(stop,[&ctx]{ctx.interrupt();}); z3::solver solver(ctx);
        auto guard=[&]{if(stop.stop_requested()) throw SolveStatus::Cancelled; if(elapsed()>=p.timeoutMs) throw SolveStatus::Unknown;};
        const auto& l=r.layout;
        std::vector<int> pinned(l.size(),-1),mine(l.size(),-1),mapping(l.size(),-1);
        std::vector<unsigned> excluded(l.size(),0);
        bool conflict=false;
        auto pin=[&](size_t i,int s){if(pinned[i]>=0 && pinned[i]!=s) conflict=true; pinned[i]=s;};
        for(const auto& a:p.pins) pin(l.index(a.coord),p.rules.index(a.symbol));
        if(p.boundary==Boundary::FixedHalo) for(int y=0;y<l.height;++y) for(int x=0;x<l.width;++x)
            if(!l.core(x,y)) pin(static_cast<size_t>(y)*l.width+x,p.haloSymbol);
        for(const auto& e:p.exclusions) excluded[l.index(e.coord)]|=1u<<p.rules.index(e.symbol);
        for(size_t i=0;i<l.size();++i) {
            guard();
            if((excluded[i]&1023u)==1023u) conflict=true;
            if(pinned[i]>=0) { if(excluded[i]&(1u<<pinned[i])) conflict=true; mine[i]=pinned[i]==9; }
            else if(excluded[i]&(1u<<9)) mine[i]=0;
            else if((excluded[i]&511u)==511u) mine[i]=1;
        }
        // Ordinary count propagation only: if a revealed clue already has its
        // mines, all other neighbours are safe; if all remaining are needed,
        // they are mines. Revisit only nearby revealed clues after a change.
        std::vector<size_t> queue; std::vector<bool> queued(l.size(),false);
        auto schedule=[&](int x,int y) {
            if(!l.core(x,y)) return;
            size_t i=static_cast<size_t>(y)*l.width+x;
            if(pinned[i]>=0 && pinned[i]<9 && !queued[i]) {queue.push_back(i); queued[i]=true;}
        };
        if(p.propagate && !conflict) for(int y=1;y<l.height-1;++y) for(int x=1;x<l.width-1;++x) schedule(x,y);
        for(size_t head=0;head<queue.size() && !conflict;++head) {
            guard(); size_t i=queue[head]; queued[i]=false;
            int x=static_cast<int>(i%l.width),y=static_cast<int>(i/l.width),fixed=0,count=0;
            size_t undecided[8];
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
                size_t j=static_cast<size_t>(y+dy)*l.width+x+dx;
                if(mine[j]==1) ++fixed; else if(mine[j]<0) undecided[count++]=j;
            }
            if(fixed>pinned[i] || fixed+count<pinned[i]) {conflict=true; break;}
            auto force=[&](const size_t* cells,int size,int needed) {
                if(needed<0 || needed>size) {conflict=true; return false;}
                if(!size || (needed!=0 && needed!=size)) return false;
                for(int n=0;n<size;++n) {
                    size_t j=cells[n]; mine[j]=needed!=0;
                    int nx=static_cast<int>(j%l.width),ny=static_cast<int>(j/l.width);
                    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) schedule(nx+dx,ny+dy);
                }
                return true;
            };
            if(force(undecided,count,pinned[i]-fixed) || conflict || !count) continue;
            // Subtract two overlapping revealed-number equations when one
            // unknown-neighbour set contains the other. This is the ordinary
            // Minesweeper subset rule, independent of any circuit templates.
            bool changed=false;
            for(int sy=-2;sy<=2 && !changed && !conflict;++sy) for(int sx=-2;sx<=2 && !changed && !conflict;++sx) {
                if((!sx && !sy) || !l.core(x+sx,y+sy)) continue;
                size_t other=static_cast<size_t>(y+sy)*l.width+x+sx;
                if(pinned[other]<0 || pinned[other]>=9) continue;
                size_t candidates[8]; int n=0,known=0;
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
                    size_t j=static_cast<size_t>(y+sy+dy)*l.width+x+sx+dx;
                    if(mine[j]==1) ++known; else if(mine[j]<0) candidates[n++]=j;
                }
                auto subtract=[&](const size_t* large,int largeN,int largeCount,const size_t* small,int smallN,int smallCount) {
                    if(smallN>largeN) return false;
                    for(int a=0;a<smallN;++a) if(std::find(large,large+largeN,small[a])==large+largeN) return false;
                    size_t difference[8]; int size=0;
                    for(int a=0;a<largeN;++a) if(std::find(small,small+smallN,large[a])==small+smallN) difference[size++]=large[a];
                    return force(difference,size,largeCount-smallCount);
                };
                changed=subtract(undecided,count,pinned[i]-fixed,candidates,n,pinned[other]-known);
                if(!changed && !conflict) changed=subtract(candidates,n,pinned[other]-known,undecided,count,pinned[i]-fixed);
                if(changed) schedule(x,y);
            }
        }
        z3::expr_vector vars(ctx);
        if(!conflict) for(size_t i=0;i<l.size();++i) {
            guard(); if(mine[i]<0) {mapping[i]=static_cast<int>(vars.size()); vars.push_back(ctx.bool_const(("m"+std::to_string(i)).c_str()));}
        }
        r.booleanVariables=vars.size();
        auto isMine=[&](size_t i){return mapping[i]>=0?vars[static_cast<unsigned>(mapping[i])]:ctx.bool_val(mine[i]==1);};
        if(conflict) solver.add(ctx.bool_val(false));
        else for(int y=1;y<l.height-1;++y) for(int x=1;x<l.width-1;++x) {
            guard(); size_t i=static_cast<size_t>(y)*l.width+x;
            if(mine[i]==1 || (pinned[i]<0 && !(excluded[i]&511u))) continue;
            int fixed=0; z3::expr_vector neighbours(ctx);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
                size_t j=static_cast<size_t>(y+dy)*l.width+x+dx;
                if(mine[j]==1) ++fixed; else if(mine[j]<0) neighbours.push_back(isMine(j));
            }
            std::vector<int> ones(neighbours.size(),1);
            auto equals=[&](int count) {
                int residual=count-fixed;
                if(residual<0 || residual>static_cast<int>(neighbours.size())) return ctx.bool_val(false);
                if(neighbours.empty()) return ctx.bool_val(residual==0);
                return z3::pbeq(neighbours,ones.data(),residual);
            };
            if(pinned[i]>=0) { solver.add(equals(pinned[i])); ++r.localConstraints; }
            else for(int count=0;count<=8;++count) if(excluded[i]&(1u<<count)) {
                solver.add(isMine(i) || !equals(count)); ++r.localConstraints;
            }
        }
        guard(); z3::params params(ctx); params.set("timeout",static_cast<unsigned>(std::max(1.0,p.timeoutMs-elapsed())));
        if(p.resourceLimit) params.set("rlimit",p.resourceLimit); solver.set(params);
        auto result=solver.check(); guard();
        if(result==z3::unsat) return done(SolveStatus::Unsat,"No model for this finite core + halo");
        if(result==z3::unknown) return done(SolveStatus::Unknown,solver.reason_unknown());
        auto model=solver.get_model();
        for(size_t i=0;i<l.size();++i) { guard(); if(mine[i]<0) mine[i]=model.eval(isMine(i),true).is_true(); }
        r.labels.resize(l.size());
        for(int y=0;y<l.height;++y) for(int x=0;x<l.width;++x) {
            guard(); size_t i=static_cast<size_t>(y)*l.width+x;
            if(mine[i]) { r.labels[i]=9; continue; }
            int digit=0;
            if(l.core(x,y)) {
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) digit+=mine[static_cast<size_t>(y+dy)*l.width+x+dx];
            } else {
                // Halo labels have no local row, just as in the generic solver.
                if(pinned[i]>=0) digit=pinned[i];
                else while(digit<=8 && (excluded[i]&(1u<<digit))) ++digit;
                if(digit>8) throw std::runtime_error("No safe halo label");
            }
            r.labels[i]=static_cast<uint16_t>(digit);
        }
        if(auto error=verifyModel(p,r.labels);!error.empty()) throw std::runtime_error(error);
        guard(); r.modelChecked=true; return done(SolveStatus::Sat,"One checked ordinary model; uniqueness not tested");
    } catch(SolveStatus status) { r.labels.clear(); return done(status,status==SolveStatus::Cancelled?"Cancelled":"Time budget exhausted"); }
      catch(const std::exception& e) { r.labels.clear(); return done(stop.stop_requested()?SolveStatus::Cancelled:SolveStatus::Error,e.what()); }
}
}
