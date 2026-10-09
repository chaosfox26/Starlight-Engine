// The mesh identity mix must be stable, must change for any single changed byte whatever the length, and must depend on the running hash.
#include "../../deep/engine/mesh_mix.h"
static void mesh_mix_checks(const auto& require){
    uint32_t seed=97531u;auto random=[&]{seed=seed*1664525u+1013904223u;return uint8_t(seed>>24);};
    const uint64_t basis=1469598103934665603ull;
    bool stable=true,sensitive=true;
    std::vector<uint8_t> bytes,copy;
    for(size_t length=0;length<=72;++length){
        bytes.resize(length);for(auto& b:bytes)b=random();
        const uint64_t base=mesh_mix_bytes(basis,bytes.data(),length);
        copy=bytes;if(mesh_mix_bytes(basis,copy.data(),length)!=base)stable=false;
        for(size_t at=0;at<length;++at)for(uint8_t flip:{uint8_t(1),uint8_t(0x80),uint8_t(0xff)}){
            copy=bytes;copy[at]^=flip;
            if(mesh_mix_bytes(basis,copy.data(),length)==base)sensitive=false;
        }
    }
    require(stable,"Mesh mix: equal bytes give equal hashes");
    require(sensitive,"Mesh mix: changing any single byte changes the hash, at every length from 0 to 72");
    const uint8_t sample[8]{1,2,3,4,5,6,7,8};
    require(mesh_mix_bytes(1,sample,8)!=mesh_mix_bytes(2,sample,8),"Mesh mix: the running hash takes part in the result");
}
