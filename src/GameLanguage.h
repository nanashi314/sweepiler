#pragma once
#include "FrameCircuit.h"
#include <memory>
#include <string>
#include <vector>
namespace ims {
// .imsg: a small language for interactive machines on chained frames.
//
//   display W H                 screen size in pixels (1..16 x 1..32)
//   input NAME [hold|press]     a key; press counts once per key press
//   state NAME WIDTH [= VALUE]  unsigned state, 1..64 bits, kept between ticks
//   tick ... end                runs once per tick on the state
//   screen ... end              sets pixels from the state after the tick
//
// Statements (one per line): NAME = expr, NAME[expr] = expr,
// let NAME WIDTH = expr, if expr / elif expr / else / end,
// for NAME FIRST LAST ... end (constant bounds, unrolled),
// pixel X Y = expr (screen only), at X Y (placement hint).
// Expressions: integers, names, NAME[expr], ( ), + - & | ^ ~ << >>
// (constant shift), == != < <= > >=, and/or/not. Arithmetic wraps at the
// wider operand's width; comparisons and logic give one bit.
struct GameNode;
struct GameProgram {
    struct Variable {std::string name;int width{};uint64_t initial{};};
    struct Key {std::string name;bool press{};};
    int width{},height{};
    std::vector<Key> keys;
    std::vector<Variable> state;
    std::vector<std::shared_ptr<GameNode>> tick,screen;
};
GameProgram parseGame(const std::string& source);
FrameCircuit compileGame(const std::string& source,const std::string& name="program");
// Independent integer interpreter of the same program (verification only).
struct GameReference {
    explicit GameReference(GameProgram program);
    // state: one value per state variable; keys: one bit per key.
    std::vector<uint64_t> step(const std::vector<uint64_t>& state,const std::vector<uint8_t>& keys,std::vector<uint8_t>* display=nullptr) const;
    std::vector<uint64_t> initial() const;
    const GameProgram& program() const {return program_;}
private:
    GameProgram program_;
};
std::vector<uint8_t> packGameState(const GameProgram& p,const std::vector<uint64_t>& values);
std::vector<uint64_t> unpackGameState(const GameProgram& p,const std::vector<uint8_t>& bits);
const char* gameExample(int index);   // nullptr past the last example
const char* gameExampleName(int index);
}
