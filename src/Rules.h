#pragma once
#include <string>
#include <vector>
namespace ims {
// n[source][target] = exact number among the 8 Moore neighbours; -1 = '?'.
struct RuleTable {
    std::vector<std::string> symbols;
    std::vector<std::vector<int>> counts;
    std::string validate() const;
    int index(const std::string& symbol) const;
    static RuleTable example1();
    static RuleTable classic();
};
}
