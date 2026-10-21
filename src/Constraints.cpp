#include "Constraints.h"
#include <z3++.h>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <stdexcept>
namespace ims {
SolveResult solveClassic(const Problem& p,std::stop_token stop);
bool Layout::contains(Coord p) const { return p.x>=min.x && p.x<=max.x && p.y>=min.y && p.y<=max.y; }
size_t Layout::index(Coord p) const {
    return static_cast<size_t>(p.y-min.y)*width+static_cast<size_t>(p.x-min.x);
}
Coord Layout::coord(int x,int y) const { return {min.x+x,min.y+y}; }
std::string makeLayout(const Problem& p,Layout& l) {
    if(auto e=p.rules.validate();!e.empty()) return e;
    static const auto ordinary=RuleTable::classic();
    const bool classic=p.rules.symbols==ordinary.symbols && p.rules.counts==ordinary.counts;
    int axisLimit=classic?32768:64;
    if(p.width<1 || p.height<1 || p.width>axisLimit || p.height>axisLimit) return "Core exceeds axis limit";
    if(static_cast<int64_t>(p.width+2)*(p.height+2)>4194304) return "Core plus halo exceeds 4194304 cells";
    if(p.timeoutMs<1 || p.timeoutMs>60000) return "Timeout must be 1..60000 ms";
    if(p.boundary!=Boundary::FreeHalo && p.boundary!=Boundary::FixedHalo) return "Invalid boundary mode";
    if(p.boundary==Boundary::FixedHalo && (p.haloSymbol<0 || p.haloSymbol>=static_cast<int>(p.rules.symbols.size()))) return "Invalid fixed halo symbol";
    if(!addChecked(p.origin.x,-1,l.min.x) || !addChecked(p.origin.y,-1,l.min.y) ||
       !addChecked(p.origin.x,p.width,l.max.x) || !addChecked(p.origin.y,p.height,l.max.y)) return "Core plus halo exceeds int64 coordinates";
    l.width=p.width+2; l.height=p.height+2;
    if(p.pins.size()>l.size()*2) return "Too many fixed assignments";
    for(const auto& pin:p.pins) {
        if(!l.contains(pin.coord)) return "Fixed assignment lies outside core + halo";
        if(p.rules.index(pin.symbol)<0) return "Unknown fixed symbol: "+pin.symbol;
    }
    if(p.exclusions.size()>l.size()*2) return "Too many exclusions";
    for(const auto& e:p.exclusions) {
        if(!l.contains(e.coord)) return "Excluded assignment lies outside core + halo";
        if(p.rules.index(e.symbol)<0) return "Unknown excluded symbol: "+e.symbol;
    }
    return {};
}
const char* statusName(SolveStatus s) {
    switch(s) {
    case SolveStatus::Sat: return "SAT"; case SolveStatus::Unsat: return "UNSAT";
    case SolveStatus::Unknown: return "UNKNOWN"; case SolveStatus::Cancelled: return "CANCELLED";
    case SolveStatus::Invalid: return "INVALID"; default: return "ERROR";
    }
}
std::string verifyModel(const Problem& p,const std::vector<uint16_t>& values) {
    Layout l; if(auto e=makeLayout(p,l);!e.empty()) return e;
    if(values.size()!=l.size()) return "Incomplete model";
    for(auto v:values) if(v>=p.rules.symbols.size()) return "Model symbol out of domain";
    for(const auto& pin:p.pins) if(values[l.index(pin.coord)]!=p.rules.index(pin.symbol)) return "Model violates fixed assignment";
    for(const auto& e:p.exclusions) if(values[l.index(e.coord)]==p.rules.index(e.symbol)) return "Model violates excluded assignment";
    for(int y=0;y<l.height;++y) for(int x=0;x<l.width;++x) {
        size_t i=static_cast<size_t>(y)*l.width+x;
        if(!l.core(x,y)) {
            if(p.boundary==Boundary::FixedHalo && values[i]!=p.haloSymbol) return "Model violates fixed halo";
            continue;
        }
        const auto& row=p.rules.counts[values[i]];
        if(std::all_of(row.begin(),row.end(),[](int n){return n<0;})) continue;
        std::vector<int> counts(p.rules.symbols.size(),0);
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
            if(dx || dy) ++counts[values[static_cast<size_t>(y+dy)*l.width+x+dx]];
        for(size_t target=0;target<row.size();++target)
            if(row[target]>=0 && counts[target]!=row[target]) return "Model violates a local neighbour count";
    }
    return {};
}
SolveResult solve(const Problem& p,std::stop_token stop) {
    static const auto ordinary=RuleTable::classic();
    if(p.compactClassic && p.rules.symbols==ordinary.symbols && p.rules.counts==ordinary.counts) return solveClassic(p,stop);
    using Clock=std::chrono::steady_clock;
    auto start=Clock::now(); SolveResult result;
    auto elapsed=[&] { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); };
    auto done=[&](SolveStatus s,std::string message) { result.status=s; result.message=std::move(message); result.elapsedMs=elapsed(); return result; };
    if(auto e=makeLayout(p,result.layout);!e.empty()) return done(SolveStatus::Invalid,e);
    if(stop.stop_requested()) return done(SolveStatus::Cancelled,"Cancelled");
    try {
        z3::context ctx;
        std::stop_callback interrupt(stop,[&ctx] { ctx.interrupt(); });
        z3::solver solver(ctx);
        auto guard=[&] {
            if(stop.stop_requested()) throw SolveStatus::Cancelled;
            if(elapsed()>=p.timeoutMs) throw SolveStatus::Unknown;
        };
        const auto& l=result.layout; const auto alphabet=p.rules.symbols.size();
        std::vector<bool> activeRows(alphabet);
        for(size_t s=0;s<alphabet;++s) activeRows[s]=std::any_of(p.rules.counts[s].begin(),p.rules.counts[s].end(),[](int n){return n>=0;});
        std::vector<uint16_t> all(alphabet); std::iota(all.begin(),all.end(),uint16_t{0});
        std::vector<std::vector<uint16_t>> domains(l.size(),all);
        bool conflict=false;
        auto fix=[&](size_t cell,int symbol) {
            auto& d=domains[cell];
            if(std::find(d.begin(),d.end(),symbol)==d.end()) { conflict=true; return; }
            d.assign(1,static_cast<uint16_t>(symbol));
        };
        for(const auto& pin:p.pins) fix(l.index(pin.coord),p.rules.index(pin.symbol));
        for(const auto& e:p.exclusions) {
            auto& d=domains[l.index(e.coord)]; std::erase(d,static_cast<uint16_t>(p.rules.index(e.symbol)));
            if(d.empty()) conflict=true;
        }
        if(p.boundary==Boundary::FixedHalo)
            for(int y=0;y<l.height;++y) for(int x=0;x<l.width;++x)
                if(!l.core(x,y)) fix(static_cast<size_t>(y)*l.width+x,p.haloSymbol);
        // This is only cardinality propagation on the SAME local rules.
        // There are no gate types, expected outputs or Boolean gate evaluators here.
        std::vector<size_t> queue; std::vector<bool> queued(l.size(),false);
        auto schedule=[&](int x,int y) {
            if(!l.core(x,y)) return;
            size_t i=static_cast<size_t>(y)*l.width+x;
            if(!queued[i] && domains[i].size()==1 && activeRows[domains[i][0]]) { queue.push_back(i); queued[i]=true; }
        };
        auto affected=[&](size_t i) {
            int x=static_cast<int>(i%l.width),y=static_cast<int>(i/l.width);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) schedule(x+dx,y+dy);
        };
        if(p.propagate && !conflict) for(int y=1;y<l.height-1;++y) for(int x=1;x<l.width-1;++x) schedule(x,y);
        for(size_t head=0;head<queue.size() && !conflict;++head) {
                guard(); size_t cell=queue[head]; queued[cell]=false;
                int x=static_cast<int>(cell%l.width),y=static_cast<int>(cell/l.width);
                const auto& row=p.rules.counts[domains[cell][0]];
                for(size_t target=0;target<alphabet && !conflict;++target) if(row[target]>=0) {
                    int fixed=0,possible=0; std::vector<size_t> undecided;
                    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
                        size_t other=static_cast<size_t>(y+dy)*l.width+x+dx; const auto& d=domains[other];
                        if(std::find(d.begin(),d.end(),target)!=d.end()) {
                            ++possible; if(d.size()==1) ++fixed; else undecided.push_back(other);
                        }
                    }
                    if(fixed>row[target] || possible<row[target]) { conflict=true; break; }
                    if(undecided.empty()) continue;
                    if(fixed==row[target]) for(auto other:undecided) {
                        auto& d=domains[other]; std::erase(d,static_cast<uint16_t>(target)); affected(other);
                        if(d.empty()) conflict=true;
                    }
                    else if(possible==row[target]) for(auto other:undecided) {
                        domains[other].assign(1,static_cast<uint16_t>(target)); affected(other);
                    }
                }
        }
        guard();
        // Fixed cells become constants. Only remaining alternatives need SAT variables.
        std::vector<int> mapping(l.size()*alphabet,-1);
        z3::expr_vector variables(ctx);
        if(conflict) solver.add(ctx.bool_val(false));
        else for(size_t cell=0;cell<l.size();++cell) {
            guard(); const auto& d=domains[cell];
            if(d.size()==1) { mapping[cell*alphabet+d[0]]=-2; continue; }
            z3::expr_vector choices(ctx); std::vector<int> ones(d.size(),1);
            for(auto s:d) {
                mapping[cell*alphabet+s]=static_cast<int>(variables.size());
                variables.push_back(ctx.bool_const(("v"+std::to_string(variables.size())).c_str()));
                choices.push_back(variables.back());
            }
            solver.add(z3::pbeq(choices,ones.data(),1));
        }
        result.booleanVariables=variables.size();
        auto expression=[&](size_t cell,size_t s) {
            int id=mapping[cell*alphabet+s];
            return id>=0 ? variables[static_cast<unsigned>(id)] : ctx.bool_val(id==-2);
        };
        if(!conflict) for(int y=1;y<l.height-1;++y) for(int x=1;x<l.width-1;++x) {
            guard(); size_t cell=static_cast<size_t>(y)*l.width+x;
            if(domains[cell].size()==1 && !activeRows[domains[cell][0]]) continue;
            // Fold constant contradictions even with propagation disabled. This only
            // removes a source row already falsified by pinned neighbour counts.
            std::vector<int> fixedCounts(alphabet,0); std::vector<uint16_t> fixedSymbols;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
                const auto& d=domains[static_cast<size_t>(y+dy)*l.width+x+dx];
                if(d.size()==1) { ++fixedCounts[d[0]]; fixedSymbols.push_back(d[0]); }
            }
            for(auto source:domains[cell]) {
                const auto& row=p.rules.counts[source];
                bool impossible=false;
                for(auto s:fixedSymbols) if(row[s]>=0 && row[s]<fixedCounts[s]) { impossible=true; break; }
                if(impossible) { solver.add(!expression(cell,source)); ++result.localConstraints; continue; }
                for(size_t target=0;target<alphabet;++target) if(row[target]>=0) {
                    ++result.localConstraints; int fixed=0; z3::expr_vector neighbours(ctx);
                    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
                        size_t other=static_cast<size_t>(y+dy)*l.width+x+dx;
                        int id=mapping[other*alphabet+target];
                        if(id==-2) ++fixed; else if(id>=0) neighbours.push_back(variables[static_cast<unsigned>(id)]);
                    }
                    int needed=row[target]-fixed;
                    if(needed<0 || needed>static_cast<int>(neighbours.size())) solver.add(!expression(cell,source));
                    else if(!neighbours.empty()) {
                        std::vector<int> ones(neighbours.size(),1);
                        solver.add(z3::implies(expression(cell,source),z3::pbeq(neighbours,ones.data(),needed)));
                    }
                }
            }
        }
        guard(); z3::params params(ctx);
        params.set("timeout",static_cast<unsigned>(std::max(1.0,p.timeoutMs-elapsed())));
        if(p.resourceLimit) params.set("rlimit",p.resourceLimit);
        solver.set(params);
        auto status=solver.check(); guard();
        if(status==z3::unsat) return done(SolveStatus::Unsat,"No model for this finite core + halo");
        if(status==z3::unknown) return done(SolveStatus::Unknown,solver.reason_unknown());
        auto model=solver.get_model(); result.labels.reserve(l.size());
        for(size_t cell=0;cell<l.size();++cell) {
            guard(); bool found=false;
            for(auto s:domains[cell]) if(model.eval(expression(cell,s),true).is_true()) {
                result.labels.push_back(s); found=true; break;
            }
            if(!found) throw std::runtime_error("Z3 returned an incomplete cell");
        }
        if(auto e=verifyModel(p,result.labels);!e.empty()) throw std::runtime_error(e);
        guard(); result.modelChecked=true;
        return done(SolveStatus::Sat,"One checked model; uniqueness not tested");
    } catch(SolveStatus s) {
        result.labels.clear(); return done(s,s==SolveStatus::Cancelled?"Cancelled":"Time budget exhausted");
    } catch(const std::exception& e) {
        result.labels.clear();
        return done(stop.stop_requested()?SolveStatus::Cancelled:SolveStatus::Error,e.what());
    }
}
}
