#pragma once
#include <cstdint>
// Independently declared public VST2 binary interface. No legacy SDK required.
namespace cs {
struct Effect;
using HostCallback=intptr_t(*)(Effect*,int32_t,int32_t,intptr_t,void*,float);
struct Effect {
    int32_t magic;HostCallback dispatch;
    void(*process)(Effect*,float**,float**,int32_t);
    void(*setParameter)(Effect*,int32_t,float);
    float(*getParameter)(Effect*,int32_t);
    int32_t programs,parameters,inputs,outputs,flags;
    intptr_t reserved1,reserved2;
    int32_t latency,quality1,quality2;float ratio;
    void *object,*user;
    int32_t id,version;
    void(*replace)(Effect*,float**,float**,int32_t);
    void(*doubleReplace)(Effect*,double**,double**,int32_t);
    char future[56];
};
struct TimeInfo {
    double samplePosition=0,sampleRate=44100,nanoseconds=0,ppqPosition=0,tempo=120,
      barStart=0,cycleStart=0,cycleEnd=0;
    int32_t numerator=4,denominator=4,smpteOffset=0,smpteRate=0,nextClock=0,flags=0;
};
struct LegacyMidi {
    int32_t type=1,byteSize=32,deltaFrames=0,flags=0,noteLength=0,noteOffset=0;
    uint8_t data[4]{};int8_t detune=0,noteOffVelocity=0,reserved1=0,reserved2=0;
};
}
