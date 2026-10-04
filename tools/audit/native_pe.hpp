#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
#include <algorithm>
#include "../../deep/src/json.hpp"
namespace starlight::audit {
inline nlohmann::json inspect_pe(const std::string& bytes) {
    auto number=[&](size_t offset,size_t width)->uint32_t{if(offset>bytes.size() || width>bytes.size()-offset)throw std::runtime_error("Truncated PE data");uint32_t value=0;for(size_t i=0;i<width;++i)value|=uint32_t(static_cast<unsigned char>(bytes[offset+i]))<<(8*i);return value;};
    if(bytes.size()<64 || bytes.substr(0,2)!="MZ")throw std::runtime_error("Missing PE DOS header");
    size_t pe=number(0x3c,4);if(pe>bytes.size() || bytes.substr(pe,4)!=std::string("PE\0\0",4))throw std::runtime_error("Missing PE signature");
    auto machine=number(pe+4,2),count=number(pe+6,2);size_t optional=pe+24;auto magic=number(optional,2);
    if(magic!=0x20b && magic!=0x10b)throw std::runtime_error("Unknown PE optional header");
    size_t directories=optional+(magic==0x20b?112:96),sections=optional+number(pe+20,2);
    auto raw=[&](uint32_t rva)->size_t{if(rva<number(optional+60,4) && rva<bytes.size())return rva;for(size_t i=0;i<count;++i){auto s=sections+i*40;auto vs=number(s+8,4),va=number(s+12,4),rs=number(s+16,4),rp=number(s+20,4);if(rva>=va && uint64_t(rva)<uint64_t(va)+(std::max)(vs,rs)){auto offset=uint64_t(rp)+rva-va;if(offset>=bytes.size())break;return size_t(offset);}}throw std::runtime_error("Invalid PE RVA");};
    auto clrRva=number(directories+14*8,4),clrSize=number(directories+14*8+4,4);
    nlohmann::json imports=nlohmann::json::array();auto importRva=number(directories+8,4);bool managed=false;
    if(importRva){size_t cursor=raw(importRva);for(;;cursor+=20){uint32_t any=0;for(size_t i=0;i<5;++i)any|=number(cursor+i*4,4);if(!any)break;auto start=raw(number(cursor+12,4));auto end=bytes.find('\0',start);if(end==std::string::npos)throw std::runtime_error("Unterminated PE import");auto name=bytes.substr(start,end-start);imports.push_back(name);std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});for(auto marker:{"mscoree","coreclr","hostfxr","hostpolicy","clr.dll"})if(name.find(marker)!=std::string::npos)managed=true;}}
    return {{"x64",machine==0x8664},{"clr",nlohmann::json::array({clrRva,clrSize})},{"imports",imports},{"passed",machine==0x8664 && clrRva==0 && clrSize==0 && !managed}};
}
}
