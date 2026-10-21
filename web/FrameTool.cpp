// frame_tool: lays out sample frames into a frame cache directory (the files
// obtainFrame reads) and times a few ticks of each. Built for Node with the
// web build (NODERAWFS), it measures the core in WebAssembly and makes the
// caches the page loads instead of routing in the browser. Each cache file
// also gets a gzipped copy (NAME.frame.gz), which is what the page fetches:
// a frame compresses about twenty times.
//
//   node frame_tool.js OUTDIR [--ticks=N] [NAME ...]     (no NAME: every sample)
#include "FrameMachine.h"
#include "MachineSession.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if __has_include(<zlib.h>)
#include <fstream>
#include <iterator>
#include <zlib.h>
// Writes path.gz unless it is already newer than path.
static void gzipCopy(const std::filesystem::path& path) {
    auto gz=path; gz+=".gz";
    if(std::filesystem::exists(gz) && std::filesystem::last_write_time(gz)>=std::filesystem::last_write_time(path)) return;
    std::ifstream in(path,std::ios::binary); std::string bytes{std::istreambuf_iterator<char>(in),{}};
    gzFile f=gzopen(gz.string().c_str(),"wb9"); if(!f) throw std::runtime_error("Cannot write "+gz.string());
    gzwrite(f,bytes.data(),static_cast<unsigned>(bytes.size())); gzclose(f);
}
#endif
#ifdef __EMSCRIPTEN__
static double memoryMB() {return static_cast<double>(__builtin_wasm_memory_size(0))*65536/1048576;}
#else
static double memoryMB() {return 0;}
#endif
int main(int argc,char** argv) {
    if(argc<2) {std::fprintf(stderr,"usage: frame_tool OUTDIR [--ticks=N] [NAME ...]\n"); return 2;}
    const std::filesystem::path out=argv[1];
    int ticks=20; std::vector<std::string> names;
    for(int i=2;i<argc;++i) {if(std::strncmp(argv[i],"--ticks=",8)==0) ticks=std::atoi(argv[i]+8); else names.push_back(argv[i]);}
    using Clock=std::chrono::steady_clock;
    auto ms=[](Clock::time_point a){return std::chrono::duration<double,std::milli>(Clock::now()-a).count();};
    int failures=0;
    for(const auto& p:ims::machinePrograms()) {
        if(!names.empty() && std::find(names.begin(),names.end(),p.label)==names.end()) continue;
        try {
            auto t0=Clock::now(); auto circuit=p.compile(); double compileMs=ms(t0);
            t0=Clock::now(); auto frame=std::make_shared<ims::MachineFrame>(ims::obtainFrame(circuit,ims::FrameOptions{},out)); double frameMs=ms(t0);
            t0=Clock::now(); ims::FrameMachine machine(frame,4); double indexMs=ms(t0);
            double worst=0,total=0; std::vector<uint8_t> keys(circuit.keys.size());
            for(int t=0;t<ticks;++t) {
                for(size_t k=0;k<keys.size();++k) keys[k]=static_cast<uint8_t>((t*7+static_cast<int>(k))%3==0);
                auto s=Clock::now(); machine.step(keys); double d=ms(s); worst=std::max(worst,d); total+=d;
            }
            std::printf("%-14s NOR %6zu  frame %5dx%-5d (%6.1f Mcells)  compile %7.0f ms  layout/load %8.0f ms  index %6.0f ms  tick avg %7.1f ms max %7.1f ms  memory %6.0f MB\n",
                p.label.c_str(),circuit.net.gates.size(),frame->grid.width,frame->grid.height,static_cast<double>(frame->grid.cells.size())/1e6,
                compileMs,frameMs,indexMs,ticks?total/ticks:0.0,worst,memoryMB());
        } catch(const std::exception& e) {std::printf("%-14s FAIL: %s\n",p.label.c_str(),e.what()); ++failures;}
        std::fflush(stdout);
    }
#if __has_include(<zlib.h>)
    for(const auto& e:std::filesystem::directory_iterator(out)) if(e.path().extension()==".frame") gzipCopy(e.path());
#endif
    return failures?1:0;
}
