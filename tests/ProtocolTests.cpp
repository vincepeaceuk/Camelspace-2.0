#include "BridgeProtocol.h"
#include <iostream>
static std::vector<uint8_t> record(size_t nameLength,float value) {
    std::vector<uint8_t> result;
    auto append=[&](uint32_t word){for(int i=0;i<4;++i)result.push_back(uint8_t(word>>(i*8)));};
    append(cs::bits(1));result.insert(result.end(),nameLength,'X');result.push_back(0);append(344);
    for(int i=0;i<344;++i)append(cs::bits(value));return result;
}
int main(){
    int failed=0;auto check=[&](bool good,const char* name){std::cout<<(good?"PASS ":"FAIL ")<<name<<"\n";failed+=!good;};
    check(cs::validRecords(record(63,0.5f),1),"original 64-byte name buffer accepts 63 characters plus NUL");
    check(!cs::validRecords(record(64,0.5f),1),"overlong name rejected before unchecked legacy readString");
    check(!cs::validRecords(record(23,1.25f),1),"out-of-range parameter rejected before legacy enum/index use");
    check(!cs::validRecords(record(23,-0.01f),1),"negative parameter rejected");
    check(!cs::validRecords(record(23,std::numeric_limits<float>::quiet_NaN()),1),"NaN parameter rejected");
    return failed?1:0;
}
