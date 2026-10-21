#include "Board.h"
#include <limits>
#include <algorithm>

namespace ims {
size_t CoordHash::operator()(Coord p) const noexcept {
    auto mix = [](uint64_t x) { x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL; x ^= x >> 27; x *= 0x94d049bb133111ebULL; return x ^ (x >> 31); };
    return static_cast<size_t>(mix(static_cast<uint64_t>(p.x)) ^ (mix(static_cast<uint64_t>(p.y)) >> 1));
}
bool addChecked(int64_t a, int64_t b, int64_t& r) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
    r = a + b; return true;
}
int64_t addClamped(int64_t a, int64_t b) {
    int64_t r; return addChecked(a,b,r) ? r : (b > 0 ? INT64_MAX : INT64_MIN);
}
static int64_t chunkAxis(int64_t v) { return v / 32 - (v % 32 < 0 ? 1 : 0); }
static Coord chunkCoord(Coord p) { return {chunkAxis(p.x), chunkAxis(p.y)}; }
static uint16_t localKey(Coord p) {
    return static_cast<uint16_t>(((p.y % 32 + 32) % 32) * 32 + (p.x % 32 + 32) % 32);
}
void Board::set(Coord p, std::string s) {
    revision_+=safes_.erase(p);
    auto c = chunkCoord(p); auto k = localKey(p);
    if (s.empty()) {
        auto it = chunks_.find(c); if (it == chunks_.end()) return;
        auto removed=it->second.erase(k); count_ -= removed; revision_ += removed; if (it->second.empty()) chunks_.erase(it);
    } else {
        auto& chunk = chunks_[c]; auto it=chunk.find(k);
        if(it!=chunk.end() && it->second==s) return;
        if(it==chunk.end()) ++count_; chunk[k] = std::move(s); ++revision_;
    }
}
void Board::assumeSafe(Coord p) {
    if(safes_.contains(p)) return;
    set(p,""); safes_.insert(p); ++revision_;
}
std::string Board::get(Coord p) const {
    auto it = chunks_.find(chunkCoord(p)); if (it == chunks_.end()) return {};
    auto s = it->second.find(localKey(p)); return s == it->second.end() ? "" : s->second;
}
void Board::visible(Bounds b, const std::function<void(Coord, const std::string&)>& visit) const {
    if (b.min.x > b.max.x || b.min.y > b.max.y) return;
    auto lo = chunkCoord(b.min), hi = chunkCoord(b.max);
    for (auto y = lo.y; y <= hi.y; ++y) for (auto x = lo.x; x <= hi.x; ++x) {
        auto it = chunks_.find({x,y}); if (it == chunks_.end()) continue;
        for (const auto& [key,s] : it->second) {
            Coord p{x * 32 + key % 32, y * 32 + key / 32};
            if (p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y) visit(p,s);
        }
    }
}
void Board::forEach(const std::function<void(Coord,const std::string&)>& visit) const {
    for(const auto& [chunk,cells]:chunks_) for(const auto& [key,s]:cells)
        visit({chunk.x*32+key%32,chunk.y*32+key/32},s);
}
}
