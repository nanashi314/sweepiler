#pragma once
#include "FrameCircuit.h"
#include "GridLayout.h"
#include <filesystem>
#include <functional>
#include <optional>
namespace ims {
// One tick of a FrameCircuit as an ordinary Minesweeper frame. State enters on
// the left port column and leaves on the right port column at the same rows,
// so frame t+1 placed `stride` cells to the right shares that column with
// frame t. Keys enter on the top edge; display cells leave on the bottom edge.
struct MachineFrame {
    FrameCircuit circuit;
    CellGrid grid;            // full template, in the frame's own coordinates
    GridInfo info;
    int64_t left{},right{},stride{};
    std::vector<Coord> stateIn,stateOut,keys,display;
};
struct FrameOptions {int pitchX{40},pitchY{32},spread{6};};
MachineFrame layoutFrame(const FrameCircuit& circuit,FrameOptions options={});
// Laid-out frames are cached on disk: routing can take minutes. The key covers
// the format, spacing and the whole netlist, so a changed program, circuit or
// router never reuses an old layout.
uint64_t frameKey(const FrameCircuit& circuit,const FrameOptions& options);
void saveFrame(const std::filesystem::path& path,const MachineFrame& frame,const FrameOptions& options);
std::optional<MachineFrame> loadFrame(const std::filesystem::path& path,const FrameCircuit& circuit,const FrameOptions& options);
MachineFrame obtainFrame(const FrameCircuit& circuit,const FrameOptions& options,const std::filesystem::path& cacheDir);
// Called by obtainFrame when the cache has no usable file, before laying the
// frame out: it may put a prebuilt file at that path (the web build downloads
// one) and return true. Unset in the native app.
extern std::function<bool(const std::filesystem::path&)> fetchMissingFrame;
// Placement: from the netlist's spatial hints, or in creation order on a near-square grid.
std::vector<Coord> placeNorGates(const NorNetlist& net,const FrameOptions& options,Coord origin);
std::vector<Coord> placeInOrder(size_t gates,const FrameOptions& options,Coord origin);
std::vector<Coord> placeAnnealed(const FrameCircuit& circuit,const FrameOptions& options,Coord origin);
}
