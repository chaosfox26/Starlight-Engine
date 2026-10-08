// Editor wave-2 audit findings (EDIT-1, EDIT-3): error text that is not valid UTF-8 must never throw out of the error handler or the command-line report.
static void editor_wave2_checks(const auto& require,const fs::path& folder){
    const auto work=folder/"editor-wave2-checks";fs::remove_all(work);fs::create_directories(work);
    const std::string ansi="bad \xE9 path";
    bool strictThrows=false;
    try{(void)wide(ansi);}catch(const std::exception&){strictThrows=true;}
    require(strictThrows,"EDIT-1: the strict wide() still refuses text that is not UTF-8 (scene and path code relies on it)");
    bool lossyThrows=false;std::wstring shown;
    try{shown=wide_lossy(ansi);}catch(...){lossyThrows=true;}
    require(!lossyThrows&&shown.size()==ansi.size()&&shown.starts_with(L"bad ")&&shown.ends_with(L" path"),"EDIT-1: wide_lossy shows error text that is not valid UTF-8 instead of throwing");
    require(wide_lossy("plain \xC3\xA9").ends_with(L"\x00E9")&&wide_lossy("").empty(),"EDIT-1: wide_lossy converts valid UTF-8 exactly and accepts empty text");
    const auto marker="edit1-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
    diagnostic(marker,ansi);
    bool written=false;
    std::error_code listing;
    for(fs::directory_iterator it(application_data()/L"Diagnostics"/L"NativeEditor",listing),end;!listing&&it!=end;it.increment(listing)){
        try{if(read_file(it->path()).find(marker)!=std::string::npos){written=true;break;}}catch(const std::exception&){}
    }
    require(written,"EDIT-1: a diagnostic whose message is not valid UTF-8 is still written");
    const auto bad=work/"bad.json";atomic_write(bad,std::string("{\"x\":\"bad \xE9\"}"));
    const auto result=work/"result.json";
    std::vector<std::wstring> args={L"Starlight.NativeCommands",L"--commands",bad.wstring(),L"--result",result.wstring()};
    std::vector<wchar_t*> argv;for(auto& a:args)argv.push_back(a.data());
    std::ostringstream captured;auto* old=std::cout.rdbuf(captured.rdbuf());
    int code=-1;
    try{code=run_commands(int(argv.size()),argv.data());}catch(...){}
    std::cout.rdbuf(old);
    require(code==1&&fs::is_regular_file(result),"EDIT-3: a command-line failure whose message holds bytes that are not UTF-8 still writes the JSON error report and exits 1");
    require(json::parse(read_file(result)).value("success",true)==false,"EDIT-3: that report says success is false");
}
