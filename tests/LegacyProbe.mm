#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#include <cstdint>
#include <cstdio>

// A small ABI probe, not an alternative DSP implementation.
struct Effect;
using Callback = intptr_t (*)(Effect*, int32_t, int32_t, intptr_t, void*, float);
struct Effect {
    int32_t magic;
    Callback dispatch;
    void (*process)(Effect*, float**, float**, int32_t);
    void (*setParameter)(Effect*, int32_t, float);
    float (*getParameter)(Effect*, int32_t);
    int32_t programs, parameters, inputs, outputs, flags;
    intptr_t reserved1, reserved2;
    int32_t latency, qualities1, qualities2;
    float ratio;
    void *object, *user;
    int32_t id, version;
    void (*replace)(Effect*, float**, float**, int32_t);
    void* doubleReplace;
    char future[56];
};
static intptr_t host(Effect*, int32_t opcode, int32_t, intptr_t, void*, float) {
    if (opcode == 1) return 2400;
    if (opcode == 16) return 44100;
    if (opcode == 17) return 256;
    if (opcode == 32) return 0x43615331;
    return 0;
}
int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc != 2) return 2;
        fprintf(stderr,"Before NSApplication\n");
        [NSApplication sharedApplication];
        fprintf(stderr,"Before dlopen\n");
        void* lib = dlopen(argv[1], RTLD_LOCAL | RTLD_NOW);
        if (!lib) { fprintf(stderr, "LOAD FAILED: %s\n", dlerror()); return 1; }
        puts("Library loaded"); fflush(stdout);
        auto entry = reinterpret_cast<Effect*(*)(Callback)>(dlsym(lib,"VSTPluginMain"));
        if (!entry) return 3;
        auto* e=entry(host);
        if (!e) return 4;
        printf("effect magic=%x programs=%d params=%d in=%d out=%d id=%x flags=%x\n",
            e->magic,e->programs,e->parameters,e->inputs,e->outputs,e->id,e->flags); fflush(stdout);
        e->dispatch(e,0,0,0,nullptr,0);
        for (int i=0;i<e->parameters && i<90;++i) {
            char name[256]={},value[256]={};
            e->dispatch(e,8,i,0,name,0); e->dispatch(e,7,i,0,value,0);
            printf("param %d %s %.7g %s\n",i,name,e->getParameter(e,i),value);
        }
        void* chunk=nullptr;
        auto bytes=e->dispatch(e,23,0,0,&chunk,0);
        printf("state bytes=%ld\n",(long)bytes);
        e->dispatch(e,10,0,0,nullptr,44100);
        e->dispatch(e,11,0,256,nullptr,0);
        e->dispatch(e,12,0,1,nullptr,0);
        float in[2][256]={},out[2][256]={}; in[0][0]=in[1][0]=1;
        float* ip[]={in[0],in[1]};float* op[]={out[0],out[1]};
        if(e->replace) e->replace(e,ip,op,256);
        printf("audio first=%.7g %.7g\n",out[0][0],out[1][0]);
        e->dispatch(e,12,0,0,nullptr,0);
        e->dispatch(e,1,0,0,nullptr,0);
        puts("Probe passed");
    }
}
