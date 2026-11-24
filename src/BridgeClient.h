#pragma once
#include "BridgeProtocol.h"
#include <memory>
#include <mutex>

namespace cs {
class BridgeClient {
public:
    BridgeClient(const std::string& helper,const std::string& plugin,const std::string& data);
    ~BridgeClient();
    bool ready() const;
    const std::string& error() const { return error_; }
    int processId() const { return pid_; }
    int parameterCount() const { return info_.parameters; }
    int latency() const { return info_.latency; }
    std::string parameterName(int i) const;
    std::string parameterLabel(int i) const;
    std::string display(int i) const;
    float value(int i) const;
    void setValue(int i,float v);
    bool sync();
    bool randomize();
    bool clearPreset();
    bool updateDisplays();
    bool prepare(double rate,int frames);
    bool process(const float* const* input,float* const* output,int frames,const Transport&,
                 const Midi* midi=nullptr,int midiCount=0);
    bool loadBank(const std::string& path,int slot);
    std::vector<uint8_t> state();
    bool restore(const std::vector<uint8_t>& state,int slot=0);
private:
    bool request(Message msg,const void* payload=nullptr, std::vector<uint8_t>* result=nullptr,int timeout=1000);
    void fail(const std::string& reason);
    Shared* shared_=nullptr;
    Info info_{};
    char displays_[kParameters][64]{};
    mutable std::mutex displayMutex_;
    int socket_=-1,pid_=-1;
    std::atomic<bool> alive_{false};
    mutable std::timed_mutex ioMutex_;
    std::atomic<bool> displaysDirty_{true};
    std::string error_;
};
}
