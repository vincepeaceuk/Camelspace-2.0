#import <Cocoa/Cocoa.h>
#include "BridgeClient.h"
#include "LegacyABI.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <thread>

namespace {
cs::TimeInfo timing;
double rate=44100;
int blockSize=256;
std::string directory;
intptr_t host(cs::Effect*,int32_t opcode,int32_t,intptr_t,void* ptr,float) {
    switch(opcode) {
        case 0: return 0;
        case 1: return 2400;
        case 2: return 0x43615331;
        case 7: return reinterpret_cast<intptr_t>(&timing);
        case 15: return 1;
        case 16: return intptr_t(rate);
        case 17: return blockSize;
        case 23: return 2;
        case 32: if(ptr)std::strcpy(static_cast<char*>(ptr),"CamelSpace Revival");return 1;
        case 33: if(ptr)std::strcpy(static_cast<char*>(ptr),"Camel Space 2");return 1;
        case 34: return 100;
        case 37: return ptr && (std::strcmp(static_cast<char*>(ptr),"sendVstTimeInfo")==0 || std::strcmp(static_cast<char*>(ptr),"sendVstMidiEvent")==0);
        case 38: return 1;
        case 41: return reinterpret_cast<intptr_t>(directory.c_str());
        case 42: case 43: case 44: return 1;
        default: return 0;
    }
}
struct Original {
    cs::Effect* e=nullptr;
    void* library=nullptr;
    bool active=false;
    explicit Original(const std::string& bundlePath) {
        NSBundle* bundle=[NSBundle bundleWithPath:[NSString stringWithUTF8String:bundlePath.c_str()]];
        [bundle load];
        library=dlopen((bundlePath+"/Contents/MacOS/CamelSpace").c_str(),RTLD_LOCAL|RTLD_NOW);
        if(!library) throw std::runtime_error(dlerror());
        auto entry=reinterpret_cast<cs::Effect*(*)(cs::HostCallback)>(dlsym(library,"VSTPluginMain"));
        e=entry?entry(host):nullptr;
        if(!e || e->magic!=0x56737450 || !e->replace) throw std::runtime_error("Original VST entry failed");
        e->dispatch(e,0,0,0,nullptr,0);
    }
    ~Original() {
        if(!e)return;
        if(active) {e->dispatch(e,72,0,0,nullptr,0);e->dispatch(e,12,0,0,nullptr,0);}
        e->dispatch(e,1,0,0,nullptr,0);
    }
    void restore(const std::vector<uint8_t>& chunk,int slot) {
        e->dispatch(e,24,0,chunk.size(),const_cast<uint8_t*>(chunk.data()),0);
        e->dispatch(e,2,0,slot,nullptr,0);
    }
    void prepare(double newRate,int newBlock) {
        if(active)e->dispatch(e,12,0,0,nullptr,0);
        rate=newRate;blockSize=newBlock;timing.sampleRate=rate;
        e->dispatch(e,10,0,0,nullptr,float(rate));
        e->dispatch(e,11,0,blockSize,nullptr,0);
        e->dispatch(e,12,0,1,nullptr,0);
        e->dispatch(e,71,0,0,nullptr,0);
        active=true;
    }
    std::vector<uint8_t> state() {
        void* bytes=nullptr;
        auto size=e->dispatch(e,23,0,0,&bytes,0);
        if(size<1 || size>intptr_t(cs::kMaxState) || !bytes) return {};
        auto* first=static_cast<uint8_t*>(bytes);
        return {first,first+size};
    }
    void process(float** input,float** output,int frames,const cs::Transport& t) {
        timing.samplePosition=t.samplePosition;timing.ppqPosition=t.ppqPosition;
        timing.tempo=std::isfinite(t.bpm) && t.bpm>0 ? t.bpm : 120;
        timing.numerator=t.numerator>0?t.numerator:4;timing.denominator=t.denominator>0?t.denominator:4;
        timing.cycleStart=t.cycleStart;timing.cycleEnd=t.cycleEnd;
        timing.flags=(1<<9)|(1<<10)|(1<<11)|(1<<12)|(1<<13)|(t.playing?2:0)|(t.cycling?4:0);
        timing.barStart=std::floor(t.ppqPosition/4.0)*4;
        e->replace(e,input,output,frames);
    }
};
struct Bank {
    std::string name;
    std::vector<uint8_t> chunk;
    std::vector<std::string> names;
};
Bank readBank(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    if(bytes.size()<160)throw std::runtime_error("Truncated bank");
    Bank bank{path.filename().string(),{bytes.begin()+160,bytes.end()},{}};
    if(!cs::validChunk(bank.chunk))throw std::runtime_error("Invalid original bank");
    size_t cursor=0;
    for(int i=0;i<64;++i) {
        cursor+=4;
        const auto start=cursor;
        while(bank.chunk[cursor])++cursor;
        bank.names.emplace_back(reinterpret_cast<const char*>(bank.chunk.data()+start),cursor-start);
        cursor+=1+4+cs::kParameters*4;
    }
    return bank;
}
struct Result {
    uint64_t samples=0,different=0;
    uint64_t mutatedInputSamples=0;
    double maxDifference=0,energy=0;
    bool finite=true;
    int firstBlock=-1,firstChannel=-1,firstFrame=-1;
    float direct=0,bridged=0;
};
Result compare(Original& original,cs::BridgeClient& bridge,double sampleRate,int frames,int blocks,double bpm=120,bool tempoChange=false) {
    std::vector<float> inL(frames),inR(frames),bridgeInputL(frames),bridgeInputR(frames),directL(frames),directR(frames),bridgeL(frames),bridgeR(frames);
    float* ip[]={inL.data(),inR.data()};
    const float* constIp[]={bridgeInputL.data(),bridgeInputR.data()};
    float* directOut[]={directL.data(),directR.data()};
    float* bridgeOut[]={bridgeL.data(),bridgeR.data()};
    Result result;
    uint32_t noise=0x3751aa91;
    double ppq=0;
    for(int k=0;k<blocks;++k) {
        cs::Transport t;t.playing=true;t.bpm=tempoChange && k>=blocks/2 ? 87 : bpm;
        t.samplePosition=double(k)*frames;t.ppqPosition=ppq;
        ppq+=frames/sampleRate*t.bpm/60;
        for(int i=0;i<frames;++i) {
            const double seconds=(double(k)*frames+i)/sampleRate;
            noise=noise*1664525u+1013904223u;
            const float n=(float(noise>>8)/16777216.0f-0.5f)*0.05f;
            inL[i]=float(0.13*std::sin(6.283185307179586*220*seconds)+0.05*std::sin(6.283185307179586*367*seconds))+n;
            inR[i]=float(0.11*std::sin(6.283185307179586*331*seconds))+n*0.7f;
        }
        // Each engine must receive the same pre-call samples even if an old
        // VST implementation uses its mutable input buffers as scratch space.
        bridgeInputL=inL;bridgeInputR=inR;
        original.process(ip,directOut,frames,t);
        for(int i=0;i<frames;++i)
            result.mutatedInputSamples+=(cs::bits(inL[i])!=cs::bits(bridgeInputL[i]))+(cs::bits(inR[i])!=cs::bits(bridgeInputR[i]));
        if(!bridge.process(constIp,bridgeOut,frames,t))throw std::runtime_error(bridge.error());
        for(int ch=0;ch<2;++ch)for(int i=0;i<frames;++i) {
            const float a=directOut[ch][i],b=bridgeOut[ch][i];
            result.finite&=std::isfinite(a)&&std::isfinite(b);
            result.energy+=double(a)*a;
            ++result.samples;
            if(cs::bits(a)!=cs::bits(b)) {
                ++result.different;
                result.maxDifference=std::max(result.maxDifference,std::abs(double(a)-b));
                if(result.firstBlock<0) {result.firstBlock=k;result.firstChannel=ch;result.firstFrame=i;result.direct=a;result.bridged=b;}
            }
        }
    }
    return result;
}
}

