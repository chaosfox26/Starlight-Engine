#pragma once
#include "../../editor/cpp/platform.hpp"
#include <bcrypt.h>
#include <map>
#include <set>
#include <iomanip>
#include <sstream>
namespace starlight::audit {
using namespace starlight::editor;
inline fs::path long_path(const fs::path& p){if(p.wstring().rfind(L"\\\\?\\",0)==0)return p;auto s=fs::absolute(p).lexically_normal().wstring();if(s.rfind(L"\\\\?\\",0)!=0)s=s.rfind(L"\\\\",0)==0?L"\\\\?\\UNC\\"+s.substr(2):L"\\\\?\\"+s;return s;}
inline std::pair<std::string,uint64_t> digest(const fs::path& path){
    starlight::editor::Handle file(CreateFileW(long_path(path).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));if(!file)throw std::runtime_error("Cannot open file for hashing");
    BCRYPT_HASH_HANDLE handle=nullptr;if(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&handle,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("Cannot create hash");
    struct Hash {BCRYPT_HASH_HANDLE h;~Hash(){BCryptDestroyHash(h);}} cleanup{handle};std::vector<unsigned char> buffer(4*1024*1024);uint64_t count=0;
    for(;;){DWORD n=0;if(!ReadFile(file.value,buffer.data(),DWORD(buffer.size()),&n,nullptr))throw std::runtime_error("Cannot read file");if(!n)break;if(BCryptHashData(handle,buffer.data(),n,0)<0)throw std::runtime_error("Cannot hash bytes");count+=n;}
    unsigned char bytes[32];if(BCryptFinishHash(handle,bytes,32,0)<0)throw std::runtime_error("Cannot finish hash");std::ostringstream text;for(auto b:bytes)text<<std::hex<<std::setw(2)<<std::setfill('0')<<int(b);return {text.str(),count};
}
inline json file_stamp(const fs::path& p){WIN32_FILE_ATTRIBUTE_DATA data{};if(!GetFileAttributesExW(long_path(p).c_str(),GetFileExInfoStandard,&data))throw std::runtime_error("Cannot read file attributes");uint64_t ticks=(uint64_t(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime;return {{"size",(uint64_t(data.nFileSizeHigh)<<32)|data.nFileSizeLow},{"mtime_ns",(int64_t(ticks)-116444736000000000LL)*100},{"reparse",bool(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)}};}
inline std::string category(const std::string& relative){
    std::vector<std::string> parts;std::stringstream input(relative);std::string part;while(std::getline(input,part,'/'))parts.push_back(part);auto lower=relative;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
    auto ext=fs::u8path(lower).extension().string();std::set<std::string> source={".cpp",".c",".h",".hpp",".inl",".hlsl",".py",".cmake",".bat",".cmd",".ps1"};auto has=[&](const std::string& value){return std::find(parts.begin(),parts.end(),value)!=parts.end();};
    if(parts[0]==".git")return "git metadata";
    if(parts.size()>1 && parts[0]=="Starlight Documents" && parts[1]=="Current")return "current project documentation";
    if(parts[0]=="Starlight Documents")return "outdated historical documents (user-designated)";
    if(has("third_party") || relative=="deep/src/json.hpp" || lower.starts_with("tools/toolchain/"))return "third-party dependency";
    if(has("backups") || has("_backups") || has("results") || has("source-snapshot") || relative.find(".before-")!=std::string::npos || lower.ends_with(".bak") || lower.ends_with(".backup") || parts[0]=="deep-incomplete-first")return "historical snapshot/experiment";
    if(parts[0]=="audit-runs")return "saved audit evidence";
    if(relative.starts_with("tools/build/") && source.count(ext))return "current source candidate";
    for(auto p:{"out","build","bin","build-native","generated","__pycache__"})if(has(p))return "build/generated/binary artifacts";
    for(auto p:{"tools/checks/","tools/diagnostics/","tools/build/","tools/render/","tools/profiling/","editor/cpp/","editor/native/","deep/engine/","deep/Physics/","deep/src/","deep/projects/"})if(relative.starts_with(p) && (source.count(ext) || parts.back()=="CMakeLists.txt"))return "current source candidate";
    if((parts[0]=="tools" || parts[0]=="migration") && source.count(ext))return "analysis/migration tooling";
    if(ext==".md" || ext==".json" || ext==".txt" || ext==".log")return "documentation/configuration/evidence";
    return "other asset or repository file";
}
inline std::map<std::string,json> enumerate(const fs::path& root,json& issues){
    std::map<std::string,json> result;std::vector<fs::path> pending={fs::path()};
    while(!pending.empty()){auto relative=pending.back();pending.pop_back();WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW((long_path(root/relative)/L"*").c_str(),&data);if(find==INVALID_HANDLE_VALUE){issues.push_back({{"phase","enumeration"},{"path",utf8(relative.generic_wstring())},{"error","Cannot enumerate directory"}});continue;}
        do{std::wstring name=data.cFileName;if(name==L"." || name==L"..")continue;auto path=relative/name;auto rel=utf8(path.generic_wstring());bool directory=data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY;auto folded=name;std::transform(folded.begin(),folded.end(),folded.begin(),::towlower);
            if(relative.empty() && directory && (folded==L"analysis" || folded==L"publication" || folded==L"_quarantine"))continue;
            if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){issues.push_back({{"phase","enumeration"},{"path",rel},{"error","Reparse point not followed; external content outside coverage"}});continue;}
            if(directory)pending.push_back(path);else try{result[rel]=file_stamp(root/path);}catch(const std::exception& e){issues.push_back({{"phase","enumeration"},{"path",rel},{"error",e.what()}});}
        }while(FindNextFileW(find,&data));auto error=GetLastError();FindClose(find);if(error!=ERROR_NO_MORE_FILES)issues.push_back({{"phase","enumeration"},{"path",utf8(relative.generic_wstring())},{"error","Directory enumeration incomplete"}});
    }return result;
}
inline json streams(const fs::path& path){WIN32_FIND_STREAM_DATA data{};HANDLE find=FindFirstStreamW(long_path(path).c_str(),FindStreamInfoStandard,&data,0);if(find==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_HANDLE_EOF)return json::array();throw std::runtime_error("Cannot enumerate NTFS streams");}json result=json::array();do{if(std::wstring(data.cStreamName)!=L"::$DATA")result.push_back({{"name",utf8(data.cStreamName)},{"bytes",data.StreamSize.QuadPart}});}while(FindNextStreamW(find,&data));auto error=GetLastError();FindClose(find);if(error!=ERROR_HANDLE_EOF)throw std::runtime_error("NTFS stream enumeration incomplete");return result;}
inline json hash_entry(const fs::path& root,const std::string& relative,const json& stamp){
    auto path=root/fs::u8path(relative);json entry={{"path",relative},{"bytes_at_enumeration",stamp.at("size")},{"mtime_ns",stamp.at("mtime_ns")},{"category",category(relative)}};
    try{auto before=file_stamp(path);if(before.at("reparse").get<bool>())throw std::runtime_error("File became a reparse point");auto [hash,bytes]=digest(path);auto after=file_stamp(path);entry["sha256"]=hash;entry["bytes_read"]=bytes;entry["stable"]=stamp==before && before==after && bytes==stamp.at("size").get<uint64_t>();entry["streams"]=json::array();
        try{auto original=streams(path);for(const auto& stream:original){auto name=stream.at("name").get<std::string>();auto [streamHash,count]=digest(fs::path(path.wstring()+wide(name)));entry["streams"].push_back({{"name",name},{"bytes_expected",stream.at("bytes")},{"bytes_read",count},{"sha256",streamHash},{"size_matches",count==stream.at("bytes").get<uint64_t>()}});}entry["streams_enumerated"]=true;entry["streams_stable"]=original==streams(path);}catch(const std::exception& e){entry["stream_error"]=e.what();}
    }catch(const std::exception& e){entry["error"]=e.what();}return entry;
}
}
