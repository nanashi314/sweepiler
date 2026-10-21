#include "Circuits.h"
#include "ClassicCircuits.h"
namespace ims {
unsigned referenceOutput(CircuitKind k,unsigned inputs) {
    switch(k) {
    case CircuitKind::Not:case CircuitKind::ClassicNot:return (~inputs)&1;
    case CircuitKind::Nor:case CircuitKind::ClassicNor:return (inputs&3)?0:1;
    case CircuitKind::Fanout:case CircuitKind::ClassicFanout:return (inputs&1)?3:0;
    case CircuitKind::Crossover:case CircuitKind::ClassicCross:return inputs&3;
    case CircuitKind::WireNotNor:case CircuitKind::ClassicWireNotNor:return (inputs&1) && !(inputs&2);
    case CircuitKind::RoutedNor:case CircuitKind::ClassicRoutedNor:return ((inputs&3)?0:1) | ((inputs&1)<<1);
    case CircuitKind::NorSRStep:case CircuitKind::ClassicSRStep: { bool q=!(inputs&2) && ((inputs&1) || (inputs&4)); return q?1:2; }
    case CircuitKind::NorSR:case CircuitKind::ClassicSR:return (inputs&3)==3?0:((inputs&1)?1:2); // One representative; referenceAccepts handles bistability.
    default:return inputs&1;
    }
}
bool referenceAccepts(CircuitKind k,unsigned inputs,unsigned outputs) {
    if((k==CircuitKind::NorSRStep || k==CircuitKind::ClassicSRStep) && (inputs&3)==3) return false;
    if(k!=CircuitKind::NorSR && k!=CircuitKind::ClassicSR) return outputs==referenceOutput(k,inputs);
    const bool s=inputs&1,r=inputs&2,q=outputs&1,qb=outputs&2;
    return q==!(r || qb) && qb==!(s || q);
}
CircuitReport verifyCircuit(const Circuit& c,std::stop_token stop) {
    CircuitReport report; report.passed=true;
    for(unsigned inputs=0;inputs<(1u<<c.inputs.size());++inputs) for(unsigned outputs=0;outputs<(1u<<c.outputs.size());++outputs) {
        if(stop.stop_requested()) { report.cancelled=true; report.passed=false; return report; }
        auto p=c.problem;
        auto pin=[&](const Port& port,int bit) { if(isClassicCircuit(c.kind)) pinClassicPort(p,port,bit); else pinPort(p,port,bit); };
        for(size_t i=0;i<c.inputs.size();++i) pin(c.inputs[i],(inputs>>i)&1);
        for(size_t i=0;i<c.outputs.size();++i) pin(c.outputs[i],(outputs>>i)&1);
        bool expected=referenceAccepts(c.kind,inputs,outputs); auto r=solve(p,stop);
        report.checks.push_back({inputs,outputs,expected,r.status,r.message});
        report.passed&=r.status==(expected?SolveStatus::Sat:SolveStatus::Unsat) && (!expected || r.modelChecked);
        if(r.status==SolveStatus::Cancelled) { report.cancelled=true; report.passed=false; return report; }
    }
    return report;
}
}
