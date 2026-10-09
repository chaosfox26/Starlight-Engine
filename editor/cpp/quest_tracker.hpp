#pragma once
#include "quest_model.hpp"
#include "platform.hpp"
#include <windowsx.h>
#include <map>
#include <cmath>
namespace starlight::editor {
// Shared editor/standalone HUD. Only local presentation is mutable here.
class QuestTracker {
    HWND window_{};HFONT font_{},titleFont_{},completedFont_{};
    int scale_=100,scroll_=0,total_=0,opacity_=70,layoutTop_=104;bool journal_=false,collapsed_=false;
    bool settingsOpen_=false,locked_=true,dragging_=false,customPosition_=false;
    double anchorX_=1,anchorY_=0;
    POINT dragStart_{},dragOrigin_{};
    fs::path preference_path()const{return preferenceOverride.empty()?application_data()/L"quest-tracker.json":preferenceOverride;}
    void save_preferences(){
        if(preferenceOverride.empty() && wcsstr(GetCommandLineW(),L"--ui-"))return;
        try{atomic_write(preference_path(),preferences().dump());}
        catch(const std::exception& e){diagnostic("quest tracker preference",e.what());}
    }
    void drag_to(POINT screen){
        RECT host{},r{};GetClientRect(GetParent(window_),&host);GetWindowRect(window_,&r);
        int minX=px(12),minY=layoutTop_,rangeX=std::max(0,int(host.right)-int(r.right-r.left)-2*minX),rangeY=std::max(0,int(host.bottom)-layoutTop_-px(54)-int(r.bottom-r.top));
        int x=std::clamp(int(dragOrigin_.x+screen.x-dragStart_.x),minX,minX+rangeX),y=std::clamp(int(dragOrigin_.y+screen.y-dragStart_.y),minY,minY+rangeY);
        anchorX_=rangeX?double(x-minX)/rangeX:0;anchorY_=rangeY?double(y-minY)/rangeY:0;customPosition_=true;
        SetWindowPos(window_,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    std::string selected_;std::map<std::string,bool> pins_;
    struct Hit {RECT rect;std::string id;bool pin=false;};std::vector<Hit> hits_;
    std::vector<QuestView> views_;
    int px(int n)const{return std::max(1,MulDiv(n,scale_,100));}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto p=reinterpret_cast<QuestTracker*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<QuestTracker*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);p->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);}
        if(!p)return DefWindowProcW(h,m,w,l);
        if(m==WM_PAINT){p->paint();return 0;}
        if(m==WM_PRINTCLIENT){p->paint(reinterpret_cast<HDC>(w));return 0;}
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_MOUSEWHEEL){RECT r{};GetClientRect(h,&r);p->scroll_=std::clamp(p->scroll_-GET_WHEEL_DELTA_WPARAM(w)*p->px(48)/WHEEL_DELTA,0,std::max(0,p->total_-int(r.bottom)));InvalidateRect(h,nullptr,FALSE);return 0;}
        if(m==WM_LBUTTONDOWN && !p->locked_ && !p->journal_){
            POINT local{GET_X_LPARAM(l),GET_Y_LPARAM(l)};bool action=false;for(const auto& hit:p->hits_)action=action||PtInRect(&hit.rect,local);
            if(!action){p->dragging_=true;p->dragStart_=local;ClientToScreen(h,&p->dragStart_);RECT r{};GetWindowRect(h,&r);p->dragOrigin_={r.left,r.top};ScreenToClient(GetParent(h),&p->dragOrigin_);SetCapture(h);return 0;}
        }
        if(m==WM_MOUSEMOVE && p->dragging_){POINT q{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(h,&q);p->drag_to(q);return 0;}
        if(m==WM_LBUTTONUP){
            if(p->dragging_){p->dragging_=false;if(GetCapture()==h)ReleaseCapture();p->save_preferences();return 0;}
            SetFocus(h);p->activate(GET_X_LPARAM(l),GET_Y_LPARAM(l));return 0;
        }
        if(m==WM_CAPTURECHANGED || m==WM_CANCELMODE){if(p->dragging_){p->dragging_=false;p->save_preferences();}return 0;}
        if(m==WM_KEYDOWN && (w==VK_UP || w==VK_DOWN)){RECT r{};GetClientRect(h,&r);p->scroll_=std::clamp(p->scroll_+(w==VK_UP?-1:1)*p->px(32),0,std::max(0,p->total_-int(r.bottom)));InvalidateRect(h,nullptr,FALSE);return 0;}
        if(m==WM_KEYDOWN && (w==VK_RETURN || w=='J')){p->journal_=w==VK_RETURN?true:!p->journal_;p->selected_=p->journal_ && !p->views_.empty()?p->views_.front().id:std::string{};p->collapsed_=false;p->scroll_=0;p->relayout();InvalidateRect(h,nullptr,FALSE);return 0;}
        if(m==WM_KEYDOWN && w==VK_ESCAPE){p->selected_.clear();p->journal_=false;p->scroll_=0;p->relayout();InvalidateRect(h,nullptr,FALSE);return 0;}
        if(m==WM_DESTROY){p->window_=nullptr;return 0;}
        return DefWindowProcW(h,m,w,l);
    }
    void paint(HDC supplied=nullptr){
        if(!supplied){
            PAINTSTRUCT ps{};BeginPaint(window_,&ps);RECT r{};GetClientRect(window_,&r);
            const int width=r.right,height=r.bottom;
            if(width>0 && height>0){
                HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);BITMAPINFO info{};
                info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
                info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
                void* data=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&data,nullptr,0);
                if(bitmap && memory){auto old=SelectObject(memory,bitmap);paint(memory);GdiFlush();
                    auto pixels=static_cast<unsigned int*>(data);
                    // Only the backdrop fades. Glyphs/icons remain fully opaque.
                    // Alpha1 at0% keeps the invisible control surface clickable.
                    const unsigned alpha=std::max(1,opacity_*255/100);
                    for(size_t i=0;i<size_t(width)*height;++i){auto color=pixels[i]&0xffffff;
                        if(color==0x131a2b)pixels[i]=(alpha<<24)|((19*alpha/255)<<16)|((26*alpha/255)<<8)|(43*alpha/255);
                        else pixels[i]=0xff000000|color;
                    }
                    POINT source{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
                    UpdateLayeredWindow(window_,screen,nullptr,&size,memory,&source,0,&blend,ULW_ALPHA);SelectObject(memory,old);
                }
                if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);ReleaseDC(nullptr,screen);
            }
            EndPaint(window_,&ps);return;
        }
        PAINTSTRUCT ps{};auto dc=supplied?supplied:BeginPaint(window_,&ps);RECT area{};GetClientRect(window_,&area);
        auto brush=CreateSolidBrush(RGB(19,26,43));FillRect(dc,&area,brush);DeleteObject(brush);
        SetBkMode(dc,TRANSPARENT);hits_.clear();int y=px(12)-scroll_;
        auto line=[&](const std::string& value,int left,COLORREF color,bool title=false,bool completed=false){
            auto old=SelectObject(dc,completed?completedFont_:title?titleFont_:font_);auto label=wide(value);
            RECT r{left,y,area.right-px(14),y};DrawTextW(dc,label.c_str(),-1,&r,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
            const int height=std::max(px(18),int(r.bottom-r.top));r.bottom=y+height;
            SetTextColor(dc,color);DrawTextW(dc,label.c_str(),-1,&r,DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);y+=height+px(6);return r;
        };
        const COLORREF ink=RGB(230,235,249),accent=RGB(224,191,111),muted=RGB(165,181,206);
        if(journal_){auto heading=line("QUEST JOURNAL  ·  Close",px(14),accent,true);hits_.push_back({heading,"",false});}
        else {
            // Small original cog, not a permanent journal/settings text header.
            RECT gear{area.right-px(38),px(8),area.right-px(10),px(36)};
            int cx=(gear.left+gear.right)/2,cy=(gear.top+gear.bottom)/2;
            POINT teeth[33];for(int i=0;i<=32;++i){double a=i*6.283185307179586/32;int radius=px(i%4<2?10:7);teeth[i]={cx+int(std::cos(a)*radius),cy+int(std::sin(a)*radius)};}
            auto pen=CreatePen(PS_SOLID,std::max(1,px(1)),accent);auto old=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
            Polygon(dc,teeth,32);Ellipse(dc,cx-px(3),cy-px(3),cx+px(3),cy+px(3));SelectObject(dc,oldBrush);SelectObject(dc,old);DeleteObject(pen);
            hits_.push_back({gear,"__settings",false});
            if(!locked_)line("Drag here to move",px(14),muted);else y+=px(28);
        }
        if(settingsOpen_ && !journal_){
            line("Tracker settings",px(14),accent,true);
            int opacityY=y;line("Background: "+std::to_string(opacity_)+"%",px(14),muted);
            auto button=[&](const wchar_t* label,int x,const char* id){RECT r{x,opacityY,x+px(26),opacityY+px(20)};auto old=SelectObject(dc,font_);SetTextColor(dc,accent);DrawTextW(dc,label,-1,&r,DT_CENTER|DT_SINGLELINE|DT_VCENTER);SelectObject(dc,old);hits_.push_back({r,id,false});};
            button(L"−",area.right-px(76),"__opacity_less");button(L"+",area.right-px(42),"__opacity_more");
            auto lock=line(locked_?"Unlock to move":"Lock position",px(14),accent);hits_.push_back({lock,"__lock",false});
            auto reset=line("Reset position",px(14),muted);hits_.push_back({reset,"__reset_position",false});
            auto fold=line(collapsed_?"Expand objectives":"Collapse objectives",px(14),muted);hits_.push_back({fold,"__fold",false});
            auto journal=line("Open quest journal",px(14),muted);hits_.push_back({journal,"__journal",false});
            y+=px(8);
        }
        if(collapsed_ && !journal_){total_=y+scroll_;if(!supplied)EndPaint(window_,&ps);return;}
        int detailLeft=px(14),journalListEnd=0;
        if(journal_){
            const int bodyTop=y;
            const bool columns=area.right>=px(460);
            int listBottom=bodyTop;
            for(const auto& q:views_){
                RECT r{px(14),listBottom,columns?px(195):area.right-px(14),listBottom};auto name=wide(q.title);
                auto old=SelectObject(dc,font_);DrawTextW(dc,name.c_str(),-1,&r,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
                r.bottom=std::max(r.bottom,r.top+px(20));SetTextColor(dc,q.id==selected_?accent:ink);DrawTextW(dc,name.c_str(),-1,&r,DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);
                hits_.push_back({r,q.id,false});listBottom=r.bottom+px(8);
            }
            journalListEnd=listBottom+scroll_;
            if(columns){detailLeft=px(214);y=bodyTop;}
            else y=listBottom+px(12);
        }
        bool any=false;
        for(const auto& q:views_){
            bool pinned=pins_.contains(q.id)?pins_.at(q.id):q.tracked;
            if(!selected_.empty() && selected_!=q.id)continue;
            if(selected_.empty() && !journal_ && !pinned)continue;
            any=true;int top=y;
            // Original diamond quest icon, paired with text and completion state.
            auto pen=CreatePen(PS_SOLID,1,q.complete?RGB(115,223,174):accent);auto oldPen=SelectObject(dc,pen);
            POINT shape[5]={{detailLeft,top+px(2)},{detailLeft+px(6),top+px(8)},{detailLeft,top+px(14)},{detailLeft-px(6),top+px(8)},{detailLeft,top+px(2)}};
            Polyline(dc,shape,5);SelectObject(dc,oldPen);DeleteObject(pen);
            auto title=line(q.title+(q.complete?"  — Complete":""),detailLeft+px(14),accent,true);hits_.push_back({title,q.id,false});
            if(!selected_.empty()){
                line(q.description.empty()?"No additional description provided.":q.description,detailLeft,ink);
                auto pin=line(pinned?"Unpin from tracker":"Pin to tracker",detailLeft,accent);hits_.push_back({pin,q.id,true});
            }
            if(q.stepCount)line("Step "+std::to_string(q.currentStep+1)+" of "+std::to_string(q.stepCount)+" · Complete objectives in any order",detailLeft,muted);
            int shownStep=-1;bool currentShown=false;
            for(const auto& o:q.objectives){
                if(!journal_ && q.stepCount && o.step!=q.currentStep)continue;
                if(journal_ && q.stepCount && shownStep!=o.step){shownStep=o.step;line("Step "+std::to_string(o.step+1),detailLeft,accent);}
                std::string label=o.complete?"✓ ":o.locked?"Locked · ":"• ";
                label+=o.text;
                if(o.required>1)label+="  "+std::to_string(o.current)+"/"+std::to_string(o.required);
                else if(o.complete)label+="  (done)";
                if(!q.stepCount && journal_ && !currentShown && !o.complete && !o.locked){label="Current step · "+label;currentShown=true;}
                auto objective=line(label,detailLeft+px(14),o.complete?RGB(115,223,174):o.locked?muted:ink,false,o.complete);
                hits_.push_back({objective,q.id,false});
                if(journal_ && !o.description.empty())line(o.description,detailLeft+px(14),muted);
            }
            y+=px(10);
        }
        if(!any)line("No tracked quests. Use the cogwheel to open the journal.",px(14),muted);
        total_=std::max(y+scroll_,journalListEnd)+px(8);
        if(total_>area.bottom) {auto bar=CreateSolidBrush(RGB(113,126,157));RECT r{area.right-px(4),px(4),area.right-px(2),area.bottom-px(4)};FillRect(dc,&r,bar);DeleteObject(bar);}
        if(!supplied)EndPaint(window_,&ps);
    }
public:
    fs::path preferenceOverride; // Isolated acceptance tests; normal applications use per-user storage.
    json preferences()const{return json{{"version",2},{"backgroundOpacity",opacity_},{"locked",locked_},{"customPosition",customPosition_},{"anchorX",anchorX_},{"anchorY",anchorY_}};}
    ~QuestTracker(){if(window_)DestroyWindow(window_);if(font_)DeleteObject(font_);if(titleFont_)DeleteObject(titleFont_);if(completedFont_)DeleteObject(completedFont_);}
    HWND handle()const{return window_;}
    const std::vector<QuestView>& views()const{return views_;}
    int opacity()const{return opacity_;}
    bool locked()const{return locked_;}
    bool settings_open()const{return settingsOpen_;}
    bool collapsed()const{return collapsed_;}
    bool tracked(const std::string& id)const{auto q=std::find_if(views_.begin(),views_.end(),[&](const auto& v){return v.id==id;});return pins_.contains(id)?pins_.at(id):q!=views_.end() && q->tracked;}
    bool details_open()const{return !selected_.empty();}
    std::optional<RECT> action_bounds(const std::string& id,bool pin=false)const{for(const auto& hit:hits_)if(hit.id==id && hit.pin==pin)return hit.rect;return std::nullopt;}
    void relayout(){RECT r{};GetClientRect(GetParent(window_),&r);layout(r.right,r.bottom,layoutTop_,scale_);}
    void activate(int x,int y){for(const auto& h:hits_)if(PtInRect(&h.rect,POINT{x,y})){
        if(h.id=="__opacity_less" || h.id=="__opacity_more"){
            opacity_=std::clamp(opacity_+(h.id=="__opacity_less"?-10:10),0,100);
            save_preferences();
        }
        else if(h.id=="__settings")settingsOpen_=!settingsOpen_;
        else if(h.id=="__lock"){locked_=!locked_;save_preferences();}
        else if(h.id=="__reset_position"){customPosition_=false;anchorX_=1;anchorY_=0;save_preferences();}
        else if(h.id=="__journal"){journal_=true;selected_=views_.empty()?std::string{}:views_.front().id;settingsOpen_=false;}
        else if(h.id=="__fold")collapsed_=!collapsed_;
        else if(h.id.empty()){journal_=!journal_;selected_=journal_ && !views_.empty()?views_.front().id:std::string{};}
        else if(h.pin){auto q=std::find_if(views_.begin(),views_.end(),[&](const auto& v){return v.id==h.id;});if(q!=views_.end())pins_[h.id]=!(pins_.contains(h.id)?pins_[h.id]:q->tracked);}
        else {selected_=h.id;journal_=true;collapsed_=false;settingsOpen_=false;}
        scroll_=0;relayout();InvalidateRect(window_,nullptr,FALSE);return;
    }}
    void create(HWND owner){WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"StarlightQuestTracker";c.lpfnWndProc=proc;c.hCursor=LoadCursor(nullptr,IDC_HAND);RegisterClassW(&c);
        if(!preferenceOverride.empty() || !wcsstr(GetCommandLineW(),L"--ui-"))try{
            auto saved=json::parse(read_file(preference_path()));int version=saved.value("version",0);
            if(version==1 || version==2){opacity_=std::clamp(saved.value("backgroundOpacity",70),0,100);locked_=saved.value("locked",true);customPosition_=saved.value("customPosition",false);anchorX_=saved.value("anchorX",1.);anchorY_=saved.value("anchorY",0.);if(!std::isfinite(anchorX_) || !std::isfinite(anchorY_)){customPosition_=false;anchorX_=1;anchorY_=0;}anchorX_=std::clamp(anchorX_,0.,1.);anchorY_=std::clamp(anchorY_,0.,1.);}
        }catch(...){}

        window_=CreateWindowExW(WS_EX_LAYERED,c.lpszClassName,L"Quest tracker",WS_CHILD|WS_CLIPSIBLINGS|WS_TABSTOP,0,0,1,1,owner,nullptr,c.hInstance,this);}
    void layout(int width,int height,int top,int scale){
        if(!window_)return;layoutTop_=top;
        if(!font_ || scale_!=scale){scale_=scale;if(font_)DeleteObject(font_);if(titleFont_)DeleteObject(titleFont_);if(completedFont_)DeleteObject(completedFont_);
            font_=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
            completedFont_=CreateFontW(-px(13),0,0,0,FW_NORMAL,FALSE,FALSE,TRUE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
            titleFont_=CreateFontW(-px(14),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");}
        int w=std::min(px(journal_?700:330),std::max(1,width-2*px(12)));int h=std::max(1,std::min(px(journal_?600:440),height-top-px(54)));
        int x=std::max(0,width-w-px(12)),y=top;
        if(customPosition_ && !journal_){x=px(12)+int(std::max(0,width-w-2*px(12))*anchorX_);y=top+int(std::max(0,height-top-px(54)-h)*anchorY_);}
        SetWindowPos(window_,HWND_TOP,x,y,w,h,SWP_NOACTIVATE);InvalidateRect(window_,nullptr,FALSE);
    }
    void reset(){pins_.clear();selected_.clear();journal_=false;collapsed_=false;settingsOpen_=false;scroll_=0;}
    void update(const SceneData& scene,const GameplayRulesRuntime& runtime,bool visible){
        if(!window_)return;auto next=quest_views(scene,runtime);
        // Small bounded model; painting only invalidates when displayed data changes.
        auto signature=[](const auto& list){std::string s;for(const auto& q:list){s+=std::to_string(q.currentStep)+std::to_string(q.stepCount)+q.id+q.title+q.description+std::to_string(q.complete)+std::to_string(q.tracked);for(const auto& o:q.objectives)s+=o.text+o.description+std::to_string(o.current)+std::to_string(o.required)+std::to_string(o.complete)+std::to_string(o.locked);}return s;};
        bool changed=signature(next)!=signature(views_);views_=std::move(next);
        ShowWindow(window_,visible && !views_.empty()?SW_SHOWNOACTIVATE:SW_HIDE);if(changed)InvalidateRect(window_,nullptr,FALSE);
    }
    void hide(){if(window_)ShowWindow(window_,SW_HIDE);}
};
}