int main(int argc,char** argv) {
    if(argc!=5) {std::cerr<<"usage: audio-equivalence helper original-bundle data-dir report.csv\n";return 2;}
    @autoreleasepool {
        try {
            std::cout.setf(std::ios::unitbuf);
            directory=argv[3];
            std::vector<Bank> banks;
            for(const auto& item:std::filesystem::directory_iterator(directory))
                if(item.path().extension()==".fxb")banks.push_back(readBank(item.path()));
            std::sort(banks.begin(),banks.end(),[](const auto& a,const auto& b){return a.name<b.name;});
            if(banks.size()!=10)throw std::runtime_error("Expected all ten recovered original banks");
            // The unmodified original seeds libc rand() from time(NULL) in its
            // constructor, and random LFOs use that process-global generator.
            // Start both stock instances in one observed epoch second so their
            // random sequences are comparable. No RNG/DSP state is patched.
            const auto beginSecond=std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now())+std::chrono::seconds(1)+std::chrono::milliseconds(10);
            std::this_thread::sleep_until(beginSecond);
            const auto seedSecond=std::time(nullptr);
            Original original(argv[2]);
            cs::BridgeClient bridge(argv[1],argv[2],argv[3]);
            if(!bridge.ready())throw std::runtime_error(bridge.error());
            if(std::time(nullptr)!=seedSecond)throw std::runtime_error("Test setup crossed original RNG seed second; rerun required");
            std::cout<<"RNG stock-original constructors within same epoch-second="<<seedSecond<<" (original time(NULL)/srand behavior)\n";
            std::ofstream report(argv[4]);
            report<<"bank,slot,name,rate,block,blocks,samples,different,max_difference,finite,energy,first_block,first_channel,first_frame,direct,bridged\n";
            int cases=0,failures=0,named=0;
            uint64_t samples=0,differences=0,mutatedInputs=0;
            double maxDifference=0,totalEnergy=0;
            bool parameterMatch=true;
            const auto start=std::chrono::steady_clock::now();
            for(const auto& bank:banks) {
                int bankCases=0,bankFailures=0,bankNamed=0;
                for(int slot=0;slot<64;++slot) {
                    if(bank.names[slot].empty())continue;
                    ++named;++bankNamed;
                    for(double sampleRate:{44100.0,48000.0,96000.0,192000.0})for(int frames:{32,256,1024}) {
                        original.restore(bank.chunk,slot);
                        if(!bridge.restore(bank.chunk,slot))throw std::runtime_error("Original bank restore rejected");
                        for(int i=0;i<cs::kParameters;++i)
                            parameterMatch&=cs::bits(original.e->getParameter(original.e,i))==cs::bits(bridge.value(i));
                        original.prepare(sampleRate,frames);
                        if(!bridge.prepare(sampleRate,frames))throw std::runtime_error("Bridge prepare failed");
                        const auto r=compare(original,bridge,sampleRate,frames,8);
                        const bool failed=r.different || !r.finite;
                        ++cases;++bankCases;failures+=failed;bankFailures+=failed;
                        samples+=r.samples;differences+=r.different;mutatedInputs+=r.mutatedInputSamples;totalEnergy+=r.energy;
                        maxDifference=std::max(maxDifference,r.maxDifference);
                        report<<std::quoted(bank.name)<<','<<slot<<','<<std::quoted(bank.names[slot])<<','<<sampleRate<<','<<frames<<",8,"<<r.samples<<','<<r.different<<','<<std::setprecision(12)<<r.maxDifference<<','<<r.finite<<','<<r.energy<<','<<r.firstBlock<<','<<r.firstChannel<<','<<r.firstFrame<<','<<r.direct<<','<<r.bridged<<'\n';
                        if(failed && failures<=5)std::cout<<"DIFF "<<bank.name<<" slot "<<slot<<" "<<bank.names[slot]<<" rate "<<sampleRate<<" block "<<frames<<" first "<<r.firstBlock<<':'<<r.firstChannel<<':'<<r.firstFrame<<" original "<<std::setprecision(9)<<r.direct<<" bridge "<<r.bridged<<" max "<<r.maxDifference<<'\n';
                    }
                }
                std::cout<<"BANK "<<bank.name<<" named="<<bankNamed<<" cases="<<bankCases<<" exact="<<bankCases-bankFailures<<" mismatched="<<bankFailures<<'\n';
            }
            const auto& bank=banks.front();
            original.restore(bank.chunk,0);bridge.restore(bank.chunk,0);
            original.e->setParameter(original.e,39,0.37f);bridge.setValue(39,0.37f);bridge.sync();
            auto originalState=original.state(),bridgeState=bridge.state();
            const bool stateEqual=!originalState.empty() && originalState==bridgeState;
            original.e->setParameter(original.e,39,0.1f);bridge.setValue(39,0.1f);bridge.sync();
            original.restore(originalState,0);
            const bool restored=bridge.restore(bridgeState,0) && cs::bits(original.e->getParameter(original.e,39))==cs::bits(bridge.value(39));
            original.prepare(48000,256);bridge.prepare(48000,256);
            const auto changed=compare(original,bridge,48000,256,384,120,true);
            std::cout<<"STATE exact-original-chunk="<<stateEqual<<" edit-restore="<<restored<<'\n';
            std::cout<<"TEMPO change120to87 cases=1 samples="<<changed.samples<<" different="<<changed.different<<" max="<<changed.maxDifference<<" finite="<<changed.finite<<'\n';
            auto savePreset=reinterpret_cast<bool(*)(void*,char*)>(dlsym(original.library,"_ZN11CAPluginVST10savePresetEPc"));
            auto loadPreset=reinterpret_cast<bool(*)(void*,char*)>(dlsym(original.library,"_ZN11CAPluginVST10loadPresetEPc"));
            auto clearPreset=reinterpret_cast<void(*)(void*)>(dlsym(original.library,"_ZN11CAPluginVST11clearPresetEv"));
            if(!savePreset || !loadPreset || !clearPreset)throw std::runtime_error("Original preset methods unavailable");
            std::vector<uint32_t> savedParameters;
            for(int i=0;i<cs::kParameters;++i)savedParameters.push_back(cs::bits(original.e->getParameter(original.e,i)));
            std::string fxp=(std::filesystem::path(argv[4]).parent_path()/"original-saved-preset.fxp").string();
            const bool saved=savePreset(original.e->object,fxp.data());
            clearPreset(original.e->object);
            char clearName[256]={};original.e->dispatch(original.e,5,0,0,clearName,0);
            int clearedDifferences=0;
            for(int i=0;i<cs::kParameters;++i)clearedDifferences+=savedParameters[i]!=cs::bits(original.e->getParameter(original.e,i));
            const bool loaded=loadPreset(original.e->object,fxp.data());
            bool fxpExact=true;
            for(int i=0;i<cs::kParameters;++i)fxpExact&=savedParameters[i]==cs::bits(original.e->getParameter(original.e,i));
            std::ifstream fxpFile(fxp,std::ios::binary);
            std::vector<uint8_t> fxpBytes((std::istreambuf_iterator<char>(fxpFile)),{});
            std::cout<<"ORIGINAL PRESET save="<<saved<<" load="<<loaded<<" exact344="<<fxpExact<<" clear-name="<<std::quoted(clearName)<<" clear-changed="<<clearedDifferences<<" file-bytes="<<fxpBytes.size();
            if(fxpBytes.size()>=24)std::cout<<" container="<<std::string(fxpBytes.begin(),fxpBytes.begin()+4)<<" format="<<std::string(fxpBytes.begin()+8,fxpBytes.begin()+12)<<" plugin="<<std::string(fxpBytes.begin()+16,fxpBytes.begin()+20);
            std::cout<<'\n';
            const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            std::cout<<"SUMMARY named="<<named<<" cases="<<cases<<" exact="<<cases-failures<<" mismatched="<<failures<<" samples="<<samples<<" differing="<<differences<<" max="<<maxDifference<<" parameter-match="<<parameterMatch<<" input-mutations="<<mutatedInputs<<" energy="<<totalEnergy<<" seconds="<<seconds<<'\n';
            return failures || !parameterMatch || !stateEqual || !restored || changed.different || !changed.finite || !(totalEnergy>1e-6)
                || !saved || !loaded || !fxpExact || std::strcmp(clearName,"blank") || clearedDifferences==0 ? 1:0;
        }catch(const std::exception& e){std::cerr<<"ERROR "<<e.what()<<'\n';return 1;}
    }
}
