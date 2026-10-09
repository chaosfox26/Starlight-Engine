#pragma once
#include "render_limits.hpp"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
namespace starlight::preset {
namespace fs=std::filesystem;
using json=nlohmann::json;
inline std::string u8text(const fs::path&p){auto s=p.u8string();return std::string(s.begin(),s.end());}
inline void write(const fs::path&p,const json&j){std::ofstream f(p);if(!f || !(f<<j.dump(2,' ',false,json::error_handler_t::replace)))throw std::runtime_error("Cannot write "+u8text(p));f.close();if(f.fail())throw std::runtime_error("Cannot write "+u8text(p));}
// A preset is read as text first: it is shared between people, and deeply nested values overflow the stack when json copies or compares them (audit LAUNCH-1). The real schema is 3 levels deep.
inline json read_limited(const fs::path&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot open "+u8text(p));std::string text((std::istreambuf_iterator<char>(f)),{});if(text.size()>(1u<<20))throw std::runtime_error("Preset file is too large");int depth=0;bool quoted=false,escaped=false;for(char c:text){if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}else if(c=='"')quoted=true;else if(c=='['||c=='{'){if(++depth>8)throw std::runtime_error("Preset file is nested too deeply");}else if(c==']'||c=='}')--depth;}return json::parse(text);}
inline void keys(const json&j,const std::set<std::string>&allowed){if(!j.is_object())throw std::runtime_error("Expected settings object");for(auto i=j.begin();i!=j.end();++i)if(!allowed.count(i.key()))throw std::runtime_error("Unknown setting: "+i.key()+". No safe migration is registered.");}
inline void validate(const json&p){
    keys(p,{"schemaVersion","name","settings"});if(p.at("schemaVersion")!=2)throw std::runtime_error("Unsupported preset version");
    if(p.contains("name")&&!p.at("name").is_string())throw std::runtime_error("Preset name must be text");
    auto&s=p.at("settings");keys(s,{"resolution","displayMode","frameLimit","durationMs","upscaler","quality","frameGeneration","vsync","culling","foreground"});
    auto res=s.at("resolution");if(!res.is_array()||res.size()!=2)throw std::runtime_error("Resolution needs width and height");
    for(auto&v:res)if(!v.is_number_integer()||v.get<double>()<64||v.get<double>()>16384)throw std::runtime_error("Invalid resolution");
    if(!starlight::valid_render_size(res[0].get<int>(),res[1].get<int>()))throw std::runtime_error(starlight::renderSizeError);
    for(auto k:{"frameLimit","durationMs"})if(!s.at(k).is_number_integer()||s.at(k).get<double>()<=0)throw std::runtime_error("Positive numeric test setting required");
    if(s.at("frameLimit").get<double>()>10000||s.at("durationMs").get<double>()>3600000)throw std::runtime_error("Test setting outside supported range");
    for(auto k:{"frameGeneration","vsync","culling","foreground"})if(!s.at(k).is_boolean())throw std::runtime_error("Boolean test setting required");
    std::string mode=s.at("displayMode"),up=s.at("upscaler"),quality=s.at("quality");
    if(mode!="fullscreen"&&mode!="windowed"&&mode!="borderless")throw std::runtime_error("Unknown display mode");
    if(up!="off"&&up!="dlss"&&up!="fsr"&&up!="xess"&&up!="auto")throw std::runtime_error("Unknown upscaler");
    if(quality!="native"&&quality!="quality"&&quality!="balanced"&&quality!="performance"&&quality!="ultraperf"&&quality!="ultraperformance")throw std::runtime_error("Unknown quality");
}
}
