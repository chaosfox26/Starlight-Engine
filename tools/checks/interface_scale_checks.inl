// Exercise the public zoom command, real HWND geometry and actual fonts.
{
    RECT saved{};GetWindowRect(window,&saved);
    const auto before=json(scene).dump();
    ensure_secondary_panels();ensure_asset_browser();show_help();command(HelpClose);
    bool reachable=true,fonts=true,searchLabel=true,preserved=true,noStatusOverlap=true;
    auto in=[](HWND child,HWND host){RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,host,reinterpret_cast<POINT*>(&r),2);return r;};
    for(int percent:{25,37,50,75,100,125,150,200,100}) {
        toggle_menu(0);toggle_menu(MenuFile);
        SendMessage(GetDlgItem(menuPanel,SettingsScale),BM_SETCHECK,BST_UNCHECKED,0);
        command(SettingsScale);
        auto zoomSlider=GetDlgItem(menuPanel,InterfaceSlider);SendMessage(zoomSlider,TBM_SETPOS,TRUE,percent);
        SendMessage(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(zoomSlider));toggle_menu(0);
        for(auto size:{SIZE{720,540},SIZE{1380,900}}) {
            SetWindowPos(window,nullptr,0,0,size.cx,size.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            show_help();SetWindowTextW(helpSearch,L"save");
            searchLabel &= text(helpSearchLabel)=="Search" && IsWindowVisible(helpSearchLabel);
            for(auto child:{helpSearch,helpSearchLabel,musicToggle,librarySearch,playButton}) {
                LOGFONTW f{};GetObjectW(reinterpret_cast<HFONT>(SendMessage(child,WM_GETFONT,0,0)),sizeof(f),&f);
                fonts &= f.lfHeight==-settings_px(12);
            }
            for(auto entry:std::array<std::pair<HWND,HWND>,4>{{{helpPanel,GetDlgItem(helpPanel,HelpRestart)},
                {musicPanel,GetDlgItem(musicPanel,MusicStop)},{libraryPanel,GetDlgItem(libraryPanel,InsertAsset)},
                {morePanel,GetDlgItem(morePanel,FeatureCheck)}}}) {
                auto [host,child]=entry;
                scroll_panel(host,0,0);auto target=in(child,host);RECT area{};GetClientRect(host,&area);
                scroll_panel(host,std::max(0L,target.right-area.right),std::max(0L,target.bottom-area.bottom));
                target=in(child,host);GetClientRect(host,&area);
                reachable &= target.left>=0 && target.top>=0 && target.right<=area.right && target.bottom<=area.bottom;
                auto outer=in(host,window);RECT root{};GetClientRect(window,&root);
                reachable &= outer.left>=0 && outer.top>=0 && outer.right<=root.right && outer.bottom<=root.bottom;
                RECT overlap{},statusBounds=in(status,window);
                noStatusOverlap &= !IntersectRect(&overlap,&outer,&statusBounds);
                scroll_panel(host,0,0);
            }
            command(HelpClose);
            preserved &= json(scene).dump()==before;
        }
    }
    check("interface zoom scales Help menus and secondary-panel fonts",fonts);
    check("interface zoom preserves visible Search label with entered text",searchLabel);
    check("large-panel actions remain reachable in small windows at every interface size",reachable);
    check("enlarged panels do not overlap the bottom status line",noStatusOverlap);
    check("interface-size changes preserve all authored and hidden scene settings",preserved);
    toggle_menu(0);toggle_menu(MenuFile);
    auto zoomSlider=GetDlgItem(menuPanel,InterfaceSlider);
    SendMessage(zoomSlider,TBM_SETPOS,TRUE,63);
    SendMessage(window,WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(zoomSlider));
    check("slider drag updates its percentage without moving beneath the pointer",settingsScale==100 && text(GetDlgItem(menuPanel,InterfaceReadout))=="63%");
    SendMessage(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(zoomSlider));
    check("slider release applies any one-percent value",settingsScale==63 && interfacePercent==63);
    SendMessage(GetDlgItem(menuPanel,SettingsScale),BM_SETCHECK,BST_CHECKED,0);command(SettingsScale);toggle_menu(0);
    bool autoFits=true;
    for(int i=0;i<4;++i){
        SetWindowPos(window,nullptr,0,0,720,540,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);const auto compactScale=settingsScale;
        SetWindowPos(window,nullptr,0,0,1380,900,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        autoFits &= compactScale<settingsScale && settingsScale==100 && interfacePercent==0;
    }
    check("Auto shrinks and restores the UI through repeated window resizes",autoFits);
    toggle_menu(MenuFile);SendMessage(GetDlgItem(menuPanel,SettingsScale),BM_SETCHECK,BST_UNCHECKED,0);command(SettingsScale);
    check("switching from Auto restores the chosen manual percentage",settingsScale==63);
    zoomSlider=GetDlgItem(menuPanel,InterfaceSlider);SendMessage(zoomSlider,TBM_SETPOS,TRUE,100);
    SendMessage(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(zoomSlider));toggle_menu(0);
    bool dpiFonts=true,dpiScene=true;
    for(UINT dpi:{120u,144u,192u,240u,96u}) {
        RECT proposed{saved.left,saved.top,saved.left+MulDiv(850,dpi,96),saved.top+MulDiv(650,dpi,96)};
        SendMessage(window,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&proposed));
        LOGFONTW f{};GetObjectW(font,sizeof(f),&f);
        dpiFonts &= f.lfHeight==-MulDiv(12,int(dpi),96);
        dpiScene &= json(scene).dump()==before;
    }
    check("DPI-change handler updates real fonts without changing Windows settings",dpiFonts);
    check("DPI-change handler preserves scene and hidden settings",dpiScene);
    report["injectedDpiMessages"]={120,144,192,240,96};
    SetWindowPos(window,nullptr,saved.left,saved.top,saved.right-saved.left,saved.bottom-saved.top,SWP_NOZORDER|SWP_NOACTIVATE);
    show_inspector(5);
}
