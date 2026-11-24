#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>

// Optional probe-only compatibility shim. Redirect only the original support
// prefix, leaving the original bundle and every security/authentication path
// unchanged. Build as an injected dylib with CAMELSPACE_RESOURCE_SHIM defined.
#ifdef CAMELSPACE_RESOURCE_SHIM
static const char* mappedPath(const char* path,std::string& storage) {
    const char* replacement=getenv("CAMELSPACE_PROBE_DATA");
    const char* prefix="/Library/Application Support/Camel Audio/CamelSpaceData";
    if(replacement && path && !strncmp(path,prefix,strlen(prefix))) {
        storage=std::string(replacement)+(path+strlen(prefix));
        fprintf(stderr,"RESOURCE REDIRECT %s -> %s\n",path,storage.c_str());
        return storage.c_str();
    }
    return path;
}
static FILE* loggedFopen(const char* path,const char* mode) {
    std::string mapped;
    FILE* file=fopen(mappedPath(path,mapped),mode);
    fprintf(stderr,"FOPEN %s mode=%s result=%p\n",path?path:"<null>",mode?mode:"<null>",file);
    return file;
}
static CFURLRef loggedFileURL(CFAllocatorRef allocator,const UInt8* bytes,CFIndex length,Boolean directory) {
    CFURLRef url=CFURLCreateFromFileSystemRepresentation(allocator,bytes,length,directory);
    fprintf(stderr,"FILE URL %.*s\n",int(length),reinterpret_cast<const char*>(bytes));
    return url;
}
static CFURLRef loggedPathURL(CFAllocatorRef allocator,CFStringRef path,CFURLPathStyle style,Boolean directory) {
    char bytes[4096]={};
    CFStringGetCString(path,bytes,sizeof(bytes),kCFStringEncodingUTF8);
    std::string mapped;
    const char* use=mappedPath(bytes,mapped);
    fprintf(stderr,"PATH URL %s\n",bytes);
    CFStringRef mappedString=CFStringCreateWithCString(allocator,use,kCFStringEncodingUTF8);
    CFURLRef url=CFURLCreateWithFileSystemPath(allocator,mappedString,style,directory);
    CFRelease(mappedString);
    return url;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct { const void* replacement; const void* original; } fileObservers[]={
    {reinterpret_cast<const void*>(loggedFopen),reinterpret_cast<const void*>(fopen)},
    {reinterpret_cast<const void*>(loggedFileURL),reinterpret_cast<const void*>(CFURLCreateFromFileSystemRepresentation)},
    {reinterpret_cast<const void*>(loggedPathURL),reinterpret_cast<const void*>(CFURLCreateWithFileSystemPath)}
};
#endif

// Standalone interoperability probe. The original bundle is loaded unchanged.
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
struct EditorRect { int16_t top, left, bottom, right; };
struct TimeInfo {
    double samples=0, sampleRate=44100, nanos=0, ppq=0, tempo=120;
    double bar=0, cycleStart=0, cycleEnd=0;
    int32_t numerator=4, denominator=4, smpteOffset=0, smpteRate=0, clock=0;
    int32_t flags=(1<<9)|(1<<10)|(1<<11)|(1<<13);
};
static TimeInfo timeInfo;
static const char* dataDir;
static NSWindow* editorWindow;
static intptr_t host(Effect*, int32_t op, int32_t index, intptr_t value, void* ptr, float opt) {
    switch(op) {
        case 0: fprintf(stderr,"AUTOMATE %d %.8g\n",index,opt); return 0;
        case 1: return 2400;
        case 7: return reinterpret_cast<intptr_t>(&timeInfo);
        case 15: if(editorWindow) [editorWindow setContentSize:NSMakeSize(index,value)]; return 1;
        case 16: return 44100;
        case 17: return 256;
        case 23: return 1;
        case 32: if(ptr) strcpy(static_cast<char*>(ptr),"CamelSpace feasibility probe"); return 1;
        case 33: if(ptr) strcpy(static_cast<char*>(ptr),"CamelSpace feasibility probe"); return 1;
        case 34: return 1;
        case 37: if(ptr) {
            const auto* name=static_cast<const char*>(ptr);
            if (!strcmp(name,"sendVstTimeInfo") || !strcmp(name,"sizeWindow") || !strcmp(name,"receiveVstEvents") || !strcmp(name,"receiveVstMidiEvent")) return 1;
            fprintf(stderr,"HOST CANDO %s\n",name);
        } return 0;
        case 38: return 1;
        case 41: return reinterpret_cast<intptr_t>(dataDir);
        default: return 0;
    }
}
static uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}
static void describe(NSView* view,int depth=0) {
    fprintf(stderr,"%*s%s frame=%s layer=%d hidden=%d\n",depth*2,"",NSStringFromClass([view class]).UTF8String,
        NSStringFromRect(view.frame).UTF8String,view.wantsLayer,view.hidden);
    for (NSView* child in view.subviews) describe(child,depth+1);
}
static void pump(Effect* e,double seconds) {
    NSDate* until=[NSDate dateWithTimeIntervalSinceNow:seconds];
    while(until.timeIntervalSinceNow>0) {
        @autoreleasepool {
            e->dispatch(e,19,0,0,nullptr,0);
            NSEvent* event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01] inMode:NSDefaultRunLoopMode dequeue:YES];
            if(event) [NSApp sendEvent:event];
            [NSApp updateWindows];
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
        }
    }
}
static bool screenshot(NSView* view,NSString* path) {
    NSBitmapImageRep* rep=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
    NSData* png=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    const bool ok=[png writeToFile:path atomically:YES];
    bool varied=false;
    const auto* bytes=rep.bitmapData;
    const auto pixelBytes=rep.bitsPerPixel/8;
    if(bytes && pixelBytes>0) {
        for(NSInteger y=0;y<rep.pixelsHigh && !varied;++y)
            for(NSInteger x=0;x<rep.pixelsWide;++x)
                if(memcmp(bytes,bytes+y*rep.bytesPerRow+x*pixelBytes,pixelBytes)) { varied=true; break; }
    }
    fprintf(stderr,"SCREENSHOT %s size=%ldx%ld bytes=%lu saved=%d pixel-variation=%d\n",path.UTF8String,(long)rep.pixelsWide,(long)rep.pixelsHigh,(unsigned long)png.length,ok,varied);
    return ok && varied;
}
static void directScreenshot(NSView* view,NSString* path) {
    NSBitmapImageRep* rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:570 pixelsHigh:500 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
    NSGraphicsContext* context=[NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    [[NSColor colorWithRed:1 green:0 blue:0 alpha:1] set]; NSRectFill(NSMakeRect(0,0,570,500));
    [view drawRect:view.bounds];
    [NSGraphicsContext restoreGraphicsState];
    NSData* png=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    [png writeToFile:path atomically:YES];
}
int main(int argc,char** argv) {
    if(argc<4) { fprintf(stderr,"usage: editor-probe binary data-dir output-dir [layered]\n"); return 2; }
    @autoreleasepool {
        setvbuf(stdout,nullptr,_IONBF,0); setvbuf(stderr,nullptr,_IONBF,0);
        dataDir=argv[2];
        [[NSUserDefaults standardUserDefaults] setBool:NO forKey:@"NSViewUsesAutomaticLayerBacking"];
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];
        chdir(dataDir);
        fprintf(stderr,"LOAD %s CWD %s\n",argv[1],dataDir);
        void* lib=dlopen(argv[1],RTLD_LOCAL|RTLD_NOW);
        if(!lib) { fprintf(stderr,"LOAD FAILED %s\n",dlerror()); return 3; }
        auto entry=reinterpret_cast<Effect*(*)(Callback)>(dlsym(lib,"VSTPluginMain"));
        Effect* e=entry?entry(host):nullptr;
        if(!e || e->magic!=0x56737450) return 4;
        e->dispatch(e,0,0,0,nullptr,0);
        fprintf(stderr,"OPEN params=%d programs=%d object=%p\n",e->parameters,e->programs,e->object);
        // Read-only inspection of the two resource-path fields identified in
        // this exact 1.50.1 binary's exported constructors; no field is edited.
        auto* originalPlugin=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(e->object)+0x110);
        fprintf(stderr,"RESOURCE PATH wrapper=%s plugin=%s\n",static_cast<char*>(e->object)+0x12c,reinterpret_cast<char*>(originalPlugin+0x1e48));
        const auto* pluginVtable=*reinterpret_cast<void***>(originalPlugin);
        void* randomizeSymbol=dlsym(lib,"_ZN10CamelSpace9randomizeEv");
        Dl_info randomizeInfo{}; dladdr(pluginVtable[12],&randomizeInfo);
        fprintf(stderr,"INNER VTABLE RANDOMIZE pointer=%p exported=%p match=%d symbol=%s\n",pluginVtable[12],randomizeSymbol,pluginVtable[12]==randomizeSymbol,randomizeInfo.dli_sname?randomizeInfo.dli_sname:"<unknown>");
        auto shortName=reinterpret_cast<void(*)(void*,int,char*)>(dlsym(lib,"_ZN8CAPlugin17getParamShortNameEiPc"));
        auto targets=static_cast<const uint16_t*>(dlsym(lib,"ModulationTarget"));
        if(shortName && targets)for(int i=0;i<23;++i) {
            char text[256]={};shortName(originalPlugin,targets[i],text);
            fprintf(stderr,"MORPH %d param=%d short=%s\n",i,targets[i],text);
        }
        const int internalCount=*reinterpret_cast<int*>(originalPlugin+0x10);
        auto getName=reinterpret_cast<void(*)(void*,int,char*)>(dlsym(lib,"_ZN8CAPlugin12getParamNameEiPc"));
        auto getRaw=reinterpret_cast<float(*)(void*)>(dlsym(lib,"_ZN6PParam18getUnsmoothedParamEv"));
        auto internalParameters=*reinterpret_cast<void***>(originalPlugin+8);
        fprintf(stderr,"INTERNAL PARAMETER COUNT %d\n",internalCount);
        if(getName && getRaw && internalCount>=344 && internalCount<400)for(int i=344;i<internalCount;++i) {
            char text[256]={};getName(originalPlugin,i,text);
            fprintf(stderr,"INTERNAL %d name=%s raw=%.9g\n",i,text,getRaw(internalParameters[i]));
        }
        std::ifstream file(std::string(dataDir)+"/-Default.fxb",std::ios::binary);
        std::vector<unsigned char> bank((std::istreambuf_iterator<char>(file)),{});
        if(bank.size()>160 && be32(bank.data()+156)==bank.size()-160) {
            const auto r=e->dispatch(e,24,0,bank.size()-160,bank.data()+160,0);
            e->dispatch(e,2,0,0,nullptr,0);
            fprintf(stderr,"SET BANK result=%ld bytes=%zu\n",(long)r,bank.size()-160);
        }
        for(int i=0;i<e->parameters;++i) { char n[256]={},v[256]={}; e->dispatch(e,8,i,0,n,0); e->dispatch(e,7,i,0,v,0); printf("PARAM %d %s %.8g %s\n",i,n,e->getParameter(e,i),v); }
        e->dispatch(e,10,0,0,nullptr,44100);
        e->dispatch(e,11,0,256,nullptr,0);
        EditorRect* rect=nullptr;
        e->dispatch(e,13,0,0,&rect,0);
        int w=rect?rect->right-rect->left:780,h=rect?rect->bottom-rect->top:500;
        fprintf(stderr,"RECT %d %d\n",w,h);
        if(w<1 || h<1 || w>2048 || h>2048) { w=780;h=500; }
        editorWindow=[[NSWindow alloc] initWithContentRect:NSMakeRect(100,100,w,h) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
        editorWindow.title=@"CamelSpace original editor feasibility probe";
        NSView* view=editorWindow.contentView;
        [view setWantsLayer:argc>4];
        [editorWindow makeKeyAndOrderFront:nil];
        fprintf(stderr,"BEFORE EDIT OPEN parent=%p\n",view);
        const auto opened=e->dispatch(e,14,0,0,(__bridge void*)view,0);
        fprintf(stderr,"EDIT OPEN result=%ld\n",(long)opened);
        pump(e,2);
        describe(view);
        NSString* out=[NSString stringWithUTF8String:argv[3]];
        [[NSFileManager defaultManager] createDirectoryAtPath:out withIntermediateDirectories:YES attributes:nil error:nil];
        const bool rendered=screenshot(view,[out stringByAppendingPathComponent:@"original-editor.png"]);
        if(view.subviews.count) directScreenshot(view.subviews.firstObject,[out stringByAppendingPathComponent:@"original-editor-direct-draw.png"]);
        const float before=e->getParameter(e,0);
        NSPoint p=NSMakePoint(428,h-372);
        NSView* hit=[view hitTest:p];
        fprintf(stderr,"CLICK view=%s at=%s param0=%.8g\n",hit?NSStringFromClass([hit class]).UTF8String:"nil",NSStringFromPoint(p).UTF8String,before);
        for(NSEventType type: {NSEventTypeLeftMouseDown,NSEventTypeLeftMouseUp}) {
            NSEvent* event=[NSEvent mouseEventWithType:type location:p modifierFlags:0 timestamp:[NSProcessInfo processInfo].systemUptime windowNumber:editorWindow.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1.0];
            [NSApp sendEvent:event];
        }
        pump(e,0.5);
        screenshot(view,[out stringByAppendingPathComponent:@"original-editor-after-click.png"]);
        fprintf(stderr,"CLICK RESULT param0 before=%.8g after=%.8g\n",before,e->getParameter(e,0));
        e->dispatch(e,15,0,0,nullptr,0);
        e->dispatch(e,1,0,0,nullptr,0);
        [editorWindow orderOut:nil];
        puts("EDITOR PROBE FINISHED");
        if(!rendered) { fprintf(stderr,"ORIGINAL EDITOR RENDER ACCEPTANCE FAILED: uniform bitmap\n"); return 5; }
    }
}
