#pragma once
// Included by Theme.h after its painting helpers. The native list box retains
// accessible item names, type search and scrolling; only its pixels are custom.
namespace veyra::ui {
struct PopupOption { int command; std::wstring label; Icon glyph=Icon::None; bool checked=false; };
inline int popupDepth=0;
inline HWND activeSelector=nullptr;
inline bool popupSelectorOpen(){return popupDepth>0;}
inline void cancelPopupSelector(){if(IsWindow(activeSelector))PostMessageW(activeSelector,WM_CLOSE,0,0);}
namespace popup_detail {
struct State {
    HWND window=nullptr,list=nullptr,anchor=nullptr;HFONT font=nullptr;
    const std::vector<PopupOption>* options=nullptr;GlassBackdrop glass;
    bool done=false,deactivated=false;int result=-1,tab=0;RECT anchorRect{};
};
inline void accept(State& s){int i=int(SendMessageW(s.list,LB_GETCURSEL,0,0));if(i>=0&&size_t(i)<s.options->size()){s.result=i;s.done=true;}}
inline LRESULT CALLBACK listProc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR context){
    auto& s=*reinterpret_cast<State*>(context);
    if(m==WM_KEYDOWN){if(w==VK_ESCAPE||w==VK_F4||w==VK_TAB){if(w==VK_TAB)s.tab=(GetKeyState(VK_SHIFT)&0x8000)?-1:1;s.done=true;return 0;}if(w==VK_RETURN||w==VK_SPACE){accept(s);return 0;}}
    if(m==WM_SYSKEYDOWN&&w==VK_UP){s.done=true;return 0;}
    if(m==WM_MOUSEMOVE){auto item=SendMessageW(h,LB_ITEMFROMPOINT,0,l);if(!HIWORD(item)&&SendMessageW(h,LB_GETCURSEL,0,0)!=LOWORD(item))SendMessageW(h,LB_SETCURSEL,LOWORD(item),0);return 0;}
    if(m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK){auto item=SendMessageW(h,LB_ITEMFROMPOINT,0,l);if(!HIWORD(item))SendMessageW(h,LB_SETCURSEL,LOWORD(item),0);return 0;}
    if(m==WM_LBUTTONUP){auto item=SendMessageW(h,LB_ITEMFROMPOINT,0,l);if(!HIWORD(item)){SendMessageW(h,LB_SETCURSEL,LOWORD(item),0);accept(s);}return 0;}
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PaintBuffer paint(h,reinterpret_cast<HDC>(w));fillSurface(paint.dc,paint.rect,h);int first=int(SendMessageW(h,LB_GETTOPINDEX,0,0));int selected=int(SendMessageW(h,LB_GETCURSEL,0,0));
        for(int i=std::max(0,first);size_t(i)<s.options->size();++i){RECT r{};if(SendMessageW(h,LB_GETITEMRECT,i,LPARAM(&r))==LB_ERR||r.top>=paint.rect.bottom)break;DRAWITEMSTRUCT draw{ODT_LISTBOX,1,UINT(i),ODA_DRAWENTIRE,UINT(i==selected?ODS_SELECTED:0),h,paint.dc,r,0};SendMessageW(s.window,WM_DRAWITEM,1,LPARAM(&draw));}return 0;}
    if(controlVisualChange(m)||m==LB_SETCURSEL||m==LB_SETTOPINDEX||m==WM_KEYDOWN||m==WM_CHAR||m==WM_VSCROLL||m==WM_MOUSEWHEEL)return updateControlModel(h,m,w,l);
    return DefSubclassProc(h,m,w,l);
}
inline LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    auto s=reinterpret_cast<State*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(m==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);s->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(s));}
    if(!s)return DefWindowProcW(h,m,w,l);
    switch(m){
    case WM_NCCALCSIZE:if(w)return 0;break;
    case WM_NCPAINT:return 0;
    case WM_ERASEBKGND:return 1;
    case WM_CREATE:{titleTheme(h);s->glass.attach(h);s->font=makeFont(h,13);RECT r{};GetClientRect(h,&r);const int pad=dip(h,8);s->glass.render(r.right,r.bottom,false,false,{{r,dip(h,12),88}});
        s->list=CreateWindowExW(0,L"LISTBOX",L"Options",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS|LBS_OWNERDRAWFIXED|LBS_NOINTEGRALHEIGHT,pad,pad,r.right-2*pad,r.bottom-2*pad,h,HMENU(1),GetModuleHandleW(nullptr),nullptr);
        SetWindowTheme(s->list,L"DarkMode_Explorer",nullptr);SendMessageW(s->list,WM_SETFONT,WPARAM(s->font),FALSE);SendMessageW(s->list,LB_SETITEMHEIGHT,0,dip(h,38));SetWindowSubclass(s->list,listProc,910,DWORD_PTR(s));
        for(const auto& option:*s->options)SendMessageW(s->list,LB_ADDSTRING,0,LPARAM(option.label.c_str()));SetTimer(h,1,100,nullptr);return 0;}
    case WM_MEASUREITEM:reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight=dip(h,38);return TRUE;
    case WM_DRAWITEM:{auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item.itemID>=s->options->size())return TRUE;const auto& option=(*s->options)[item.itemID];RECT row=item.rcItem;fillSurface(item.hDC,row,s->list);InflateRect(&row,-dip(h,1),-dip(h,2));if(item.itemState&ODS_SELECTED)translucentRound(item.hDC,row,32,dip(h,7));auto ink=option.checked?accent:textColor;
        auto old=SelectObject(item.hDC,s->font);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ink);const bool glyph=option.glyph!=Icon::None;if(glyph)drawIcon(item.hDC,option.glyph,float(row.left+dip(h,18)),(row.top+row.bottom)/2.f,float(dip(h,18)),ink);
        RECT label=row;label.left+=dip(h,glyph?36:12);label.right-=dip(h,34);glassText(item.hDC,option.label.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);if(option.checked)drawIcon(item.hDC,Icon::Check,float(row.right-dip(h,17)),(row.top+row.bottom)/2.f,float(dip(h,17)),accent);SelectObject(item.hDC,old);return TRUE;}
    case WM_CTLCOLORLISTBOX:SetTextColor(HDC(w),textColor);SetBkColor(HDC(w),panel);return LRESULT(panelBrush());
    case WM_PAINT:{PaintBuffer paint(h);fillSurface(paint.dc,paint.rect,h);return 0;}
    case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE){s->deactivated=true;s->done=true;}return 0;
    case WM_TIMER:{RECT current{};if(!IsWindow(s->anchor)||!IsWindowVisible(s->anchor)||!IsWindowEnabled(s->anchor)||!GetWindowRect(s->anchor,&current)||!EqualRect(&current,&s->anchorRect))s->done=true;return 0;}
    case WM_CLOSE:s->done=true;return 0;
    case WM_DESTROY:s->done=true;s->glass.detach();KillTimer(h,1);DeleteObject(s->font);return 0;
    }return DefWindowProcW(h,m,w,l);
}
}
// Returns an option index, or -1 on cancellation. Commit is dispatched only
// after this window is destroyed, so callers can safely rebuild controls.
inline int popupSelector(HWND anchor,const std::vector<PopupOption>& options,int selected=-1,bool above=false,const wchar_t* title=L"Select an option"){
    if(options.empty()||!IsWindowEnabled(anchor)||popupSelectorOpen())return -1;
    popup_detail::State state;state.options=&options;state.anchor=anchor;GetWindowRect(anchor,&state.anchorRect);auto owner=GetAncestor(anchor,GA_ROOT);auto priorFocus=GetFocus();
    MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromRect(&state.anchorRect,MONITOR_DEFAULTTONEAREST),&mi);auto work=mi.rcWork;int pad=dip(anchor,8),row=dip(anchor,38);int width=std::max(int(state.anchorRect.right-state.anchorRect.left),dip(anchor,236));
    HDC dc=GetDC(anchor);auto font=makeFont(anchor,13);auto old=SelectObject(dc,font);for(const auto& option:options){SIZE extent{};GetTextExtentPoint32W(dc,option.label.c_str(),int(option.label.size()),&extent);width=std::max(width,int(extent.cx)+dip(anchor,88));}SelectObject(dc,old);DeleteObject(font);ReleaseDC(anchor,dc);
    width=std::min({width,dip(anchor,520),int(work.right-work.left)-2*pad});int height=std::min(int(options.size()),8)*row+2*pad;height=std::min(height,int(work.bottom-work.top)-2*pad);
    int x=std::clamp(int(state.anchorRect.left),int(work.left)+pad,int(work.right)-width-pad);int y=above?int(state.anchorRect.top)-height-pad:int(state.anchorRect.bottom)+pad;
    if(y+height>work.bottom)y=int(state.anchorRect.top)-height-pad;if(y<work.top)y=int(state.anchorRect.bottom)+pad;y=std::clamp(y,int(work.top)+pad,int(work.bottom)-height-pad);
    WNDCLASSW wc{};wc.lpfnWndProc=popup_detail::proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VeyraGlassSelector";wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));RegisterClassW(&wc);
    ++popupDepth;HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,title,WS_POPUP|WS_CLIPCHILDREN,x,y,width,height,owner,nullptr,wc.hInstance,&state);
    if(!window){--popupDepth;return -1;}activeSelector=window;SendMessageW(state.list,LB_SETCURSEL,selected>=0?selected:0,0);ShowWindow(window,SW_SHOW);SetFocus(state.list);veyra::log::info("ui-selector",std::format("open items={} selected={} width={} height={} acrylic=true",options.size(),selected,width,height));
    MSG msg{};while(!state.done&&IsWindow(window)){int result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(int(msg.wParam));break;}TranslateMessage(&msg);DispatchMessageW(&msg);}
    const bool restore=!state.deactivated;if(IsWindow(window))DestroyWindow(window);activeSelector=nullptr;--popupDepth;
    // Restore this thread's focus for keyboard/selection closure, even when a
    // test or application opened it without foreground privilege. An outside
    // activation keeps its own focus and is never pulled back to the anchor.
    if(restore&&IsWindow(owner)&&IsWindowVisible(owner)&&IsWindowEnabled(owner)){SetActiveWindow(owner);auto focus=state.tab?GetNextDlgTabItem(owner,anchor,state.tab<0):priorFocus;if(IsWindow(focus)&&IsWindowVisible(focus)&&IsWindowEnabled(focus))SetFocus(focus);}
    if(state.tab)veyra::log::info("ui-selector",std::format("tab={} restore={} nextId={} focusId={} ownerVisible={} ownerEnabled={} nextVisible={} nextEnabled={}",state.tab,restore,GetDlgCtrlID(GetNextDlgTabItem(owner,anchor,state.tab<0)),GetDlgCtrlID(GetFocus()),IsWindowVisible(owner),IsWindowEnabled(owner),IsWindowVisible(GetNextDlgTabItem(owner,anchor,state.tab<0)),IsWindowEnabled(GetNextDlgTabItem(owner,anchor,state.tab<0))));
    veyra::log::info("ui-selector",std::format("closed selection={} choiceAccepted={}",state.result,state.result>=0));return state.result;
}
inline void comboSelector(HWND h){
    if(GetPropW(h,L"Veyra.ComboOpen")||popupSelectorOpen())return;SetFocus(h);RemovePropW(h,L"Veyra.ComboCancel");SetPropW(h,L"Veyra.ComboOpen",HANDLE(1));auto notify=[&](int code){if(IsWindow(h))SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(h),code),LPARAM(h));};notify(CBN_DROPDOWN);
    if(!IsWindow(h))return;
    // A synchronous dropdown listener can cancel before a popup HWND exists.
    if(GetPropW(h,L"Veyra.ComboCancel")){RemovePropW(h,L"Veyra.ComboCancel");RemovePropW(h,L"Veyra.ComboOpen");notify(CBN_SELENDCANCEL);notify(CBN_CLOSEUP);return;}
    std::vector<PopupOption> options;int selected=int(SendMessageW(h,CB_GETCURSEL,0,0));int count=int(SendMessageW(h,CB_GETCOUNT,0,0));
    for(int i=0;i<count;++i){int n=int(SendMessageW(h,CB_GETLBTEXTLEN,i,0));if(n<0)break;std::wstring value(size_t(n)+1,0);SendMessageW(h,CB_GETLBTEXT,i,LPARAM(value.data()));value.resize(n);options.push_back({i,std::move(value),Icon::None,i==selected});}
    const int chosen=popupSelector(h,options,selected);if(!IsWindow(h))return;RemovePropW(h,L"Veyra.ComboOpen");RemovePropW(h,L"Veyra.ComboCancel");
    // Async device/preset refreshes may replace native items during the popup.
    bool valid=chosen>=0&&size_t(chosen)<options.size()&&SendMessageW(h,CB_GETCOUNT,0,0)==count;
    if(valid){int n=int(SendMessageW(h,CB_GETLBTEXTLEN,chosen,0));std::wstring current(size_t(std::max(0,n))+1,0);if(n<0)valid=false;else{SendMessageW(h,CB_GETLBTEXT,chosen,LPARAM(current.data()));current.resize(n);valid=current==options[chosen].label;}}
    if(valid){SendMessageW(h,CB_SETCURSEL,chosen,0);notify(CBN_SELENDOK);if(chosen!=selected)notify(CBN_SELCHANGE);}else notify(CBN_SELENDCANCEL);notify(CBN_CLOSEUP);if(IsWindow(h))InvalidateRect(h,nullptr,FALSE);
    veyra::log::info("ui-selector",std::format("combo committed={} changed={}",valid,valid&&chosen!=selected));
}
}
