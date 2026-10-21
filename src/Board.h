#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace ims {
struct Coord {
    int64_t x{}, y{};
    bool operator==(const Coord&) const = default;
};
struct CoordHash { size_t operator()(Coord p) const noexcept; };
struct Bounds { Coord min, max; };
// Absence is blank (no information). Constraint interpretation lives elsewhere.
class Board {
public:
    void set(Coord p, std::string symbol);
    std::string get(Coord p) const;
    void assumeSafe(Coord p); // Input metadata: non-mine, not a revealed zero clue.
    bool safe(Coord p) const { return safes_.contains(p); }
    const std::unordered_set<Coord,CoordHash>& safeCells() const { return safes_; }
    void clearSafeAssumptions() { if(!safes_.empty()) { safes_.clear(); ++revision_; } }
    void clear() { if(count_ || !safes_.empty()) { chunks_.clear(); safes_.clear(); count_ = 0; ++revision_; } }
    size_t size() const { return count_; }
    uint64_t revision() const { return revision_; }
    void visible(Bounds bounds, const std::function<void(Coord, const std::string&)>& visit) const;
    void forEach(const std::function<void(Coord,const std::string&)>& visit) const;
private:
    using Chunk = std::unordered_map<uint16_t, std::string>;
    std::unordered_map<Coord, Chunk, CoordHash> chunks_;
    std::unordered_set<Coord,CoordHash> safes_;
    size_t count_{};
    uint64_t revision_{};
};
bool addChecked(int64_t a, int64_t b, int64_t& result);
int64_t addClamped(int64_t a, int64_t b);
}
