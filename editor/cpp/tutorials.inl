// Optional, non-modal guidance shared by all editor experiences.
struct Lesson { const char* id; const wchar_t* title; std::vector<const wchar_t*> steps; };
static const std::vector<Lesson>& lessons() {
    static const std::vector<Lesson> items{
        {"start",L"Start here", {L"You can explore at your own pace. Choose a lesson above, or close this panel and come back through Help / Tutorials or F1. Nothing in this guide changes your project.", L"The hamburger menu contains scene and project actions. The top-right controls open building tools, characters, effects, and More. Save your scene before experimenting.", L"Beginner keeps controls simple. Advanced reveals more settings. Walk and build is currently a preview, not the complete Interactive mode. Help remains available in each experience."}},
        {"save",L"Save and reopen your work", {L"Open the hamburger menu and choose Save scene. Choose a file location when asked. Saving keeps your authored scene; it does not export a standalone game.", L"Use Open scene to return to a saved scene. If you have unsaved changes, read the save/discard prompt carefully before replacing your work."}},
        {"objects",L"Place and move objects", {L"Open the building tools or Characters menu and choose an object to place. Select an object before changing its properties.", L"Use Move, Rotate, or Scale to adjust your selection. Undo reverses an edit; Redo restores it. Grid snapping helps align objects. Advanced settings expose grid spacing and rotation increments."}},
        {"assets",L"Find and reuse assets", {L"Choose Browse assets from the hamburger menu, then choose your asset folder. Use the search and type filter to narrow the list before inserting an asset.", L"You can also copy selected objects and paste them into another scene. Save a reusable shape or group from the hamburger menu when you want to use it again later."}},
        {"adventure",L"Try the starter adventure", {L"Save your current work first. New starter adventure in the hamburger menu creates a small key, door, and exit scene. It asks before discarding unsaved work.", L"Press Play. Focus the viewport, use WASD to move, and hold the right mouse button to look. Approach the key and press E, then use E at the door and exit.", L"Gameplay uses a test copy. Stop returns to authoring without permanently collecting the key or removing the door. The current controller has basic static collision; jumping and stairs are not supported."}},
        {"walk-build",L"Walk and build (preview)", {L"Choose Walk and build (preview) in the hamburger menu. Focus the viewport to move with WASD and hold the right mouse button to look. Existing authoring tools remain available.", L"Place or edit objects, then use Play to test gameplay in a separate copy. Pause returns to building. Return to editor view leaves the walking preview. Objectives, XP, and unlocks are planned, not available yet."}},
        {"interactions",L"Set up gameplay interactions", {L"Enable adventure controls from the hamburger menu. In object properties, choose a key, door, or exit role and apply the interaction. A locked door needs the same key name as its key.", L"Open the adventure settings and choose Check gameplay to inspect common setup problems. This check does not prove that every part of your game is reachable. Play through it yourself."}},
        {"export",L"Export a playable game", {L"Save your scene and test its adventure controls and interactions. Choose Export playable game from the hamburger menu and select a parent folder. The exporter creates a new StarlightGame folder and will not overwrite an existing one.", L"Keep the whole exported folder together when sharing it. The current standalone export targets Windows. Test the exported game separately; other platforms and clean-machine compatibility are not yet fully verified."}}
    };
    return items;
}
static std::vector<TutorialSection> help_catalog() {
    std::vector<TutorialSection> catalog;
    for(const auto& lesson:lessons())catalog.push_back({lesson.id,lesson.steps.size()});
    return catalog;
}
HWND helpButton{}, helpPanel{}, helpPicker{}, helpSearch{}, helpSearchLabel{}, helpText{}, helpProgress{};
bool helpShown=false, helpProgressLoaded=false;
bool helpProgressSaved=true;
fs::path helpProgressOverride; // Isolated walkthroughs must not change user preferences.
size_t helpLesson=0;
std::vector<size_t> helpMatches;
bool filteringHelp=false;
std::vector<size_t> helpSteps=std::vector<size_t>(lessons().size(),0);
fs::path help_progress_path() {
    if(!helpProgressOverride.empty())return helpProgressOverride;
    wchar_t base[32768]{};
    auto count=GetEnvironmentVariableW(L"LOCALAPPDATA",base,DWORD(std::size(base)));
    if(!count || count>=std::size(base)) return {};
    return fs::path(base)/L"Starlight"/L"tutorial-progress.txt";
}
void load_help_progress() {
    if(helpProgressLoaded)return;
    helpProgressLoaded=true;
    try {
        auto path=help_progress_path();
        if(path.empty() || !fs::exists(path) || fs::file_size(path)>65536)return;
        auto progress=decode_tutorial_progress(help_catalog(),read_file(path));
        helpLesson=progress.selected;helpSteps=std::move(progress.positions);
    } catch(...) { /* Guidance remains usable if local preferences are unavailable. */ }
}
bool save_help_progress() {
    try {
        auto path=help_progress_path(); if(path.empty())return false;
        fs::create_directories(path.parent_path());
        TutorialProgress progress(lessons().size());
        progress.selected=helpLesson;progress.positions=helpSteps;
        atomic_write(path,encode_tutorial_progress(help_catalog(),progress)); return true;
    } catch(...) {return false;}
}
void update_help(bool save=true) {
    const auto& lesson=lessons()[helpLesson];
    const auto step=helpSteps[helpLesson];
    SetWindowTextW(helpText,lesson.steps[step]);
    auto match=std::find(helpMatches.begin(),helpMatches.end(),helpLesson);
    SendMessageW(helpPicker,CB_SETCURSEL,match==helpMatches.end()?-1:std::distance(helpMatches.begin(),match),0);
    auto progress=L"Step "+std::to_wstring(step+1)+L" of "+std::to_wstring(lesson.steps.size());
    if(save)helpProgressSaved=save_help_progress();
    if(!helpProgressSaved)progress+=L" - saved for this session only";
    SetWindowTextW(helpProgress,progress.c_str());
    EnableWindow(GetDlgItem(helpPanel,HelpBack),step>0);
    EnableWindow(GetDlgItem(helpPanel,HelpNext),step+1<lesson.steps.size());
}
void filter_help() {
    if(!helpPicker || filteringHelp)return;
    filteringHelp=true;
    auto query=wide(text(helpSearch));
    auto lower=[](std::wstring value){for(auto& ch:value)ch=wchar_t(towlower(ch));return value;};
    query=lower(query);helpMatches.clear();
    SendMessageW(helpPicker,WM_SETREDRAW,FALSE,0);
    SendMessageW(helpPicker,CB_RESETCONTENT,0,0);
    for(size_t i=0;i<lessons().size();++i){
        std::wstring searchable=lessons()[i].title;
        for(auto line:lessons()[i].steps){searchable+=L" ";searchable+=line;}
        if(!query.empty() && lower(searchable).find(query)==std::wstring::npos)continue;
        helpMatches.push_back(i);
        SendMessageW(helpPicker,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(lessons()[i].title));
    }
    EnableWindow(helpPicker,!helpMatches.empty());
    SendMessageW(helpPicker,WM_SETREDRAW,TRUE,0);InvalidateRect(helpPicker,nullptr,FALSE);
    update_help(false);
    if(helpMatches.empty())SetWindowTextW(helpProgress,L"No matching lessons. Clear search to see all.");
    filteringHelp=false;
}
void layout_help() {
    auto p=[&](int n){return settings_px(n);};
    const int helpX=p(fullScreen?56:60);
    if(helpButton)SetWindowPos(helpButton,HWND_TOP,helpX,p(fullScreen?12:18),
        std::max(p(80),std::min(p(136),width-p(194)-helpX)),p(30),SWP_NOACTIVATE);
    if(!helpPanel)return;
    unscroll_panel(helpPanel);
    const int w=std::min(p(480),width-60),h=p(360);
    SetWindowPos(helpPanel,HWND_TOP,p(18),p(fullScreen?44:52),w,h,SWP_NOACTIVATE);
    MoveWindow(helpSearch,p(80),p(48),w-p(96),p(28),TRUE);
    MoveWindow(helpPicker,p(16),p(84),w-p(32),p(260),TRUE);
    // Keep navigation in one row; the lesson itself scrolls at compact sizes.
    MoveWindow(helpText,p(16),p(124),w-p(32),std::max(p(24),h-p(212)),TRUE);
    MoveWindow(helpProgress,p(16),h-p(78),w-p(32),p(22),TRUE);
    const int navWidth=std::min(p(90),(w-p(48))/3);
    MoveWindow(GetDlgItem(helpPanel,HelpBack),p(16),h-p(46),navWidth,p(30),TRUE);
    MoveWindow(GetDlgItem(helpPanel,HelpNext),p(24)+navWidth,h-p(46),navWidth,p(30),TRUE);
    MoveWindow(GetDlgItem(helpPanel,HelpRestart),p(32)+2*navWidth,h-p(46),navWidth,p(30),TRUE);
    MoveWindow(GetDlgItem(helpPanel,HelpClose),w-p(92),p(12),p(76),p(28),TRUE);
    finish_scroll_panel(helpPanel,18,p(fullScreen?44:52),w,h);
    ShowWindow(helpPanel,helpShown?SW_SHOW:SW_HIDE);
}

