#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace starlight::audit {
template<class T>T data_value(const std::string& bytes,size_t offset){if(offset>bytes.size() || sizeof(T)>bytes.size()-offset)throw std::runtime_error("Truncated diagnostic data");T value;std::memcpy(&value,bytes.data()+offset,sizeof(value));return value;}
inline float half_float(uint16_t h){unsigned exponent=(h>>10)&31,mantissa=h&1023;float v=exponent==0?std::ldexp(float(mantissa),-24):exponent==31?(mantissa?NAN:INFINITY):std::ldexp(float(1024+mantissa),int(exponent)-25);return h&0x8000?-v:v;}
struct DiagnosticImage {
    size_t width=0,height=0,channels=0;std::vector<float> values;
    float at(size_t pixel,size_t channel)const {if(channel>=channels || pixel>=width*height)throw std::runtime_error("Diagnostic pixel outside image");return values[pixel*channels+channel];}
    float luma(size_t pixel)const{return at(pixel,0)*.2126f+at(pixel,1)*.7152f+at(pixel,2)*.0722f;}
};
inline DiagnosticImage decode_capture(const std::string& bytes){
    DiagnosticImage image;image.width=data_value<uint32_t>(bytes,0);image.height=data_value<uint32_t>(bytes,4);auto format=data_value<uint32_t>(bytes,8),row=data_value<uint32_t>(bytes,12);size_t element=(format==2 || format==41)?4:2;
    if(!image.width || !image.height || image.width>32768 || image.height>32768 || row%(image.width*element))throw std::runtime_error("Invalid diagnostic image layout");
    image.channels=row/(image.width*element);if(!image.channels || image.channels>4 || uint64_t(row)*image.height!=bytes.size()-16)throw std::runtime_error("Invalid diagnostic image size");
    image.values.resize(image.width*image.height*image.channels);
    for(size_t i=0;i<image.values.size();++i)image.values[i]=element==4?data_value<float>(bytes,16+i*4):half_float(data_value<uint16_t>(bytes,16+i*2));
    return image;
}
inline size_t preference_offset(const std::string& packet){size_t offset=12+size_t(data_value<uint32_t>(packet,8))*48+16;auto smoke=data_value<uint32_t>(packet,offset);offset+=4+size_t(smoke)*16+12;data_value<uint32_t>(packet,offset);return offset;}
// Only the fixed diagnostic fixture is upgraded. Older public scene packets
// retain their renderer compatibility semantics (no stable temporal identity).
inline std::string identified_diagnostic_packet(const std::string& packet){
    if(data_value<uint32_t>(packet,0)!=0x37564c53)throw std::runtime_error("Expected V7 diagnostic fixture");
    size_t offset=preference_offset(packet)+4;
    std::string upgraded=packet.substr(0,offset);
    const uint32_t version=0x39564c53,transient=0;
    std::memcpy(upgraded.data(),&version,4);
    const auto count=data_value<uint32_t>(packet,4);
    for(uint64_t i=0;i<count;++i){
        const size_t start=offset;
        offset+=40;
        const auto routes=data_value<uint32_t>(packet,offset);offset+=4+size_t(routes)*8+64;
        const auto vertices=data_value<uint32_t>(packet,offset);offset+=4+size_t(vertices)*40;
        const auto indices=data_value<uint32_t>(packet,offset);offset+=4+size_t(indices)*4;
        if(offset>packet.size())throw std::runtime_error("Truncated diagnostic fixture");
        const uint64_t identity=i+1;
        upgraded.append(reinterpret_cast<const char*>(&identity),8);
        upgraded.append(reinterpret_cast<const char*>(&transient),4);
        upgraded.append(packet,start,offset-start);
    }
    if(offset!=packet.size())throw std::runtime_error("Unexpected diagnostic fixture data");
    return upgraded;
}
inline std::pair<size_t,size_t> prop_offset(const std::string& packet){
    auto version=data_value<uint32_t>(packet,0),count=data_value<uint32_t>(packet,4);size_t offset=preference_offset(packet);if(version>=0x37564c53)offset+=4;
    if(version!=0x37564c53 && version!=0x39564c53)throw std::runtime_error("Unsupported diagnostic packet version");
    for(size_t i=0;i<count;++i){if(version==0x39564c53)offset+=12;auto start=offset;offset+=40;auto routes=data_value<uint32_t>(packet,offset);offset+=4+size_t(routes)*8+64;auto vertices=data_value<uint32_t>(packet,offset);offset+=4;auto kind=data_value<float>(packet,offset+36);if(kind==10){data_value<float>(packet,start+24);return {start+24,i};}offset+=size_t(vertices)*40;auto indices=data_value<uint32_t>(packet,offset);offset+=4+size_t(indices)*4;}
    throw std::runtime_error("Diagnostic rock missing from packet");
}
}
