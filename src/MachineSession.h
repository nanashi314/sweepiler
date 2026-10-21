#pragma once
#include "FrameMachine.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
namespace ims {
// A program the machine can run: a name, how to compile it, and its source
// (empty for circuits generated in C++, such as Tetris).
enum class ProgramLanguage {Generated,MineC,Imsg};
struct MachineProgram {std::string label; std::function<FrameCircuit()> compile; std::string source; ProgramLanguage language{ProgramLanguage::Generated};};
// The sample programs (the MineC examples).
std::vector<MachineProgram> machinePrograms();
// A .mc (MineC) or .imsg program file.
FrameCircuit compileProgramFile(const std::filesystem::path& path);
// Source text in a language (throws std::invalid_argument with "line N: ..." on errors).
FrameCircuit compileProgramText(const std::string& source,ProgramLanguage language,const std::string& name);

// One interactive machine without any UI: building the frame layout and every
// tick run on worker threads. A UI reads status() and the board through
// withMachine(), and feeds keys with setKey()/pressKey(). Every display it
// shows comes from the newest frame the board forced.
class MachineSession {
public:
    explicit MachineSession(std::filesystem::path cacheDir);
    ~MachineSession();
    MachineSession(const MachineSession&)=delete;
    // Lays out (or loads) the frame for the circuit in the background.
    void build(FrameCircuit circuit);
    void cancel();
    void setPlaying(bool on) {playing_=on;}
    void step() {stepOnce_=true;}
    void reset() {playing_=false; resetRequest_=true;}
    void setInterval(int ms) {intervalMs_=ms;}
    // Hold keys count while held or if pressed since the last tick; press keys once per press.
    void setKey(size_t key,bool held);
    void pressKey(size_t key);
    struct Status {
        bool building{},ready{},playing{};
        std::string name,progress,error;
        int64_t ticks{};
        double solveMs{};
        int intervalMs{};
        int frameWidth{},frameHeight{};
        size_t gates{};
        std::vector<std::string> keys;
        std::vector<uint8_t> keyPress;      // 1: counts once per press
        int displayWidth{},displayHeight{};
        std::vector<uint8_t> display;       // row-major, from the newest forced frame
        std::vector<uint8_t> lastKeys;      // keys given to the newest frame
    };
    Status status() const;
    // Calls f with the machine under the session lock (nothing if not ready).
    void withMachine(const std::function<void(const FrameMachine&)>& f) const;
    static constexpr size_t maxKeys=8;
#ifdef __EMSCRIPTEN__
    static constexpr size_t keptFrameBytes=size_t{1}<<27;   // in the browser, everything shares 4 GB of address space
#else
    static constexpr size_t keptFrameBytes=size_t{1}<<30;   // memory for past frames on the board
#endif
private:
    void tickLoop(std::stop_token stop);
    std::vector<uint8_t> takeKeys();
    std::filesystem::path cacheDir_;
    mutable std::mutex mutex_;
    std::shared_ptr<FrameMachine> machine_;
    std::jthread builder_,ticker_;
    std::string name_,progress_,error_;
    std::vector<std::string> keyNames_;
    std::vector<uint8_t> keyPress_,shown_,lastKeys_;
    double solveMs_{};
    std::atomic<bool> building_{false},playing_{false},stepOnce_{false},resetRequest_{false};
    std::atomic<int> intervalMs_{350};
    std::array<std::atomic<bool>,maxKeys> held_{},pressed_{};
};
}