void show_help() {
    load_help_progress();
    if(!helpPanel) {
        helpPanel=panel(18,62,480,360);
        control(helpPanel,L"STATIC",L"HELP / TUTORIALS",0,16,18,200,24);
        control(helpPanel,L"BUTTON",L"Close",HelpClose,388,12,76,28);
        helpSearchLabel=control(helpPanel,L"STATIC",L"Search",0,16,53,60,22);
        helpSearch=control(helpPanel,L"EDIT",L"",HelpSearch,80,48,384,28,ES_AUTOHSCROLL);
        SendMessageW(helpSearch,EM_SETLIMITTEXT,256,0);
        SendMessageW(helpSearch,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Find a lesson (for example: save or door)"));
        helpPicker=control(helpPanel,L"COMBOBOX",L"",HelpLesson,16,48,448,260,CBS_DROPDOWNLIST|WS_VSCROLL);

        helpText=control(helpPanel,L"EDIT",L"",0,16,88,448,184,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL);
        helpProgress=control(helpPanel,L"STATIC",L"",0,16,282,448,22);
        control(helpPanel,L"BUTTON",L"Back",HelpBack,16,314,72,30);
        control(helpPanel,L"BUTTON",L"Next",HelpNext,96,314,72,30);
        control(helpPanel,L"BUTTON",L"Restart",HelpRestart,176,314,90,30);
    }
    helpShown=true; SetWindowTextW(helpSearch,L""); filter_help(); layout_help(); SetFocus(helpSearch);
}
bool help_command(int id) {
    if(id==HelpOpen){show_help();return true;}
    if(id==HelpClose){helpShown=false;ShowWindow(helpPanel,SW_HIDE);SetFocus(view);return true;}
    if(id<HelpLesson || id>HelpRestart || !helpPanel)return false;
    if(filteringHelp)return true;
    auto& step=helpSteps[helpLesson];
    if(id==HelpLesson){auto selectedLesson=SendMessageW(helpPicker,CB_GETCURSEL,0,0);if(selectedLesson>=0 && size_t(selectedLesson)<helpMatches.size())helpLesson=helpMatches[size_t(selectedLesson)];}
    if(id==HelpBack && step>0)--step;
    if(id==HelpNext && step+1<lessons()[helpLesson].steps.size())++step;
    if(id==HelpRestart)step=0;
    update_help();return true;
}
