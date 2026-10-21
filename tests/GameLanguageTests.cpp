#include "GameLanguage.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace ims;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
size_t checks{};
// The compiled NOR netlist and the integer interpreter must agree on every
// next-state bit and pixel.
void agree(const std::string& source,const std::string& name,size_t ticks,bool randomStates,unsigned seed) {
    auto program=parseGame(source); auto circuit=compileGame(source,name); GameReference ref(program);
    std::mt19937_64 rng(seed); auto state=ref.initial();
    require(packGameState(program,state)==circuit.initial,name+": initial state");
    for(size_t t=0;t<ticks;++t) {
        if(randomStates) for(size_t i=0;i<state.size();++i) state[i]=rng()&(program.state[i].width>=64?~0ull:(1ull<<program.state[i].width)-1);
        std::vector<uint8_t> keys; for(size_t k=0;k<program.keys.size();++k) keys.push_back(static_cast<uint8_t>(rng()%3==0));
        auto bits=packGameState(program,state); bits.insert(bits.end(),keys.begin(),keys.end());
        auto signals=circuit.net.evaluate(bits);
        std::vector<uint8_t> display; auto next=ref.step(state,keys,&display);
        auto expected=packGameState(program,next); expected.insert(expected.end(),display.begin(),display.end());
        require(expected.size()==circuit.net.outputs.size(),name+": output count");
        for(size_t o=0;o<expected.size();++o) require(circuit.net.value(signals,circuit.net.outputs[o].second)==expected[o],name+": "+circuit.net.outputs[o].first+" differs at tick "+std::to_string(t));
        checks+=expected.size(); state=next;
    }
    std::cout<<name<<": "<<circuit.net.gates.size()<<" NOR gates, "<<circuit.stateBits<<" state bits, "<<ticks<<" ticks agree"<<std::endl;
}
int main(){try{
    for(int i=0;gameExample(i);++i) agree(gameExample(i),gameExampleName(i),3000,false,static_cast<unsigned>(i+1));
    // Every operator, variable indices, nested if/elif/else, lets, loops, wrap-around and constants.
    const char* operators=R"(
display 4 2
input K
input J press
state a 5 = 7
state b 3 = 5
state c 8
state d 1
state e 6
tick
  let s 6 = a + b
  c = (a << 2) - b + 200
  if K and a > b
    e = s ^ (c >> 1)
  elif a == 3 or not J
    e = ~e & 0x2A
  else
    e = e - 1
  end
  for i 0 3
    if c[i + b] != d
      a[i] = c[i] | J
    end
  end
  d = (a < c) and (b >= 2) and (e <= 40) and (c != 255)
  b = b + 7
end
screen
  for i 0 3
    pixel i 0 = a[i]
    pixel i 1 = (c >> (i + 1)) & 1
  end
  if d
    pixel 0 0 = 0
  end
end
)";
    agree(operators,"operators",3000,true,99);
    // Errors carry line numbers.
    auto rejects=[&](const char* source,const std::string& part){
        try {compileGame(source); } catch(const std::invalid_argument& e) {require(std::string(e.what()).find(part)!=std::string::npos,std::string("Wrong error: ")+e.what()); ++checks; return;}
        throw std::runtime_error("Accepted a bad program: "+part);
    };
    rejects("display 2 2\ninput K\nstate s 2\ntick\n  s = t\nend\n","unknown name 't'");
    rejects("display 2 2\ninput K\nstate s 2\ntick\n  K = 1\nend\n","inputs cannot be assigned");
    rejects("display 2 2\ninput K\nstate s 2\nscreen\n  pixel 5 0 = 1\nend\n","pixel outside");
    rejects("display 2 2\ninput K\nstate s 2\ntick\n  s = s << K\nend\n","shift amounts must be constant");
    rejects("display 2 2\ninput K\nstate s 2\ntick\n  if K\n  s = 1\n","'end' expected");
    rejects("display 2 2\nstate s 2\n","1..6 inputs");
    std::cout<<"PASS: "<<checks<<" compiled-netlist checks against the reference interpreter\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
