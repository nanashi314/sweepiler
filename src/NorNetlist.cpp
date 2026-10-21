#include "NorNetlist.h"
#include <stdexcept>
namespace ims {
std::vector<uint8_t> NorNetlist::evaluate(const std::vector<uint8_t>& bits) const {
    if(bits.size()!=inputs.size()) throw std::invalid_argument("Netlist input count mismatch");
    std::vector<uint8_t> s(inputs.size()+gates.size());
    for(size_t i=0;i<bits.size();++i) s[i]=bits[i]&1;
    for(size_t g=0;g<gates.size();++g) s[inputs.size()+g]=static_cast<uint8_t>(!(value(s,gates[g][0])|value(s,gates[g][1])));
    return s;
}
NorSignal NorBuilder::nor(NorSignal a,NorSignal b) {
    // Constant folding: NOR(1,x)=0, NOR(0,x)=NOT x, NOR(x,x)=NOT x, NOR(x,NOT x)=0.
    if(a.constant() && a.inverted) return constant(false);
    if(b.constant() && b.inverted) return constant(false);
    if(a.constant()) return !b;
    if(b.constant()) return !a;
    if(a.id==b.id) return a.inverted==b.inverted?!a:constant(false);
    auto x=std::pair{a.id,a.inverted},y=std::pair{b.id,b.inverted}; if(y<x) {std::swap(x,y); std::swap(a,b);}
    if(auto it=shared_.find({x,y});it!=shared_.end()) return {it->second,false};
    int id=net_.gateId(net_.gates.size());
    net_.gates.push_back({a,b}); net_.hints.push_back({hx_,hy_}); shared_[{x,y}]=id;
    return {id,false};
}
NorSignal NorBuilder::xorGate(NorSignal a,NorSignal b) {
    if(a.constant()) return a.inverted?!b:b;
    if(b.constant()) return b.inverted?!a:a;
    auto n=nor(a,b); auto l=nor(a,n),r=nor(b,n); return !nor(l,r);
}
NorSignal NorBuilder::any(const std::vector<NorSignal>& v) {
    if(v.empty()) return constant(false);
    std::vector<NorSignal> level=v;
    while(level.size()>1) {
        std::vector<NorSignal> next;
        for(size_t i=0;i+1<level.size();i+=2) next.push_back(orGate(level[i],level[i+1]));
        if(level.size()%2) next.push_back(level.back());
        level=std::move(next);
    }
    return level[0];
}
NorSignal NorBuilder::anyChain(const std::vector<NorSignal>& v,const std::vector<std::pair<float,float>>& at) {
    if(v.size()!=at.size()) throw std::invalid_argument("Chain hint count mismatch");
    NorSignal acc=constant(false);
    for(size_t i=0;i<v.size();++i) {hint(at[i].first,at[i].second); acc=orGate(acc,v[i]);}
    return acc;
}
NorSignal NorBuilder::all(const std::vector<NorSignal>& v) {
    std::vector<NorSignal> inverted; for(auto s:v) inverted.push_back(!s);
    return !any(inverted);
}
}
