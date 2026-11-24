#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>

namespace cs {
constexpr int kParameters=344, kMaxFrames=8192, kMaxMidi=256;
constexpr uint32_t kProtocol=0x43533201;
constexpr size_t kMaxState=1024*1024;
struct Transport {
    double bpm=120, ppqPosition=0, samplePosition=0;
    bool playing=false, cycling=false;
    double cycleStart=0,cycleEnd=0;
    int numerator=4,denominator=4;
};
struct Midi { int offset=0; uint8_t bytes[3]{}; uint8_t size=3; };
struct ParameterSlot {
    std::atomic<uint32_t> requested{0},actual{0},revision{0},applied{0};
};
struct Shared {
    uint32_t protocol=kProtocol;
    ParameterSlot parameter[kParameters];
    alignas(64) float input[2][kMaxFrames]{},output[2][kMaxFrames]{};
    Transport transport;
    Midi midi[kMaxMidi];
    int frames=0,midiCount=0;
};
static_assert(std::atomic<uint32_t>::is_always_lock_free);
struct Info {
    uint32_t protocol=kProtocol;
    int parameters=0,programs=0,latency=0;
    char names[kParameters][64]{},labels[kParameters][32]{};
};
enum class Command : uint32_t { ready=1,prepare,process,sync,restore,state,displays,quit,randomize,clear };
struct Message { Command command; uint32_t bytes=0; int argument=0; double number=0; };
inline uint32_t bits(float f) { uint32_t i; std::memcpy(&i,&f,4); return i; }
inline float number(uint32_t i) { float f; std::memcpy(&f,&i,4); return f; }
inline bool receive(int fd,void* data,size_t size,int timeoutMs=30000) {
    auto* out=static_cast<char*>(data);
    while(size) {
        pollfd p{fd,POLLIN,0};
        int r; do { r=poll(&p,1,timeoutMs); } while(r<0 && errno==EINTR);
        if(r<=0 || !(p.revents&POLLIN)) return false;
        auto n=::read(fd,out,size);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) return false;
        out+=n;size-=size_t(n);
    }
    return true;
}
inline bool send(int fd,const void* data,size_t size) {
    auto* in=static_cast<const char*>(data);
    while(size) {
        auto n=::write(fd,in,size);
        if(n<0 && errno==EINTR) continue;
        if(n<=0) return false;
        in+=n;size-=size_t(n);
    }
    return true;
}
// These checks protect the old parser from malformed host-provided state.
inline bool validRecords(const std::vector<uint8_t>& v,int records) {
    if(v.empty() || v.size()>kMaxState) return false;
    size_t p=0;
    for(int slot=0;slot<records;++slot) {
        if(p+4>v.size()) return false;
        float version; std::memcpy(&version,v.data()+p,4);p+=4;
        if(version!=1.0f) return false;
        size_t begin=p; while(p<v.size() && v[p] && p-begin<64) ++p;
        if(p>=v.size() || p-begin>=64) return false;
        ++p;
        if(p+4>v.size()) return false;
        uint32_t count;std::memcpy(&count,v.data()+p,4);p+=4;
        if(count!=kParameters || p+count*4>v.size()) return false;
        for(uint32_t i=0;i<count;++i) {
            float value;std::memcpy(&value,v.data()+p,4);p+=4;
            if(!std::isfinite(value) || value<0 || value>1) return false;
        }
    }
    return p==v.size();
}
inline bool validChunk(const std::vector<uint8_t>& v) {return validRecords(v,64);}
}
