#include "MachineSession.h"
#include "BuildControl.h"
#include "GameLanguage.h"
#include "MineC.h"
#include <chrono>
#include <fstream>
#include <sstream>
namespace ims {
std::vector<MachineProgram> machinePrograms() {
    // The sample programs, all written in MineC (examples/*.mc).
    std::vector<MachineProgram> list;
    for(int i=0;mineCExample(i);++i) list.push_back({mineCExampleName(i),[i]{return compileMineC(mineCExample(i),mineCExampleName(i));},mineCExample(i),ProgramLanguage::MineC});
    return list;
}
FrameCircuit compileProgramText(const std::string& source,ProgramLanguage language,const std::string& name) {
    if(language==ProgramLanguage::Imsg) return compileGame(source,name);
    if(language==ProgramLanguage::MineC) return compileMineC(source,name);
    throw std::invalid_argument("This program is generated in C++ and has no source");
}
FrameCircuit compileProgramFile(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary); if(!in) throw std::runtime_error("Cannot open "+path.string());
    std::stringstream text; text<<in.rdbuf(); auto name=path.stem().string();
    return path.extension()==".imsg"?compileGame(text.str(),name):compileMineC(text.str(),name);
}
MachineSession::MachineSession(std::filesystem::path cacheDir):cacheDir_(std::move(cacheDir)) {
    ticker_=std::jthread([this](std::stop_token stop){tickLoop(stop);});
}
MachineSession::~MachineSession() {
    playing_=false;
    ticker_.request_stop(); if(ticker_.joinable()) ticker_.join();
    builder_.request_stop(); if(builder_.joinable()) builder_.join();
}
void MachineSession::build(FrameCircuit circuit) {
    if(building_) return;
    playing_=false;
    if(builder_.joinable()) builder_.join();
    {
        std::lock_guard lock(mutex_);
        machine_.reset(); error_.clear(); progress_="Preparing frame"; shown_.clear(); lastKeys_.clear(); solveMs_=0;
        name_=circuit.name; keyNames_=circuit.keys; keyPress_=circuit.keyPress;
    }
    for(size_t i=0;i<maxKeys;++i) {held_[i]=false; pressed_[i]=false;}
    building_=true;
    builder_=std::jthread([this,circuit=std::move(circuit)](std::stop_token stop) {
        try {
            BuildScope scope(stop,[this](BuildProgress p){std::lock_guard lock(mutex_); progress_=p.phase+(p.total?" "+std::to_string(p.done)+"/"+std::to_string(p.total):"");});
            auto frame=std::make_shared<MachineFrame>(obtainFrame(circuit,FrameOptions{},cacheDir_));
            // Past frames stay on the board within a memory budget.
            auto machine=std::make_shared<FrameMachine>(frame,0);
            machine->setKeep(std::max<size_t>(4,keptFrameBytes/std::max<size_t>(1,machine->frameBytes())));
            std::lock_guard lock(mutex_); machine_=std::move(machine); progress_.clear();
        } catch(const BuildCancelled&) {std::lock_guard lock(mutex_); progress_="Cancelled";}
          catch(const std::exception& e) {std::lock_guard lock(mutex_); error_=e.what(); progress_.clear();}
        building_=false;
    });
}
void MachineSession::cancel() {if(builder_.joinable()) builder_.request_stop();}
void MachineSession::setKey(size_t key,bool held) {if(key<maxKeys) {if(held && !held_[key]) pressed_[key]=true; held_[key]=held;}}
void MachineSession::pressKey(size_t key) {if(key<maxKeys) pressed_[key]=true;}
std::vector<uint8_t> MachineSession::takeKeys() {
    std::vector<uint8_t> keys(keyNames_.size());
    for(size_t i=0;i<keys.size() && i<maxKeys;++i) {
        bool pressed=pressed_[i].exchange(false);
        keys[i]=keyPress_[i]?pressed:(pressed || held_[i]);
    }
    return keys;
}
void MachineSession::tickLoop(std::stop_token stop) {
    auto next=std::chrono::steady_clock::now();
    while(!stop.stop_requested()) {
        bool ready; {std::lock_guard lock(mutex_); ready=machine_!=nullptr; if(ready && resetRequest_.exchange(false)) {machine_->reset(); shown_.clear(); lastKeys_.clear();}}
        bool go=ready && (playing_ || stepOnce_.exchange(false));
        if(!go) {std::this_thread::sleep_for(std::chrono::milliseconds(10)); next=std::chrono::steady_clock::now(); continue;}
        auto now=std::chrono::steady_clock::now();
        if(playing_ && now<next) {std::this_thread::sleep_for(std::min(next-now,std::chrono::steady_clock::duration(std::chrono::milliseconds(10)))); continue;}
        next=now+std::chrono::milliseconds(intervalMs_.load());
        try {
            std::vector<uint8_t> keys; std::shared_ptr<FrameMachine> machine;
            {std::lock_guard lock(mutex_); keys=takeKeys(); machine=machine_;}
            if(!machine) continue;
            // Only this thread commits, so computing outside the lock is safe.
            auto frame=machine->compute(keys);
            std::lock_guard lock(mutex_);
            // A rebuild or reset meanwhile makes the result stale.
            if(machine_!=machine || frame.index!=machine->ticks()) continue;
            solveMs_=frame.solveMs; lastKeys_=keys;
            machine_->commit(std::move(frame));
            shown_=machine_->display();
        } catch(const std::exception& e) {std::lock_guard lock(mutex_); error_=e.what(); playing_=false;}
    }
}
MachineSession::Status MachineSession::status() const {
    std::lock_guard lock(mutex_); Status s;
    s.building=building_; s.ready=machine_!=nullptr && !building_; s.playing=playing_;
    s.name=name_; s.progress=progress_; s.error=error_; s.solveMs=solveMs_; s.intervalMs=intervalMs_;
    s.keys=keyNames_; s.keyPress=keyPress_; s.lastKeys=lastKeys_;
    if(machine_) {
        const auto& f=machine_->frame();
        s.ticks=machine_->ticks(); s.frameWidth=f.grid.width; s.frameHeight=f.grid.height; s.gates=f.circuit.net.gates.size();
        s.displayWidth=f.circuit.displayWidth; s.displayHeight=f.circuit.displayHeight;
        s.display=shown_.empty()?std::vector<uint8_t>(static_cast<size_t>(s.displayWidth*s.displayHeight),0):shown_;
    }
    return s;
}
void MachineSession::withMachine(const std::function<void(const FrameMachine&)>& f) const {
    std::lock_guard lock(mutex_); if(machine_) f(*machine_);
}
}
