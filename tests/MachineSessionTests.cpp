#include "MachineSession.h"
#include "MineC.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace ims;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
// Waits until pred holds for the session status, or fails after the timeout.
template<class P> MachineSession::Status waitFor(const MachineSession& s,P pred,int seconds,const char* what) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
    for(;;) {
        auto st=s.status();
        if(!st.error.empty()) throw std::runtime_error(std::string(what)+": "+st.error);
        if(pred(st)) return st;
        if(std::chrono::steady_clock::now()>end) throw std::runtime_error(std::string("Timed out: ")+what);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
int main(){try{
    // The samples are the MineC examples; Catch is small enough to lay out quickly.
    auto programs=machinePrograms();
    require(programs.size()==19 && programs[0].label=="Blink","Program list");
    for(const auto& p:programs) require(p.language==ProgramLanguage::MineC && !p.source.empty(),"Sample "+p.label+" is not MineC");
    MachineSession session(std::filesystem::current_path()/"frame-cache");
    const auto& catchProgram=*std::find_if(programs.begin(),programs.end(),[](const auto& p){return p.label=="Catch";});
    session.build(catchProgram.compile());
    auto st=waitFor(session,[](const auto& s){return s.ready;},600,"build");
    require(st.keys.size()==2 && st.displayWidth==6 && st.displayHeight==8,"Catch ports");
    // Keys given through the session reach the board; every display matches the reference.
    MineCReference ref(parseMineC(catchProgram.source)); auto state=ref.initial();
    for(int t=0;t<24;++t) {
        size_t key=static_cast<size_t>(t%3);
        if(key<2) session.pressKey(key);
        session.step();
        st=waitFor(session,[t](const auto& s){return s.ticks==t+1;},60,"tick");
        require(st.lastKeys.size()==2,"Keys of the tick");
        if(key<2) require(st.lastKeys[key]==1,"Pressed key did not reach the frame");
        std::vector<uint8_t> expected;
        state=ref.step(state,st.lastKeys,&expected);
        require(st.display==expected,"Display differs from the reference at tick "+std::to_string(t));
    }
    // Every past frame stays on the board; each frame's key cells show that tick's keys.
    session.withMachine([&](const FrameMachine& m){
        require(m.frames().size()==24,"Past frames were dropped");
        for(const auto& fr:m.frames()) for(size_t k=0;k<fr.keys.size();++k) {
            Coord c=m.frame().keys[k]; c.x+=m.frameX(fr.index);
            auto cell=m.cell(c);
            require(cell==(fr.keys[k]?FrameMachine::Cell::InputMine:FrameMachine::Cell::InputSafe),"Key cell of frame "+std::to_string(fr.index)+" wrong");
        }
    });
    // The board is readable while the session runs, and reset starts over.
    int64_t cells=0;
    session.withMachine([&](const FrameMachine& m){auto b=m.bounds(); for(int64_t x=b.min.x;x<=b.max.x;x+=7) if(m.cell({x,(b.min.y+b.max.y)/2})!=FrameMachine::Cell::Outside) ++cells;});
    require(cells>0,"Board not readable");
    session.setInterval(60); session.setPlaying(true);
    waitFor(session,[](const auto& s){return s.ticks>=30;},120,"play");
    session.reset();
    waitFor(session,[](const auto& s){return s.ticks==0 && !s.playing;},30,"reset");
    std::cout<<"PASS: session built Catch, 24 key ticks equal the reference, past frames kept, play and reset work"<<std::endl;
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
