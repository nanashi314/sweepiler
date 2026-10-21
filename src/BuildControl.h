#pragma once
#include <functional>
#include <stop_token>
#include <stdexcept>
#include <string>
namespace ims {
struct BuildProgress {std::string phase;size_t done{},total{};};
struct BuildCancelled:std::runtime_error{BuildCancelled():runtime_error("Compilation cancelled") {}};
class BuildScope {
public:
    BuildScope(std::stop_token stop,std::function<void(BuildProgress)> progress);
    ~BuildScope();
    BuildScope(const BuildScope&)=delete;
private:
    friend void buildCheckpoint(const char*,size_t,size_t);
    std::stop_token stop_;std::function<void(BuildProgress)> progress_;BuildScope* previous_;
};
void buildCheckpoint(const char* phase,size_t done=0,size_t total=0);
}
