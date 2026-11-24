#include "BridgeClient.h"
#include <algorithm>
#include <fstream>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
extern char** environ;

namespace cs {
BridgeClient::BridgeClient(const std::string& helper,const std::string& plugin,const std::string& data) {
    if(access(helper.c_str(),X_OK)!=0 || access(plugin.c_str(),R_OK)!=0) {error_="CamelSpace helper or original plug-in is missing";return;}
    char filename[]="/tmp/camelspace-shared-XXXXXX";
    int memory=mkstemp(filename); if(memory<0) {error_="Cannot create private audio memory";return;}
    unlink(filename);
    if(ftruncate(memory,sizeof(Shared))!=0) {close(memory);error_="Cannot size audio memory";return;}
    void* address=mmap(nullptr,sizeof(Shared),PROT_READ|PROT_WRITE,MAP_SHARED,memory,0);
    if(address==MAP_FAILED) {close(memory);error_="Cannot map audio memory";return;}
    shared_=new(address) Shared;
    int sockets[2];
    if(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)!=0) {close(memory);error_="Cannot create helper connection";return;}
    socket_=sockets[0];
    int yes=1;setsockopt(socket_,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
    const timeval sendTimeout{0,100000};setsockopt(socket_,SOL_SOCKET,SO_SNDTIMEO,&sendTimeout,sizeof(sendTimeout));
    // Duplicate above the reserved child descriptors before configuring spawn.
    int childSocket=fcntl(sockets[1],F_DUPFD_CLOEXEC,10);
    int childMemory=fcntl(memory,F_DUPFD_CLOEXEC,10);
    close(sockets[1]);close(memory);
    if(childSocket<0 || childMemory<0) {if(childSocket>=0)close(childSocket);if(childMemory>=0)close(childMemory);error_="Cannot transfer helper descriptors";return;}
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,childSocket,3);
    posix_spawn_file_actions_adddup2(&actions,childMemory,4);
    posix_spawn_file_actions_addclose(&actions,childSocket);
    posix_spawn_file_actions_addclose(&actions,childMemory);
    // The parent's socket may itself be descriptor 3 or 4; do not close its new child replacement.
    if(socket_!=3 && socket_!=4) posix_spawn_file_actions_addclose(&actions,socket_);
    std::vector<char*> args{const_cast<char*>(helper.c_str()),const_cast<char*>(plugin.c_str()),const_cast<char*>(data.c_str()),nullptr};
    posix_spawnattr_t attributes;posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes,POSIX_SPAWN_CLOEXEC_DEFAULT);
    if(fcntl(STDERR_FILENO,F_GETFD)>=0)posix_spawn_file_actions_addinherit_np(&actions,STDERR_FILENO);
    pid_t child=-1;int result=posix_spawn(&child,helper.c_str(),&actions,&attributes,args.data(),environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);close(childSocket);close(childMemory);
    if(result!=0) {error_="Cannot start Rosetta helper: "+std::to_string(result);return;}
    pid_=child;
    Message hello{};
    if(!receive(socket_,&hello,sizeof(hello),30000) || hello.command!=Command::ready || hello.bytes!=sizeof(Info)
       || !receive(socket_,&info_,sizeof(info_),30000) || info_.protocol!=kProtocol || info_.parameters!=kParameters) {
        error_="Original CamelSpace engine did not start (Rosetta is required)";return;
    }
    alive_.store(true);
    updateDisplays();
}
BridgeClient::~BridgeClient() {
    if(socket_>=0) {
        if(alive_.load()) { Message quit{Command::quit}; send(socket_,&quit,sizeof(quit)); }
        shutdown(socket_,SHUT_RDWR);close(socket_);
    }
    if(pid_>0) {
        int status=0;
        // A stuck legacy library must never stall host teardown indefinitely.
        for(int n=0;n<30;++n) {auto r=waitpid(pid_,&status,WNOHANG);if(r==pid_ || r<0) {pid_=-1;break;}usleep(1000);}
        if(pid_>0) {kill(pid_,SIGKILL);waitpid(pid_,&status,0);}
    }
    if(shared_) munmap(shared_,sizeof(Shared));
}
bool BridgeClient::ready() const {return alive_.load();}
void BridgeClient::fail(const std::string& reason) {error_=reason;alive_.store(false,std::memory_order_release);}
std::string BridgeClient::parameterName(int i) const {return i>=0&&i<info_.parameters ? info_.names[i] : "";}
std::string BridgeClient::parameterLabel(int i) const {return i>=0&&i<info_.parameters ? info_.labels[i] : "";}
std::string BridgeClient::display(int i) const {std::lock_guard lock(displayMutex_);return i>=0&&i<info_.parameters ? displays_[i] : "";}
float BridgeClient::value(int i) const {
    if(!shared_ || i<0 || i>=kParameters)return 0;
    auto& p=shared_->parameter[i];
    return number(p.revision.load(std::memory_order_acquire)!=p.applied.load(std::memory_order_acquire)
        ? p.requested.load(std::memory_order_acquire) : p.actual.load(std::memory_order_acquire));
}
void BridgeClient::setValue(int i,float v) {
    if(!shared_ || i<0 || i>=kParameters || !std::isfinite(v)) return;
    v=std::clamp(v,0.0f,1.0f);auto& p=shared_->parameter[i];
    p.requested.store(bits(v),std::memory_order_relaxed);
    p.actual.store(bits(v),std::memory_order_release);
    p.revision.fetch_add(1,std::memory_order_release);
    displaysDirty_.store(true,std::memory_order_release);
}
bool BridgeClient::request(Message msg,const void* payload,std::vector<uint8_t>* result,int timeout) {
    if(!ready()) return false;
    if(!send(socket_,&msg,sizeof(msg)) || (msg.bytes && !send(socket_,payload,msg.bytes))) {fail("CamelSpace helper connection closed");return false;}
    Message reply{};
    if(!receive(socket_,&reply,sizeof(reply),timeout) || reply.command!=msg.command || reply.bytes>kMaxState) {fail("CamelSpace helper stopped responding");return false;}
    if(reply.bytes) {
        if(!result) {fail("Unexpected helper payload");return false;}
        result->resize(reply.bytes);
        if(!receive(socket_,result->data(),result->size(),timeout)) {fail("Truncated CamelSpace helper reply");return false;}
    }
    return reply.argument==1;
}
bool BridgeClient::sync() {std::lock_guard lock(ioMutex_);return request({Command::sync});}
bool BridgeClient::randomize() {std::lock_guard lock(ioMutex_);bool ok=request({Command::randomize});displaysDirty_.store(true);return ok;}
bool BridgeClient::clearPreset() {std::lock_guard lock(ioMutex_);bool ok=request({Command::clear});displaysDirty_.store(true);return ok;}
bool BridgeClient::updateDisplays() {
    if(!displaysDirty_.load(std::memory_order_acquire))return ready();
    std::unique_lock lock(ioMutex_,std::try_to_lock);if(!lock.owns_lock())return false;
    displaysDirty_.store(false,std::memory_order_release);std::vector<uint8_t> bytes;
    if(!request({Command::displays},nullptr,&bytes,100) || bytes.size()!=sizeof(displays_)) {displaysDirty_.store(true);return false;}
    std::lock_guard displayLock(displayMutex_);std::memcpy(displays_,bytes.data(),bytes.size());return true;
}
bool BridgeClient::prepare(double rate,int frames) {
    if(!std::isfinite(rate) || rate<8000 || rate>384000 || frames<1 || frames>kMaxFrames) return false;
    std::lock_guard lock(ioMutex_);return request({Command::prepare,0,frames,rate});
}
bool BridgeClient::process(const float* const* input,float* const* output,int frames,const Transport& transport,const Midi* midi,int count) {
    if(!ready() || frames<1 || frames>kMaxFrames) return false;
    std::unique_lock lock(ioMutex_,std::defer_lock);
    if(!lock.try_lock_for(std::chrono::microseconds(100)))return false;
    shared_->frames=frames;shared_->transport=transport;shared_->midiCount=std::clamp(count,0,kMaxMidi);
    for(int ch=0;ch<2;++ch) std::memcpy(shared_->input[ch],input[ch],frames*sizeof(float));
    if(midi && shared_->midiCount) std::memcpy(shared_->midi,midi,shared_->midiCount*sizeof(Midi));
    else shared_->midiCount=0;
    std::atomic_thread_fence(std::memory_order_release);
    if(!request({Command::process},nullptr,nullptr,100)) return false;
    std::atomic_thread_fence(std::memory_order_acquire);
    for(int ch=0;ch<2;++ch) std::memcpy(output[ch],shared_->output[ch],frames*sizeof(float));
    return true;
}
std::vector<uint8_t> BridgeClient::state() {
    std::lock_guard lock(ioMutex_); std::vector<uint8_t> result;
    if(!request({Command::state},nullptr,&result) || !validChunk(result)) result.clear();return result;
}
bool BridgeClient::restore(const std::vector<uint8_t>& state,int slot) {
    if(!validChunk(state) || slot<0 || slot>=64) return false;
    std::lock_guard lock(ioMutex_);bool ok=request({Command::restore,uint32_t(state.size()),slot},state.data());displaysDirty_.store(true);return ok;
}
bool BridgeClient::loadBank(const std::string& path,int slot) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file) return false;
    const auto size=file.tellg();if(size<160 || size>std::streamoff(kMaxState+160)) return false;
    std::vector<uint8_t> bytes(size_t(size),uint8_t{});file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),size);
    if(!file || std::memcmp(bytes.data(),"CcnK",4)!=0 || std::memcmp(bytes.data()+8,"FBCh",4)!=0 || std::memcmp(bytes.data()+16,"CaS1",4)!=0) return false;
    return restore(std::vector<uint8_t>(bytes.begin()+160,bytes.end()),slot);
}
}
