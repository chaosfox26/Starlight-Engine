#pragma once
#include "native_inventory.hpp"
namespace starlight::audit {
inline void plain_chain(const fs::path& root,const fs::path& selected){
    auto base=fs::absolute(root).lexically_normal(),path=fs::absolute(selected).lexically_normal();
    auto relative=path.lexically_relative(base);
    if(relative.empty() || relative==L"." || *relative.begin()==L"..")throw std::runtime_error("Cleanup target must be a child of its allowed root");
    for(auto p=path;;p=p.parent_path()){
        auto attributes=GetFileAttributesW(long_path(p).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || attributes&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("Cleanup refuses missing or redirected paths");
        if(p==base)break;
    }
}
inline std::vector<fs::path> cleanup_files(const fs::path& base){
    std::vector<fs::path> files;
    for(fs::recursive_directory_iterator it(long_path(base)),end;it!=end;++it){
        auto attributes=GetFileAttributesW(it->path().c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Cannot inspect cleanup candidate");
        if(attributes&FILE_ATTRIBUTE_REPARSE_POINT){it.disable_recursion_pending();continue;}
        if(!(attributes&FILE_ATTRIBUTE_DIRECTORY))files.push_back(it->path().lexically_relative(long_path(base)));
    }
    std::sort(files.begin(),files.end());return files;
}
inline json cleanup_plan(const fs::path& root,const fs::path& selected,const std::string& mode,const std::set<std::string>& preexisting={}){
    auto base=fs::absolute(selected).lexically_normal();
    plain_chain(root,base);
    plain_chain(root/(mode=="build"?"out":"analysis"),base);
    if(mode!="build" && mode!="report")throw std::runtime_error("Unknown cleanup mode");
    if(mode=="build"){
        std::istringstream input(read_file(base/"CMakeCache.txt"));std::string line;int homes=0;
        while(std::getline(input,line))if(line.starts_with("CMAKE_HOME_DIRECTORY:INTERNAL=")){
            if(!line.empty() && line.back()=='\r')line.pop_back();
            if(fs::weakly_canonical(fs::u8path(line.substr(std::string("CMAKE_HOME_DIRECTORY:INTERNAL=").size())))!=fs::canonical(root))throw std::runtime_error("Build cache belongs to another source tree");++homes;
        }
        if(homes!=1)throw std::runtime_error("Build cache must identify this source tree");
    }
    const std::set<std::string> keep={"audit-summary.json","audit-findings.json","run-state.json","runtime-evidence.json","audit-deliverable-hashes.json","final-status.txt","progress.txt","worker.log","failure.txt","summary-retention.json","capture-retention.json","capture-retention-warning.json"};
    json plan=json::array();
    for(const auto& relative:cleanup_files(base)){
        auto rel=utf8(relative.generic_wstring()),name=utf8(relative.filename().wstring()),ext=utf8(relative.extension().wstring());
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(mode=="build" && ext!=".obj" && ext!=".pch" && ext!=".ilk")continue;
        if(mode=="report" && (preexisting.contains(rel) || (relative.parent_path().empty() && (keep.contains(name) || ext==".md" || ext==".py"))))continue;
        if(mode=="report" && *relative.begin()==L"cleanup-records")continue;
        auto [hash,bytes]=digest(base/relative);
        plan.push_back({{"path",rel},{"bytes",bytes},{"sha256",hash},{"status","planned"}});
    }
    return plan;
}
inline json apply_cleanup(const fs::path& root,const fs::path& selected,json plan,const fs::path& log){
    auto base=fs::absolute(selected).lexically_normal();plain_chain(root,base);
    // The log is created exclusively before any deletion and is never a candidate.
    auto logPath=fs::absolute(log).lexically_normal();
    plain_chain(root,logPath.parent_path());
    starlight::editor::Handle journal(CreateFileW(long_path(logPath).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!journal)throw std::runtime_error("Cleanup journal already exists or cannot be created");
    auto save=[&]{auto text=json{{"files",plan}}.dump(2);LARGE_INTEGER zero{};
        if(!SetFilePointerEx(journal.value,zero,nullptr,FILE_BEGIN))throw std::runtime_error("Cannot seek cleanup journal");
        DWORD written=0;if(text.size()>MAXDWORD || !WriteFile(journal.value,text.data(),DWORD(text.size()),&written,nullptr) || written!=text.size() || !SetEndOfFile(journal.value) || !FlushFileBuffers(journal.value))throw std::runtime_error("Cannot persist cleanup journal");
    };save();
    for(auto& record:plan){try{
        auto relative=fs::u8path(record.at("path").get<std::string>());auto candidate=(base/relative).lexically_normal();
        plain_chain(base,candidate);
        if(candidate==logPath)throw std::runtime_error("Cleanup cannot remove its journal");
        // Deny concurrent replacement/writes while validating and deleting this exact file.
        starlight::editor::Handle file(CreateFileW(long_path(candidate).c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(!file)throw std::runtime_error("File is busy or unavailable; retained");
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(file.value,&info) || info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error("Candidate is no longer a regular file");
        std::wstring actual(32768,L'\0');DWORD length=GetFinalPathNameByHandleW(file.value,actual.data(),DWORD(actual.size()),FILE_NAME_NORMALIZED);
        if(!length || length>=actual.size())throw std::runtime_error("Cannot verify final file location");actual.resize(length);
        if(_wcsicmp(actual.c_str(),long_path(candidate).c_str())!=0)throw std::runtime_error("Candidate location changed");
        auto [hash,bytes]=digest(candidate);
        if(hash!=record.at("sha256").get<std::string>() || bytes!=record.at("bytes").get<uint64_t>())throw std::runtime_error("Candidate changed since planning; retained");
        FILE_DISPOSITION_INFO disposition{TRUE};
        if(!SetFileInformationByHandle(file.value,FileDispositionInfo,&disposition,sizeof(disposition)))throw std::runtime_error("Windows refused removal; retained");
        record["status"]="removed";
    }catch(const std::exception& error){record["status"]="retained";record["error"]=error.what();}save();}
    return plan;
}
inline json capture_retention_plan(const fs::path& root,const fs::path& current,size_t keep=3){
    if(keep<1)throw std::runtime_error("At least one recent run must be retained");
    auto base=root/"analysis";plain_chain(root,base);plain_chain(base,current);
    struct Run{fs::path path;json summary;fs::file_time_type modified;};std::vector<Run> runs;
    for(const auto& entry:fs::directory_iterator(base)){
        try{if(!entry.is_directory())continue;plain_chain(base,entry.path());
            plain_chain(entry.path(),entry.path()/"audit-summary.json");plain_chain(entry.path(),entry.path()/"progress.txt");
            auto summary=json::parse(read_file(entry.path()/"audit-summary.json"));std::istringstream input(read_file(entry.path()/"progress.txt"));std::string progress;std::getline(input,progress);if(!progress.empty() && progress.back()=='\r')progress.pop_back();
            if(progress!="100" || fs::weakly_canonical(fs::u8path(summary.value("root",std::string())))!=fs::canonical(root))continue;
            runs.push_back({entry.path(),summary,fs::last_write_time(entry.path()/"audit-summary.json")});
        }catch(const std::exception&){continue;}
    }
    std::stable_sort(runs.begin(),runs.end(),[](const Run& a,const Run& b){return a.modified>b.modified;});json plan=json::array();
    for(size_t i=keep;i<runs.size();++i){auto& run=runs[i];if(fs::equivalent(run.path,current) || !run.summary.contains("allRequestedStagesPassed") || run.summary["allRequestedStagesPassed"]!=true)continue;
        for(const auto& file:cleanup_files(run.path)){auto ext=utf8(file.extension().wstring());std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});if(ext!=".bmp")continue;
            auto [hash,bytes]=digest(run.path/file);plan.push_back({{"path",utf8((run.path/file).lexically_relative(base).generic_wstring())},{"bytes",bytes},{"sha256",hash},{"status","planned"}});
        }
    }return plan;
}
inline void remove_empty_report_directories(const fs::path& root,const fs::path& selected){
    plain_chain(root/"analysis",selected);std::vector<fs::path> directories;
    for(fs::recursive_directory_iterator it(long_path(selected)),end;it!=end;++it){auto attributes=GetFileAttributesW(it->path().c_str());if(attributes==INVALID_FILE_ATTRIBUTES)continue;
        if(attributes&FILE_ATTRIBUTE_REPARSE_POINT){it.disable_recursion_pending();continue;}if(attributes&FILE_ATTRIBUTE_DIRECTORY)directories.push_back(it->path().lexically_relative(long_path(selected)));
    }
    std::reverse(directories.begin(),directories.end());for(const auto& relative:directories){auto path=selected/relative;try{plain_chain(selected,path);RemoveDirectoryW(long_path(path).c_str());}catch(const std::exception&){} }
}
}
