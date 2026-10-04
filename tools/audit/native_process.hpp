#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
#include <functional>
#include <chrono>
#include <map>
namespace starlight::audit {
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;
    explicit Handle(HANDLE h):value(h){}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
inline void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
inline std::wstring quote_argument(const std::wstring& arg) {
    std::wstring out=L"\"";size_t slashes=0;
    for(auto c:arg){if(c==L'\\'){++slashes;continue;}out.append(c==L'"'?slashes*2+1:slashes,L'\\');slashes=0;out+=c;}
    out.append(slashes*2,L'\\');return out+L'"';
}
struct ProcessResult {DWORD exitCode=0;bool timedOut=false;double elapsedSeconds=0;};
inline ProcessResult run(const std::filesystem::path& executable,const std::vector<std::wstring>& args,
                         const std::filesystem::path& directory,const std::filesystem::path& log,
                         DWORD timeoutMs,const std::function<void(double)>& progress={},
                         const std::map<std::wstring,std::wstring>& overrides={}) {
    require(executable.is_absolute() && directory.is_absolute() && log.is_absolute(),"Audit process paths must be absolute");
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle output(CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    require(output.value!=INVALID_HANDLE_VALUE,"Cannot create fresh audit log");
    Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr));
    require(input.value!=INVALID_HANDLE_VALUE,"Cannot open process input");
    Handle job(CreateJobObjectW(nullptr,nullptr));require(job.value!=nullptr,"Cannot create audit process job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    require(SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"Cannot protect audit process lifetime");
    SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);
    std::vector<unsigned char> storage(size);auto attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    require(InitializeProcThreadAttributeList(attributes,1,0,&size),"Cannot initialize process attributes");
    struct Attributes {LPPROC_THREAD_ATTRIBUTE_LIST p;~Attributes(){DeleteProcThreadAttributeList(p);}} cleanup{attributes};
    HANDLE inherited[]={input.value,output.value};
    require(UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr),"Cannot restrict inherited handles");
    STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof(start);start.lpAttributeList=attributes;
    start.StartupInfo.dwFlags=STARTF_USESTDHANDLES;start.StartupInfo.hStdInput=input.value;start.StartupInfo.hStdOutput=start.StartupInfo.hStdError=output.value;
    std::wstring command=quote_argument(executable.wstring());for(const auto& arg:args)command+=L" "+quote_argument(arg);
    struct LessInsensitive {bool operator()(const std::wstring& a,const std::wstring& b)const{return _wcsicmp(a.c_str(),b.c_str())<0;}};
    std::map<std::wstring,std::wstring,LessInsensitive> environment;
    auto inheritedEnvironment=GetEnvironmentStringsW();require(inheritedEnvironment!=nullptr,"Cannot read environment");
    for(auto p=inheritedEnvironment;*p;p+=wcslen(p)+1){std::wstring entry=p;auto equals=entry.find(L'=',entry[0]==L'='?1:0);if(equals!=std::wstring::npos)environment[entry.substr(0,equals)]=entry.substr(equals+1);}
    FreeEnvironmentStringsW(inheritedEnvironment);
    for(const auto& item:overrides){require(!item.first.empty() && item.first.find(L'=')==std::wstring::npos && item.first.find(L'\0')==std::wstring::npos && item.second.find(L'\0')==std::wstring::npos,"Invalid environment override");environment[item.first]=item.second;}
    std::wstring environmentBlock;for(const auto& item:environment){environmentBlock+=item.first+L"="+item.second;environmentBlock.push_back(L'\0');}environmentBlock.push_back(L'\0');
    PROCESS_INFORMATION process{};auto began=std::chrono::steady_clock::now();
    require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT,environmentBlock.data(),directory.c_str(),&start.StartupInfo,&process),"Cannot start audit executable");
    Handle child(process.hProcess),thread(process.hThread);
    if(!AssignProcessToJobObject(job.value,child.value)){TerminateProcess(child.value,2);WaitForSingleObject(child.value,5000);throw std::runtime_error("Cannot contain audit child process");}
    // Pass existing foreground permission to the child without activating a controller window.
    AllowSetForegroundWindow(process.dwProcessId);
    require(ResumeThread(thread.value)!=DWORD(-1),"Cannot resume audit executable");
    ProcessResult result;
    for(;;){
        auto wait=WaitForSingleObject(child.value,100);
        result.elapsedSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
        if(wait==WAIT_OBJECT_0)break;
        require(wait==WAIT_TIMEOUT,"Cannot observe audit executable");
        if(result.elapsedSeconds*1000>=timeoutMs){result.timedOut=true;TerminateJobObject(job.value,1460);WaitForSingleObject(child.value,5000);break;}
        if(progress)progress(result.elapsedSeconds);
    }
    require(GetExitCodeProcess(child.value,&result.exitCode),"Cannot read audit exit status");
    return result;
}
}
