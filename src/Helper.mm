#import <Cocoa/Cocoa.h>
#include "BridgeProtocol.h"
#include "LegacyABI.h"
#include <dlfcn.h>
#include <sys/mman.h>
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace {
cs::Shared* shared=nullptr;
cs::TimeInfo timeInfo;
double sampleRate=44100;int blockSize=256;std::string dataDirectory;
intptr_t host(cs::Effect*,int32_t opcode,int32_t index,intptr_t,void* ptr,float) {
    switch(opcode) {
        case 0: return 0;
        case 1: return 2400;
        case 2: return 0x43615331;
        case 7: return reinterpret_cast<intptr_t>(&timeInfo);
        case 15: return 1;
        case 16: return intptr_t(sampleRate);
        case 17: return blockSize;
        case 23: return 2;
        case 32: if(ptr)std::strcpy(static_cast<char*>(ptr),"CamelSpace Revival");return 1;
        case 33: if(ptr)std::strcpy(static_cast<char*>(ptr),"Camel Space 2");return 1;
        case 34: return 100;
        case 37: return ptr && (std::strcmp(static_cast<char*>(ptr),"sendVstTimeInfo")==0 || std::strcmp(static_cast<char*>(ptr),"sendVstMidiEvent")==0);
        case 38: return 1;
        case 41: return reinterpret_cast<intptr_t>(dataDirectory.c_str());
        case 42: case 43: case 44: return 1;
        default: (void)index;return 0;
    }
}
bool reply(int fd,cs::Command cmd,bool success,const void* bytes=nullptr,size_t size=0) {
    cs::Message msg{cmd,uint32_t(size),success?1:0};
    return cs::send(fd,&msg,sizeof(msg)) && (!size || cs::send(fd,bytes,size));
}
}
int main(int argc,char** argv) {
    @autoreleasepool {
        if(argc!=3) return 2;
        int fd=3;dataDirectory=argv[2];
        int yes=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
        const timeval sendTimeout{0,100000};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&sendTimeout,sizeof(sendTimeout));
        void* address=mmap(nullptr,sizeof(cs::Shared),PROT_READ|PROT_WRITE,MAP_SHARED,4,0);close(4);
        if(address==MAP_FAILED) return 3;shared=static_cast<cs::Shared*>(address);
        if(shared->protocol!=cs::kProtocol) return 4;
        NSString* bundlePath=[NSString stringWithUTF8String:argv[1]];
        NSBundle* bundle=[NSBundle bundleWithPath:bundlePath];
        // Register the original bundle so its own CFBundle resource lookups work.
        [bundle load];
        std::string binary=std::string(argv[1])+"/Contents/MacOS/CamelSpace";
        void* library=dlopen(binary.c_str(),RTLD_LOCAL|RTLD_NOW);
        if(!library) {fprintf(stderr,"CamelSpace helper: %s\n",dlerror());return 5;}
        auto entry=reinterpret_cast<cs::Effect*(*)(cs::HostCallback)>(dlsym(library,"VSTPluginMain"));
        if(!entry) return 6;
        auto* e=entry(host);
        if(!e || e->magic!=0x56737450 || e->parameters!=cs::kParameters || e->inputs!=2 || e->outputs!=2 || !e->replace) return 7;
        e->dispatch(e,0,0,0,nullptr,0);
        // Version-specific adapter to the original public C++ exports. The
        // wrapper's 0x110 member and vtable slot were verified against this
        // supplied 1.501 binary; no engine instructions are replaced.
        void* inner=nullptr;
        if(e->object)std::memcpy(&inner,static_cast<char*>(e->object)+0x110,sizeof(inner));
        auto randomize=reinterpret_cast<void(*)(void*)>(dlsym(library,"_ZN10CamelSpace9randomizeEv"));
        auto clear=reinterpret_cast<void(*)(void*)>(dlsym(library,"_ZN11CAPluginVST11clearPresetEv"));
        auto* table=reinterpret_cast<char*>(dlsym(library,"_ZTV10CamelSpace"));
        bool verifiedInner=inner && table && *static_cast<void**>(inner)==table+2*sizeof(void*);
        uint32_t applied[cs::kParameters]{};
        auto refresh=[&] {
            for(int i=0;i<cs::kParameters;++i) {
                shared->parameter[i].actual.store(cs::bits(e->getParameter(e,i)),std::memory_order_release);
                shared->parameter[i].applied.store(applied[i],std::memory_order_release);
            }
        };
        auto apply=[&] {
            for(int i=0;i<cs::kParameters;++i) {
                auto revision=shared->parameter[i].revision.load(std::memory_order_acquire);
                if(revision!=applied[i]) {e->setParameter(e,i,cs::number(shared->parameter[i].requested.load()));applied[i]=revision;}
            }
        };
        cs::Info info;info.parameters=e->parameters;info.programs=e->programs;info.latency=e->latency;
        for(int i=0;i<cs::kParameters;++i) {e->dispatch(e,8,i,0,info.names[i],0);e->dispatch(e,6,i,0,info.labels[i],0);}
        refresh();if(!reply(fd,cs::Command::ready,true,&info,sizeof(info))) return 8;
        bool active=false,running=true;
        while(running) {
            cs::Message msg{};if(!cs::receive(fd,&msg,sizeof(msg),-1)) break;
            if(msg.bytes>cs::kMaxState) break;
            std::vector<uint8_t> payload(msg.bytes);
            if(msg.bytes && !cs::receive(fd,payload.data(),payload.size())) break;
            apply();
            bool ok=true;
            switch(msg.command) {
                case cs::Command::prepare:
                    ok=std::isfinite(msg.number) && msg.number>=8000 && msg.number<=384000 && msg.argument>0 && msg.argument<=cs::kMaxFrames;
                    if(ok) {
                        if(active)e->dispatch(e,12,0,0,nullptr,0);
                        sampleRate=msg.number;blockSize=msg.argument;timeInfo.sampleRate=sampleRate;
                        e->dispatch(e,10,0,0,nullptr,float(sampleRate));e->dispatch(e,11,0,blockSize,nullptr,0);
                        e->dispatch(e,12,0,1,nullptr,0);e->dispatch(e,71,0,0,nullptr,0);active=true;
                    }
                    break;
                case cs::Command::process: {
                    std::atomic_thread_fence(std::memory_order_acquire);
                    ok=active && shared->frames>0 && shared->frames<=cs::kMaxFrames;
                    if(!ok)break;
                    const auto& t=shared->transport;
                    timeInfo.samplePosition=t.samplePosition;timeInfo.ppqPosition=t.ppqPosition;
                    timeInfo.tempo=std::isfinite(t.bpm) && t.bpm>0 ? t.bpm : 120;
                    timeInfo.numerator=t.numerator>0?t.numerator:4;timeInfo.denominator=t.denominator>0?t.denominator:4;
                    timeInfo.cycleStart=t.cycleStart;timeInfo.cycleEnd=t.cycleEnd;
                    timeInfo.flags=(1<<9)|(1<<10)|(1<<11)|(1<<12)|(1<<13)|(t.playing?2:0)|(t.cycling?4:0);
                    timeInfo.barStart=std::floor(t.ppqPosition/4.0)*4;
                    cs::LegacyMidi midi[cs::kMaxMidi];
                    struct Events {int32_t count;intptr_t reserved;void* events[cs::kMaxMidi];} events{};
                    events.count=std::clamp(shared->midiCount,0,cs::kMaxMidi);
                    for(int i=0;i<events.count;++i) {midi[i].deltaFrames=std::clamp(shared->midi[i].offset,0,shared->frames-1);std::memcpy(midi[i].data,shared->midi[i].bytes,3);events.events[i]=&midi[i];}
                    if(events.count)e->dispatch(e,25,0,0,&events,0);
                    float* inputs[]={shared->input[0],shared->input[1]};float* outputs[]={shared->output[0],shared->output[1]};
                    e->replace(e,inputs,outputs,shared->frames);
                    std::atomic_thread_fence(std::memory_order_release);
                    break;
                }
                case cs::Command::sync:break;
                case cs::Command::randomize:ok=verifiedInner && randomize;if(ok)randomize(inner);break;
                case cs::Command::clear:ok=verifiedInner && clear;if(ok)clear(e->object);break;
                case cs::Command::restore:
                    ok=cs::validChunk(payload) && msg.argument>=0 && msg.argument<64;
                    if(ok) {e->dispatch(e,24,0,payload.size(),payload.data(),0);e->dispatch(e,2,0,msg.argument,nullptr,0);}
                    break;
                case cs::Command::state: {
                    void* state=nullptr;auto count=e->dispatch(e,23,0,0,&state,0);
                    ok=count>0 && count<=intptr_t(cs::kMaxState) && state;
                    if(!reply(fd,msg.command,ok,ok?state:nullptr,ok?size_t(count):0))running=false;
                    refresh();continue;
                }
                case cs::Command::displays: {
                    char displays[cs::kParameters][64]{};
                    for(int i=0;i<cs::kParameters;++i)e->dispatch(e,7,i,0,displays[i],0);
                    if(!reply(fd,msg.command,true,displays,sizeof(displays)))running=false;
                    refresh();continue;
                }
                case cs::Command::quit:running=false;continue;
                default:ok=false;break;
            }
            refresh();if(!reply(fd,msg.command,ok))break;
        }
        if(active) {e->dispatch(e,72,0,0,nullptr,0);e->dispatch(e,12,0,0,nullptr,0);}
        e->dispatch(e,1,0,0,nullptr,0);
        munmap(shared,sizeof(cs::Shared));close(fd);
        // Leave unloading to process teardown: old Cocoa plug-ins may retain class pointers.
    }
    return 0;
}
