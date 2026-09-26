// Real child-window geometry checks in the isolated editor process.
{
    RECT saved{}; GetWindowRect(window, &saved);
    const auto sceneBefore = json(scene).dump();
    auto rectIn = [](HWND child, HWND host) {
        RECT r{}; GetWindowRect(child, &r);
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&r), 2); return r;
    };
    bool reachable = true, noDrift = true, withinPanel = true;
    for (const auto size : {SIZE{720,540}, SIZE{1050,720}, SIZE{1380,900}}) {
        SetWindowPos(window, nullptr, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        for (int page : {1,4,5}) {
            show_inspector(page);
            auto first = rectIn(fields.front(), inspector);
            SendMessageW(inspector, WM_VSCROLL, SB_BOTTOM, 0);
            RECT viewport{}; GetClientRect(inspector, &viewport);
            auto last = rectIn(fields.back(), inspector);
            reachable &= last.top >= 0 && last.bottom <= viewport.bottom && last.right <= viewport.right;
            auto outer = rectIn(propertiesPanel, window); RECT client{}; GetClientRect(window, &client);
            withinPanel &= outer.left >= 0 && outer.top >= 0 && outer.right <= client.right && outer.bottom <= client.bottom;
            SendMessageW(inspector, WM_VSCROLL, SB_TOP, 0);
            auto returned = rectIn(fields.front(), inspector);
            noDrift &= EqualRect(&first, &returned) && scroll == 0;
        }
    }
    check("last inspector controls are fully reachable at small and regular window sizes", reachable);
    check("scrolling fully offscreen controls to bottom and top does not drift", noDrift);
    check("inspector panels remain inside the small-window client", withinPanel);
    bool dpiWidthFits = true;
    for (int page : {0,1,2,3,4,5,6}) {
        show_inspector(page);
        for (UINT dpi : {96u,120u,144u,192u,240u}) {
            const int available = property_panel_width(page,dpi) - 24 - GetSystemMetricsForDpi(SM_CXVSCROLL,dpi);
            for (auto field : fields) {
                const auto r = rectIn(field,inspector);
                dpiWidthFits &= r.left >= 0 && r.right <= available;
            }
        }
    }
    check("real inspector controls fit reserved scrollbar widths at 100 to 250 percent DPI",dpiWidthFits);
    report["layoutDpiWidthModels"]={96,120,144,192,240};
    bool zoomReachable=true,zoomFonts=true,zoomSafe=true;
    for(int percent:{100,125,150,200}) {
        settingsScale=percent;
        for(const auto size:{SIZE{720,540},SIZE{1380,900}}) {
            SetWindowPos(window,nullptr,0,0,size.cx,size.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            for(int page:{0,1,2,3,4,5,6}) {
                show_inspector(page);
                scroll_inspector(INT_MAX);
                RECT area{};GetClientRect(inspector,&area);
                if(!fields.empty()) {
                    auto last=rectIn(fields.back(),inspector);
                    zoomReachable &= last.top>=0 && last.bottom<=area.bottom && last.right<=area.right;
                    if(last.top<0 || last.bottom>area.bottom || last.right>area.right)
                        report["settingsZoomFailures"].push_back({{"percent",percent},{"page",page},{"window",{size.cx,size.cy}},
                            {"last",{last.left,last.top,last.right,last.bottom}},{"viewport",{area.right,area.bottom}},{"scroll",scroll}});
                    LOGFONTW f{};GetObjectW(reinterpret_cast<HFONT>(SendMessage(fields.back(),WM_GETFONT,0,0)),sizeof(f),&f);
                    zoomFonts &= f.lfHeight==-settings_px(12);
                }
                RECT client{};GetClientRect(window,&client);auto panel=rectIn(propertiesPanel,window);
                zoomSafe &= panel.left>=0 && panel.right<=client.right && panel.top>=0 && panel.bottom<=client.bottom;
            }
        }
    }
    check("application settings zoom keeps last controls reachable at 100 to 200 percent",zoomReachable);
    check("application settings zoom creates actual scaled fonts",zoomFonts);
    check("application settings zoom panels stay inside small windows",zoomSafe);
    settingsScale=100;show_inspector(5);
    report["settingsZoomTested"]={100,125,150,200};
    SetWindowPos(window,nullptr,0,0,720,540,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    show_inspector(5); scroll_inspector(INT_MAX);
    SetWindowPos(window,nullptr,0,0,1380,1000,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    SCROLLINFO range{sizeof(range),SIF_ALL}; GetScrollInfo(inspector,SB_VERT,&range);
    auto last=rectIn(fields.back(),inspector); RECT viewport{};GetClientRect(inspector,&viewport);
    check("growing a scrolled inspector reconciles child positions and scrollbar", scroll==range.nPos && last.bottom<=viewport.bottom);
    SetWindowPos(window,nullptr,0,0,720,540,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    scroll_assets(INT_MAX); auto assetLast=rectIn(list,assetPanel);GetClientRect(assetPanel,&viewport);
    check("small-window character panel scrolls to the complete scene list",assetLast.top>=0 && assetLast.bottom<=viewport.bottom && assetLast.right<=viewport.right);
    scroll_assets(0);
    SetWindowPos(window,nullptr,saved.left,saved.top,saved.right-saved.left,saved.bottom-saved.top,SWP_NOZORDER|SWP_NOACTIVATE);
    show_inspector(5);
    check("scrolling and resizing preserve all scene data",json(scene).dump()==sceneBefore);
    report["layoutDpiObserved"]=GetDpiForWindow(window);
}
