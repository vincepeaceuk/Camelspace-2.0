#include "BridgeClient.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <thread>

static int failures = 0;
static void check(bool pass, const char* message) {
    std::cout << (pass ? "PASS " : "FAIL ") << message << std::endl;
    failures += !pass;
}
int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) return 2;
    cs::BridgeClient missing("/nonexistent/helper", argv[2], argv[3]);
    check(!missing.ready(), "missing helper fails without launching anything");
    cs::BridgeClient client(argv[1], argv[2], argv[3]);
    check(client.ready(), "original Intel engine starts from ARM client");
    if (!client.ready()) { std::cerr << client.error() << std::endl; return 1; }
    check(client.parameterCount() == 344, "all 344 original parameters available");
    check(client.parameterName(39) == "MasterVolume", "original parameter identities retained");
    check(client.loadBank(std::string(argv[3]) + "/-Default.fxb", 0), "original FXB bank loads");
    check(client.value(39) > 0.0f, "bank loads actual nonzero program values");
    if(argc==5){client.updateDisplays();std::ofstream metadata(argv[4]);for(int i=0;i<344;++i)metadata<<"PARAM "<<i<<" "<<client.parameterName(i)<<" "<<client.value(i)<<" "<<client.display(i)<<"\n";}
    auto state = client.state();
    check(state.size() > 80000, "original full bank state retained");
    client.setValue(39, 0.37f);
    client.sync();
    check(std::abs(client.value(39) - 0.37f) < 1.0e-6f, "parameter edit reaches original engine");
    check(client.restore(state), "original state restores");
    check(std::abs(client.value(39) - 0.37f) > 0.01f, "state restores original parameter value");
    const auto before = client.state();
    check(!client.restore(std::vector<uint8_t>{1,2,3}), "malformed state rejected before legacy dispatcher");
    check(client.state() == before, "malformed state leaves preset intact");
    check(client.clearPreset(), "Clear Preset calls original initialization method");
    check(client.state()!=before,"Clear Preset initializes original blank program");
    check(client.randomize(),"Randomize calls original CamelSpace algorithm");
    check(client.restore(state),"preset restored after native action checks");
    bool finite = true; double energy = 0;
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        for (int block : {32,256,1024}) {
            check(client.prepare(rate,block), "prepare processing rate/block");
            std::vector<float> l(block),r(block),ol(block),orr(block);
            const float* input[] = {l.data(),r.data()}; float* output[] = {ol.data(),orr.data()};
            cs::Transport transport; transport.bpm = 123; transport.playing = true;
            for (int k=0;k<20;++k) {
                for(int i=0;i<block;++i) l[i]=r[i]=0.1f*std::sin(6.283185307179586*220*(k*block+i)/rate);
                transport.samplePosition=k*block; transport.ppqPosition=transport.samplePosition/rate*transport.bpm/60;
                check(client.process(input,output,block,transport), "bridge processes original stereo audio");
                for (int i=0;i<block;++i) {
                    finite &= std::isfinite(ol[i]) && std::isfinite(orr[i]);
                    energy += double(ol[i])*ol[i]+double(orr[i])*orr[i];
                }
            }
        }
    }
    {
        cs::BridgeClient stopped(argv[1],argv[2],argv[3]);
        check(stopped.ready(),"state-stall fixture starts");
        kill(stopped.processId(),SIGSTOP);
        const auto time=std::chrono::steady_clock::now();
        check(!stopped.restore(state),"stalled helper cannot hang a large state write");
        check(std::chrono::steady_clock::now()-time<std::chrono::milliseconds(500),"state send timeout is bounded");
        kill(stopped.processId(),SIGKILL);
    }
    check(finite && energy > 1.0e-6, "processed audio is finite and audible");
    {
        cs::BridgeClient second(argv[1],argv[2],argv[3]);
        check(second.ready() && second.processId()!=client.processId(), "instances own independent helper processes");
        second.prepare(48000,256);second.setValue(10,0.4f);
        kill(second.processId(),SIGSTOP);
        std::thread stalledDisplay([&]{second.updateDisplays();});
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        float quiet[256]{},a[256]{},b[256]{};
        const float* input[]={quiet,quiet};float* output[]={a,b};
        const auto time=std::chrono::steady_clock::now();
        check(!second.process(input,output,256,{}),"stalled control request gives audio dry fallback");
        check(std::chrono::steady_clock::now()-time<std::chrono::milliseconds(50),"audio never waits on stalled editor/state lock");
        kill(second.processId(),SIGKILL);stalledDisplay.join();
    }
    kill(client.processId(), SIGKILL);
    std::vector<float> z(256),out1(256),out2(256);
    const float* ip[]={z.data(),z.data()}; float* op[]={out1.data(),out2.data()};
    const auto start=std::chrono::steady_clock::now();
    check(!client.process(ip,op,256,{}), "helper death reports failure rather than hanging");
    check(std::chrono::steady_clock::now()-start < std::chrono::milliseconds(500), "helper-death wait is bounded");
    return failures ? 1 : 0;
}
