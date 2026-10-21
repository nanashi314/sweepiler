#pragma once
#include "FrameCircuit.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace ims {
// MineC: a C-like language for machines on chained Minesweeper frames.
//
//   #display 16 16                  screen size (1..32 x 1..32)
//   input hold left;                key read as bool; press = once per press
//   const int N = 6;                compile-time constant
//   uint4 x = 3;                    globals are the state kept between ticks
//   int8 v[4] = {1, 2, 3, 4};       global arrays
//   int8 clamp(int8 a) {...}        functions (inlined, no recursion)
//   void tick() {...}               runs once per tick
//   void draw() {...}               sets pixels with pixel(x, y, value)
//
// Types: bool, uintN, intN (N = 1..64, two's complement), int (= int32).
// Statements: declarations, = += -= *= /= %= &= |= ^= <<= >>=, ++, --,
// if/else, for with compile-time bounds (unrolled), break, continue,
// return, blocks, function calls, pixel(x, y, v), at(x, y).
// Expressions: C operators except assignment, comma and short-circuit
// (both sides of && and || are evaluated), casts (uint8)e, ?:.
// Arithmetic wraps at the operands' common width; integer literals take the
// other operand's type. Division by zero gives all ones (and the remainder
// is the dividend), as unsigned hardware dividers do.
struct MineCType {int width{32};bool sign{true};};
struct MineCExpr;
struct MineCStmt;
struct MineCProgram {
    struct Key {std::string name;bool press{};};
    struct Global {std::string name;MineCType type;int count{};std::vector<int64_t> init;int line{};}; // count 0: scalar
    struct Function {std::string name;bool returns{};MineCType type;std::vector<std::pair<std::string,MineCType>> params;std::shared_ptr<MineCStmt> body;int line{};};
    int width{},height{};
    std::vector<Key> keys;
    std::vector<Global> globals;
    std::map<std::string,int64_t> constants;
    std::map<std::string,Function> functions;
};
MineCProgram parseMineC(const std::string& source);
FrameCircuit compileMineC(const std::string& source,const std::string& name="program");
// Independent interpreter of the same program with 64-bit integers
// (verification only). State: one value per scalar or array element, in
// declaration order.
class MineCReference {
public:
    explicit MineCReference(MineCProgram program);
    std::vector<int64_t> initial() const;
    std::vector<int64_t> step(const std::vector<int64_t>& state,const std::vector<uint8_t>& keys,std::vector<uint8_t>* display=nullptr) const;
    std::vector<uint8_t> pack(const std::vector<int64_t>& state) const;
    std::vector<int64_t> unpack(const std::vector<uint8_t>& bits) const;
    const MineCProgram& program() const {return program_;}
private:
    MineCProgram program_;
};
const char* mineCExample(int index);   // nullptr past the last example
const char* mineCExampleName(int index);
}
