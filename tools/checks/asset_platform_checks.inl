// Asset, file-writing and platform robustness checks from the 2026-10-08 audit (06 assets/platform, 10 security).
#include "../../deep/engine/event_log.h"
static void asset_platform_checks(const auto& require,const fs::path& folder){
    const fs::path dir=folder/"asset-platform-checks";std::error_code cleared;fs::remove_all(dir,cleared);fs::create_directories(dir);
    {
        // A-06-3: every save with backup used to leave one more "<file>.<id>.bak" forever; now only the newest few remain and only that exact name is ever removed.
        const auto target=dir/"scene.starscene";
        const std::vector<std::string> bystanders{"scene.starscene.bak","scene.starscene.20260101.bak","other.starscene."+std::string(32,'a')+".bak","scene.starscene."+std::string(32,'A')+".bak","scene.starscene."+std::string(32,'a')+".bak.keep"};
        for(const auto& name:bystanders)atomic_write(dir/name,"not ours");
        atomic_write(target,"version 0");
        for(int i=1;i<=8;++i){Sleep(20);atomic_write(target,"version "+std::to_string(i),true);}
        std::set<std::string> kept;size_t ours=0;const std::string head="scene.starscene.";
        for(const auto& entry:fs::directory_iterator(dir)){
            const auto name=entry.path().filename().string();
            if(name.size()==head.size()+32+4&&name.starts_with(head)&&name.ends_with(".bak")&&std::all_of(name.begin()+head.size(),name.begin()+head.size()+32,[](unsigned char c){return std::isdigit(c)||(c>='a'&&c<='f');})){++ours;kept.insert(read_file(entry.path()));}
        }
        require(ours==5&&kept==std::set<std::string>{"version 3","version 4","version 5","version 6","version 7"},"A-06-3: Repeated saves keep only the five newest backups of the file");
        bool bystandersKept=true;for(const auto& name:bystanders)bystandersKept=bystandersKept&&fs::exists(dir/name)&&read_file(dir/name)=="not ours";
        require(bystandersKept&&read_file(target)=="version 8","A-06-3: Pruning never touches a file that is not this program's own backup name, and the saved file is intact");
    }
    {
        // A-06-10: a failure to take the write lock used to be reported as "File is busy" whatever the cause.
        const auto contested=dir/"contested.txt";
        HANDLE holder=CreateFileW((contested.wstring()+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        bool busy=false;try{atomic_write(contested,"x");}catch(const std::exception& e){busy=std::string(e.what())=="File is busy";}
        require(holder!=INVALID_HANDLE_VALUE&&busy,"A-06-10: A file held by another writer is still reported as busy");
        std::thread release([holder]{Sleep(150);CloseHandle(holder);});
        bool waited=true;try{atomic_write(contested,"second writer");}catch(const std::exception&){waited=false;}
        release.join();
        require(waited&&read_file(contested)=="second writer","A-06-10: A writer that finds the lock held for a moment waits and then saves instead of failing");
        fs::create_directories(dir/"blocked.txt.lock");
        std::string why;try{atomic_write(dir/"blocked.txt","x");}catch(const std::exception& e){why=e.what();}
        require(why.find("Windows error")!=std::string::npos&&why.find("busy")==std::string::npos,"A-06-10: A lock that cannot be created for another reason reports the Windows error instead of 'busy'");
    }
}
