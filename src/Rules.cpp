#include "Rules.h"
#include <algorithm>
#include <unordered_set>
namespace ims {
std::string RuleTable::validate() const {
    if(symbols.empty() || symbols.size()>512) return "Alphabet must contain 1..512 symbols";
    if(counts.size()!=symbols.size()) return "Rule table row count mismatch";
    std::unordered_set<std::string> unique;
    for(size_t r=0;r<symbols.size();++r) {
        if(symbols[r].empty() || !unique.insert(symbols[r]).second) return "Symbols must be nonempty and unique";
        if(counts[r].size()!=symbols.size()) return "Rule table column count mismatch";
        for(int n:counts[r]) if(n < -1 || n > 8) return "Rule entries must be '?' or 0..8";
    }
    // Impossible but well-formed rows remain constraints: they forbid that source label.
    return {};
}
int RuleTable::index(const std::string& s) const {
    auto i=std::find(symbols.begin(),symbols.end(),s);
    return i==symbols.end() ? -1 : static_cast<int>(i-symbols.begin());
}
RuleTable RuleTable::example1() { return {{"A","B"},{{-1,-1},{-1,4}}}; }
RuleTable RuleTable::classic() {
    RuleTable t; for(int n=0;n<9;++n) t.symbols.push_back(std::to_string(n)); t.symbols.push_back("*");
    t.counts.assign(10,std::vector<int>(10,-1));
    for(int n=0;n<9;++n) t.counts[n][9]=n;
    return t;
}
}
