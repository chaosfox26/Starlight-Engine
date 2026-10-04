#pragma once
#include "native_inventory.hpp"
#include <regex>
namespace starlight::audit {
inline json pe_summary_native(const fs::path& path){
    auto display=path;display.make_preferred();json result={{"path",utf8(display.wstring())}};
    try{auto bytes=read_file(long_path(path));if(bytes.substr(0,2)!="MZ")return nullptr;
        auto number=[&](size_t offset,size_t width)->uint32_t{if(offset>bytes.size() || width>bytes.size()-offset)throw std::runtime_error("Truncated PE data");uint32_t value=0;for(size_t i=0;i<width;++i)value|=uint32_t(static_cast<unsigned char>(bytes[offset+i]))<<(8*i);return value;};
        size_t pe=number(0x3c,4);if(pe>bytes.size() || bytes.size()-pe<24)throw std::runtime_error("Invalid PE offset");if(bytes.substr(pe,4)!=std::string("PE\0\0",4))return nullptr;
        auto machine=number(pe+4,2),count=number(pe+6,2),stamp=number(pe+8,4),optionalSize=number(pe+20,2);size_t optional=pe+24,sections=optional+optionalSize;auto magic=number(optional,2);size_t start=magic==0x20b?112:magic==0x10b?96:0;if(!start)throw std::runtime_error("Unknown PE optional header");
        if(optionalSize>bytes.size()-optional || uint64_t(count)*40>bytes.size()-sections)throw std::runtime_error("Truncated PE section table");
        auto directory=[&](size_t index){if(optionalSize<start+(index+1)*8)return std::pair<uint32_t,uint32_t>{0,0};return std::pair{number(optional+start+index*8,4),number(optional+start+index*8+4,4)};};
        auto raw=[&](uint32_t address)->size_t{for(size_t i=0;i<count;++i){auto section=sections+i*40;auto vs=number(section+8,4),va=number(section+12,4),rs=number(section+16,4),rp=number(section+20,4);if(address>=va && uint64_t(address)-va<std::max(vs,rs) && address-va<rs){uint64_t offset=uint64_t(rp)+address-va;if(offset>=bytes.size())throw std::runtime_error("PE address outside file");return size_t(offset);}}throw std::runtime_error("Unmapped RVA");};
        auto ascii=[&](uint32_t address){auto offset=raw(address);auto end=bytes.find('\0',offset);if(end==std::string::npos || end-offset>4096)throw std::runtime_error("Unterminated PE name");std::string name;for(auto i=offset;i<end;++i){auto c=static_cast<unsigned char>(bytes[i]);if(c<128)name+=char(c);else name+="\xef\xbf\xbd";}return name;};
        auto add=[](uint32_t address,uint64_t offset){if(uint64_t(address)+offset>UINT32_MAX)throw std::runtime_error("PE address overflow");return uint32_t(address+offset);};
        std::ostringstream hex;hex<<"0x"<<std::hex<<machine;result.update({{"machine",hex.str()},{"sections",count},{"coff_timestamp",stamp},{"pe32plus",magic==0x20b},{"clr_directory_present",directory(14).first!=0},{"imports",json::array()},{"exports",json::array()}});
        auto [imports,length]=directory(1);if(imports)for(size_t i=0;i<std::min<uint32_t>(length/20,4096);++i){auto at=raw(add(imports,i*20));uint32_t any=0;for(size_t j=0;j<5;++j)any|=number(at+j*4,4);if(!any)break;result["imports"].push_back(ascii(number(at+12,4)));}
        auto [exports,exportLength]=directory(0);if(exports){auto at=raw(exports);auto namesCount=number(at+24,4),names=number(at+32,4);for(size_t i=0;i<std::min<uint32_t>(namesCount,20000);++i)result["exports"].push_back(ascii(number(raw(add(names,i*4)),4)));}
    }catch(const std::exception& e){result["error"]=e.what();}return result;
}
struct EvidenceAnalysis {json binaries=json::array(),saved=json::array(),duplicates=json::array();};
inline EvidenceAnalysis analyze_evidence(const fs::path& root,const json& files){
    EvidenceAnalysis result;std::vector<std::string> paths;std::map<std::string,std::vector<std::string>> groups;
    std::regex selected(R"(^(audit-runs/.*\.json|out/classic/classic-ui-[^/]*\.json|migration/classic-completion-validation\.json|out/optimization-audit/layouts\.json)$)",std::regex::icase);
    for(const auto& file:files){auto rel=file.at("path").get<std::string>();auto path=root/fs::u8path(rel);auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(ext==".exe" || ext==".dll"){auto binary=pe_summary_native(path);if(!binary.is_null()){binary["sha256"]=file.value("sha256",json());result.binaries.push_back(std::move(binary));}}
        if(std::regex_match(rel,selected))paths.push_back(rel);
        if(file.contains("sha256") && file["sha256"].is_string() && !file["sha256"].get<std::string>().empty())groups[file["sha256"].get<std::string>()].push_back(rel);
    }
    auto folded=[](std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});return value;};
    std::sort(paths.begin(),paths.end(),[&](const auto& a,const auto& b){return folded(a)<folded(b);});
    for(const auto& rel:paths){auto path=root/fs::u8path(rel);try{
        if(fs::file_size(long_path(path))>2*1024*1024){result.saved.push_back({{"path",rel},{"error","Evidence exceeds 2 MiB interpretation limit; inventoried separately; review required"}});continue;}
        auto raw=read_file(long_path(path));auto data=json::parse(raw);auto hash=folded(hash_bytes(raw));result.saved.push_back({{"path",rel},{"sha256",hash},{"data",data},{"meaning","saved evidence only, not rerun or proof of source/binary equivalence"}});
    }catch(const std::exception& e){result.saved.push_back({{"path",rel},{"error",e.what()}});}}
    for(const auto& [hash,paths]:groups)if(paths.size()>1)result.duplicates.push_back({{"sha256",hash},{"paths",paths}});
    return result;
}
}
