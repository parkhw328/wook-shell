#include "sftp_view.hpp"
#include "sftp.hpp"
#include "ui.hpp"
#include "prompt.h"
#include <filesystem>
#include <thread>
#include <memory>
#include <unordered_set>

namespace {
namespace fs = std::filesystem;
constexpr UINT finished = WM_APP + 120;
enum { Upload = 1, Download, NewFolder, Rename, Delete, Refresh, Cancel, LocalUp, LocalGo, LocalPath, LocalList, RemoteUp, RemoteGo, RemotePath, RemoteList, ShowHidden };
LRESULT CALLBACK headerProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(msg==WM_NCDESTROY){RemoveWindowSubclass(hwnd,headerProc,id);return DefSubclassProc(hwnd,msg,wp,lp);}
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){
        PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);RECT client{};GetClientRect(hwnd,&client);ui::fill(dc,client,ui::raised);
        for(int i=0;i<Header_GetItemCount(hwnd);++i){wchar_t text[128]{};HDITEMW item{};item.mask=HDI_TEXT;item.pszText=text;item.cchTextMax=128;Header_GetItem(hwnd,i,&item);
            RECT rect{};Header_GetItemRect(hwnd,i,&rect);rect.left+=ui::px(7);ui::label(dc,text,rect,ui::TextSize::caption,ui::muted,true);}
        EndPaint(hwnd,&ps);return 0;
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}
struct Result { std::wstring path, message; std::vector<sftp::Entry> entries; bool ok{}; };
struct Browser {
    HWND hwnd{}, owner{}, controls[17]{}; HANDLE job{};
    HWND tooltips{};
    int paneWidth{}, gutter = 88;
    bool transferLabels = true;
    std::wstring session, local, remote, status = L"Connecting to SFTP…", current;
    bool saved{}, busy{}, connected{}, showHidden = false, remoteFocused = true;
    std::unique_ptr<sftp::Client> client;
    std::thread worker;
    std::vector<sftp::Entry> localEntries, remoteEntries;
    std::atomic<uint64_t> done{}, total{};
    std::mutex progressLock;
    Browser(HWND parent, HANDLE j, std::wstring name, bool s) : owner(parent),job(j),session(std::move(name)),saved(s) {
        wchar_t home[32768]{}; GetEnvironmentVariableW(L"USERPROFILE",home,32768); local = *home ? home : L"C:\\";
    }
    ~Browser() { stop(); }
    void stop() {
        if (client) client->cancel();
        if (worker.joinable()) worker.join();
        MSG msg{}; while (PeekMessageW(&msg,hwnd,finished,finished,PM_REMOVE)) delete (Result *)msg.lParam;
        client.reset();
    }
    void layout();
    void fill(bool remoteSide);
    void readLocal();
    void state();
    void transferState();
    void start(const std::function<void(Result &)> &operation);
    void connect();
    void navigate(bool remoteSide, std::wstring path);
    void action(int id);
    std::vector<sftp::Entry> selected(bool remoteSide);
    void transfer(bool upload);
};
void Browser::layout() {
    RECT r; GetClientRect(hwnd,&r); int w = MulDiv(r.right,96,ui::dpi), h = MulDiv(r.bottom,96,ui::dpi);
    int x=16,y=14;
    for (auto [id,width] : {std::pair{NewFolder,120},{Rename,92},{Delete,86},{Refresh,96},{Cancel,90},{ShowHidden,208}}) {
        if (x+width > w-16) {x=16;y+=44;}
        ui::place(controls[id],x,y,width,34); x+=width+8;
    }
    gutter = w < 560 ? 68 : 88;
    int top = y+50, span = paneWidth = std::max(80, (w-32-gutter)/2);
    int listHeight = std::max(30,h-top-142), buttonWidth = gutter-20;
    transferLabels = listHeight >= 136;
    int buttonHeight = transferLabels ? 40 : std::min(32,(listHeight-6)/2);
    int block = transferLabels ? 136 : buttonHeight*2+6;
    int centerY = top+72+std::max(0,(listHeight-block)/2);
    ui::place(controls[Upload],16+span+10,centerY,buttonWidth,buttonHeight);
    ui::place(controls[Download],16+span+10,centerY+(transferLabels?76:buttonHeight+6),buttonWidth,buttonHeight);
    for (int side=0;side<2;++side) {
        int left=16+side*(span+gutter), base=side ? RemoteUp : LocalUp;
        ui::place(controls[base],left,top+30,40,30);
        ui::place(controls[base+2],left+46,top+34,std::max(1,span-92),24);
        ui::place(controls[base+1],left+span-42,top+30,42,30);
        ui::place(controls[base+3],left,top+72,span,listHeight);
        ListView_SetColumnWidth(controls[base+3],0,ui::px(std::max(100,span-202)));
        ListView_SetColumnWidth(controls[base+3],1,ui::px(85)); ListView_SetColumnWidth(controls[base+3],2,ui::px(108));
    }
    InvalidateRect(hwnd,nullptr,TRUE);
}
void Browser::fill(bool remoteSide) {
    HWND list=controls[remoteSide?RemoteList:LocalList]; auto &entries=remoteSide?remoteEntries:localEntries;
    SendMessageW(list,WM_SETREDRAW,FALSE,0); ListView_DeleteAllItems(list);
    int i=0;
    for (auto &entry:entries) {
        bool hidden = !entry.name.empty() && entry.name[0] == L'.';
        if (!remoteSide) { auto attributes = GetFileAttributesW((fs::path(local)/entry.name).c_str()); hidden |= attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN); }
        if (!showHidden && hidden) continue;
        auto name = (entry.directory()?L"▸  ":entry.regular()?L"   ":L"↗  ")+entry.name;
        LVITEMW item{}; item.mask=LVIF_TEXT|LVIF_PARAM; item.iItem=i; item.pszText=name.data(); item.lParam=&entry-entries.data(); ListView_InsertItem(list,&item);
        auto size=entry.directory()?L"Folder":entry.regular()?std::to_wstring(entry.size):L"Link / other";
        ListView_SetItemText(list,i,1,(wchar_t *)size.c_str());
        std::wstring date;
        if (entry.modified) {
            ULARGE_INTEGER ticks{}; ticks.QuadPart=((uint64_t)entry.modified+11644473600ULL)*10000000ULL;
            FILETIME file{ticks.LowPart,ticks.HighPart}; SYSTEMTIME time{}; FileTimeToSystemTime(&file,&time);
            wchar_t text[32]; swprintf(text,32,L"%04d-%02d-%02d",time.wYear,time.wMonth,time.wDay); date=text;
        }
        ListView_SetItemText(list,i,2,date.data()); ++i;
    }
    SendMessageW(list,WM_SETREDRAW,TRUE,0); InvalidateRect(list,nullptr,TRUE);
    SetWindowTextW(controls[remoteSide?RemotePath:LocalPath],(remoteSide?remote:local).c_str());
}
void Browser::readLocal() {
    localEntries.clear();
    for (auto &item:fs::directory_iterator(local)) {
        std::error_code error; auto attr=GetFileAttributesW(item.path().c_str());
        bool link=attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_REPARSE_POINT);
        bool directory=item.is_directory(error); uint64_t size=directory||link?0:item.file_size(error); if(error)size=0;
        sftp::Entry entry{item.path().filename().wstring(),size,(uint32_t)(link?0120000:directory?0040000:0100000),0};
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if(GetFileAttributesExW(item.path().c_str(),GetFileExInfoStandard,&data)) {
            ULARGE_INTEGER ticks{}; ticks.LowPart=data.ftLastWriteTime.dwLowDateTime; ticks.HighPart=data.ftLastWriteTime.dwHighDateTime;
            if(ticks.QuadPart>=116444736000000000ULL)entry.modified=(uint32_t)(ticks.QuadPart/10000000ULL-11644473600ULL);
        }
        localEntries.push_back(std::move(entry)); if(localEntries.size()>=100000)break;
    }
    std::sort(localEntries.begin(),localEntries.end(),[](const auto&a,const auto&b){return a.directory()!=b.directory()?a.directory():_wcsicmp(a.name.c_str(),b.name.c_str())<0;});
    fill(false);
}
void Browser::state() {
    for(int id=1;id<=RemoteList;++id) EnableWindow(controls[id],id==Cancel?busy:!busy && ((id>=LocalUp&&id<=LocalList) || connected));
    EnableWindow(controls[ShowHidden],!busy);
    transferState();
    InvalidateRect(hwnd,nullptr,TRUE);
}
void Browser::transferState() {
    EnableWindow(controls[Upload], !busy && connected && ListView_GetSelectedCount(controls[LocalList]) > 0);
    EnableWindow(controls[Download], !busy && connected && ListView_GetSelectedCount(controls[RemoteList]) > 0);
}
void Browser::start(const std::function<void(Result&)> &operation) {
    if(busy)return;
    if(worker.joinable())worker.join();
    busy=true; done=0; total=0; {std::lock_guard lock(progressLock);current.clear();} state();
    worker=std::thread([this,operation] {
        auto result=std::make_unique<Result>();
        try { operation(*result); result->ok=true; } catch(const std::exception&e){ result->message=wook::wide(e.what()); if(client)client->cancel(); }
        if(PostMessageW(hwnd,finished,0,(LPARAM)result.get()))result.release();
    });
}
void Browser::connect() {
    stop(); busy=false; connected=false; status=L"Connecting to SFTP…";
    client=std::make_unique<sftp::Client>(owner,job,session,saved);
    start([this](Result &r){r.path=client->initialize();r.entries=client->list(r.path);r.message=L"Connected · Select local files and > to upload; select remote files and < to download.";});
}
void Browser::navigate(bool remoteSide,std::wstring path) {
    if(busy)return;
    if(remoteSide) {
        if(!connected)return;
        start([this,path](Result&r){r.path=client->canonical(path);r.entries=client->list(r.path);r.message=L"Ready · Files transfer individually; browse folders to transfer their contents.";});
    } else {
        auto next=fs::weakly_canonical(path).wstring();
        if(!fs::is_directory(next))throw std::runtime_error("Choose an existing local folder.");
        auto old=local; local=next;
        try{readLocal();}catch(...){local=old;throw;}
    }
}
std::vector<sftp::Entry> Browser::selected(bool remoteSide) {
    std::vector<sftp::Entry> result; auto list=controls[remoteSide?RemoteList:LocalList]; auto &entries=remoteSide?remoteEntries:localEntries;
    for(int i=-1;(i=ListView_GetNextItem(list,i,LVNI_SELECTED))>=0;) {
        LVITEMW item{};item.mask=LVIF_PARAM;item.iItem=i;
        if(ListView_GetItem(list,&item)&&item.lParam>=0&&(size_t)item.lParam<entries.size())result.push_back(entries[item.lParam]);
    }
    return result;
}
void Browser::transfer(bool upload) {
    if(busy || !connected)return;
    auto entries=selected(!upload); if(entries.empty()) {status=L"Select one or more files in the source pane first.";state();return;}
    std::unordered_set<std::wstring> approved;
    std::wstring conflicts;
    for(auto &e:entries) {
        if(!e.regular() || !wsftp_local_name(wook::utf8(e.name).c_str()))throw std::runtime_error("Select regular files with portable filenames. Browse folders to transfer their contents; links are not followed.");
        bool exists=upload ? std::any_of(remoteEntries.begin(),remoteEntries.end(),[&](auto&item){return item.name==e.name;}) : fs::exists(fs::path(local)/e.name);
        if(exists){approved.insert(e.name);if(approved.size()<=8)conflicts+=L"\n  "+e.name;}
    }
    if(!approved.empty()) {
        auto text=L"Replace "+std::to_wstring(approved.size())+L" existing file(s) in "+(upload?L"remote "+remote:L"local "+local)+L"?\n"+conflicts+L"\n\nCompleted transfers replace these files. Cancelled or failed transfers keep the previous file.";
        if(MessageBoxW(owner,text.c_str(),L"Confirm file replacement",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
    }
    auto fromLocal=local,fromRemote=remote;
    start([this,entries,approved,upload,fromLocal,fromRemote](Result&r){
        size_t index=0;
        for(auto&e:entries) {
            {std::lock_guard lock(progressLock);current=(upload?L"Uploading ":L"Downloading ")+e.name+L" ("+std::to_wstring(++index)+L"/"+std::to_wstring(entries.size())+L")";}
            auto progress=[this](uint64_t d,uint64_t t){done=d;total=t;};
            auto lp=(fs::path(fromLocal)/e.name).wstring(), rp=sftp::join(fromRemote,e.name);
            if(upload)client->upload(lp,rp,approved.contains(e.name),progress);else client->download(rp,lp,approved.contains(e.name),progress);
        }
        r.path=fromRemote;r.entries=client->list(r.path);r.message=std::to_wstring(entries.size())+L" file(s) "+(upload?L"uploaded":L"downloaded")+L" successfully.";
    });
}
void Browser::action(int id) {
    if(id==ShowHidden && !busy){showHidden=SendMessageW(controls[ShowHidden],BM_GETCHECK,0,0)==BST_CHECKED;fill(false);fill(true);state();return;}
    if(id==Cancel){if(client)client->cancel();status=L"Cancelling transfer…";return;}
    if(busy)return;
    if(id==Upload||id==Download){transfer(id==Upload);return;}
    if(id==LocalGo||id==RemoteGo){navigate(id==RemoteGo,ui::value(controls[id==RemoteGo?RemotePath:LocalPath]));return;}
    if(id==LocalUp){auto parent=fs::path(local).parent_path();if(!parent.empty())navigate(false,parent.wstring());return;}
    if(id==RemoteUp){navigate(true,sftp::join(remote,L".."));return;}
    if(id==Refresh){readLocal();navigate(true,remote);return;}
    if(id!=NewFolder&&id!=Rename&&id!=Delete)return;
    bool remoteSide=remoteFocused; auto entries=selected(remoteSide);
    if(id!=NewFolder && entries.empty())return;
    if(id==Rename&&entries.size()!=1)throw std::runtime_error("Select one file or folder to rename.");
    std::wstring name;
    if(id==NewFolder||id==Rename) {
        char text[4097]{};if(id==Rename){auto initial=wook::utf8(entries[0].name);memcpy(text,initial.c_str(),std::min(initial.size()+1,sizeof(text)));}
        if(!wsPrompt(owner,id==Rename?"Rename":"New folder",remoteSide?"Remote name":"Local name",0,text,sizeof(text)))return;
        if(!wsftp_local_name(text))throw std::runtime_error("Enter a single portable filename, without path separators or reserved characters.");name=wook::wide(text);
    } else {
        auto question=L"Permanently delete "+std::to_wstring(entries.size())+L" selected item(s) from the "+(remoteSide?L"remote":L"local")+L" pane?\n\nOnly files, links and empty folders are removed. This does not use the Recycle Bin.";
        if(MessageBoxW(owner,question.c_str(),L"Delete selected items",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return;
    }
    auto localPath=local, remotePath=remote;
    start([this,id,entries,name,remoteSide,localPath,remotePath](Result&r){
        if(remoteSide) {
            if(id==NewFolder)client->mkdir(sftp::join(remotePath,name));
            else if(id==Rename)client->rename(sftp::join(remotePath,entries[0].name),sftp::join(remotePath,name));
            else for(auto&e:entries)client->remove(sftp::join(remotePath,e.name),e.directory());
        } else {
            auto dest=fs::path(localPath)/name;
            if(id==NewFolder){if(!fs::create_directory(dest))throw std::runtime_error("A file or folder with this name already exists.");}
            else if(id==Rename){if(fs::exists(dest))throw std::runtime_error("The destination already exists.");fs::rename(fs::path(localPath)/entries[0].name,dest);}
            else for(auto&e:entries)fs::remove(fs::path(localPath)/e.name);
        }
        r.path=remotePath;r.entries=client->list(r.path);r.message=L"Done · Folder operations apply to the highlighted pane.";
    });
}
LRESULT CALLBACK viewProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto b=(Browser *)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_NCCREATE){b=((std::unique_ptr<Browser> *)((CREATESTRUCTW *)lp)->lpCreateParams)->release();b->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)b);}
    if(!b)return DefWindowProcW(hwnd,msg,wp,lp);
    try {
        switch(msg) {
#ifdef WOOK_UI_TEST
        case WM_APP+121: return b->busy ? 0 : b->connected ? 1 : -1;
#endif
        case WM_CREATE: {
            const wchar_t *labels[]={L"",L"Upload →",L"← Download",L"New folder",L"Rename",L"Delete",L"Refresh",L"Cancel"};
            for(int i=1;i<=7;++i)b->controls[i]=ui::button(hwnd,labels[i],i);
            b->tooltips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
                CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
            for(int id : {Upload,Download}) {
                TOOLINFOW tip{sizeof(tip)};tip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tip.hwnd=hwnd;tip.uId=(UINT_PTR)b->controls[id];
                tip.lpszText=(wchar_t *)(id==Upload?L"Upload selected local files to the current remote folder (>)":L"Download selected remote files to the current local folder (<)");
                SendMessageW(b->tooltips,TTM_ADDTOOLW,0,(LPARAM)&tip);
            }
            b->controls[ShowHidden]=ui::checkbox(hwnd,L"Show hidden files",ShowHidden);
            SendMessageW(b->controls[ShowHidden],BM_SETCHECK,BST_UNCHECKED,0);
            for(int side=0;side<2;++side) {
                int base=side?RemoteUp:LocalUp;
                b->controls[base]=ui::button(hwnd,L"↑",base);b->controls[base+1]=ui::button(hwnd,L"Go",base+1);
                b->controls[base+2]=ui::edit(hwnd,L"Folder path",base+2);SendMessageW(b->controls[base+2],EM_SETLIMITTEXT,32768,0);
                auto list=b->controls[base+3]=ui::control(hwnd,WC_LISTVIEWW,L"",base+3,LVS_REPORT|LVS_SHOWSELALWAYS|WS_TABSTOP);
                ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
                ListView_SetBkColor(list,ui::bg);ListView_SetTextBkColor(list,ui::bg);ListView_SetTextColor(list,ui::text);
                SetWindowSubclass(ListView_GetHeader(list),headerProc,1,0);
                for(auto [index,title]:{std::pair{0,L"Name"},{1,L"Bytes"},{2,L"Modified"}}){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=(wchar_t *)title;column.cx=100;ListView_InsertColumn(list,index,&column);}
            }
            b->readLocal();SetTimer(hwnd,1,100,nullptr);PostMessageW(hwnd,WM_APP+60,0,0);return 0;
        }
        case WM_APP+60:b->connect();return 0;
        case finished: {
            std::unique_ptr<Result> r((Result *)lp);if(b->worker.joinable())b->worker.join();b->busy=false;b->connected=r->ok;
            b->status=r->message;
            if(r->ok){b->remote=r->path;b->remoteEntries=std::move(r->entries);b->fill(true);b->readLocal();}
            else b->status+=L" Reconnect to continue. An interrupted upload may leave a .wshell-*.part file on the server.";
            b->state();return 0;
        }
        case WM_TIMER: if(b->busy)InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_SIZE:b->layout();return 0;
        case WM_COMMAND:b->action(LOWORD(wp));return 0;
        case WM_SETFOCUS:SetFocus(b->controls[RemoteList]);return 0;
        case WM_NOTIFY: {
            auto h=(NMHDR *)lp;
            if(h->idFrom==LocalList||h->idFrom==RemoteList) {
                if(h->code==LVN_ITEMCHANGED)b->transferState();
                if(h->code==NM_CUSTOMDRAW){
                    auto draw=(NMLVCUSTOMDRAW *)lp;
                    if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
                    if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){
                        int row=(int)draw->nmcd.dwItemSpec;bool selected=ListView_GetItemState(h->hwndFrom,row,LVIS_SELECTED)!=0;
                        ui::fill(draw->nmcd.hdc,draw->nmcd.rc,selected?ui::accent:ui::bg);
                        for(int column=0;column<3;++column){wchar_t text[4097]{};ListView_GetItemText(h->hwndFrom,row,column,text,4097);
                            RECT rect{};ListView_GetSubItemRect(h->hwndFrom,row,column,LVIR_LABEL,&rect);if(!column)rect.right=rect.left+ListView_GetColumnWidth(h->hwndFrom,0);rect.left+=ui::px(6);
                            ui::label(draw->nmcd.hdc,text,rect,column?ui::TextSize::caption:ui::TextSize::body,selected?ui::bg:column?ui::muted:ui::text);}
                        return CDRF_SKIPDEFAULT;
                    }
                }
                if(h->code==NM_SETFOCUS||h->code==NM_CLICK){b->remoteFocused=h->idFrom==RemoteList;PostMessageW(GetParent(hwnd),WM_APP+46,(WPARAM)hwnd,0);InvalidateRect(hwnd,nullptr,FALSE);}
                if(h->code==NM_DBLCLK&&!b->busy){auto e=b->selected(h->idFrom==RemoteList);if(e.size()==1&&e[0].directory())b->navigate(h->idFrom==RemoteList,h->idFrom==RemoteList?sftp::join(b->remote,e[0].name):(fs::path(b->local)/e[0].name).wstring());}
                if(h->code==LVN_KEYDOWN){auto key=(NMLVKEYDOWN *)lp;if(key->wVKey==VK_F5)b->action(Refresh);}
            } return 0;
        }
        case WM_DRAWITEM: {
            auto item=(DRAWITEMSTRUCT *)lp;
            if(wp==Upload||wp==Download)ui::drawButton(item,!(item->itemState&ODS_DISABLED),ui::TextSize::title,wp==Upload?L">":L"<");
            else ui::drawButton(item);
            return TRUE;
        }
        case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,ui::text);SetBkColor((HDC)wp,ui::raised);SetDCBrushColor((HDC)wp,ui::raised);return (LRESULT)GetStockObject(DC_BRUSH);
        case WM_ERASEBKGND:return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);ui::fill(dc,r,ui::panel);
            int w=MulDiv(r.right,96,ui::dpi),h=MulDiv(r.bottom,96,ui::dpi),span=b->paneWidth;
            RECT path{};GetWindowRect(b->controls[LocalUp],&path);MapWindowPoints(nullptr,hwnd,(POINT *)&path,2);int top=MulDiv(path.top,96,ui::dpi)-30;
            for(int side=0;side<2;++side){auto title=std::wstring(side?L"REMOTE":L"LOCAL")+L"  ·  "+std::to_wstring(ListView_GetItemCount(b->controls[side?RemoteList:LocalList]))+L" items";
                ui::label(dc,title,ui::rect(16+side*(span+b->gutter),top,span,24),ui::TextSize::caption,b->remoteFocused==(side!=0)?ui::accent:ui::muted,true);}
            if(b->transferLabels)for(int id : {Upload,Download}) {
                RECT button{};GetWindowRect(b->controls[id],&button);MapWindowPoints(nullptr,hwnd,(POINT *)&button,2);
                RECT label{button.left-ui::px(8),button.bottom+ui::px(3),button.right+ui::px(8),button.bottom+ui::px(23)};
                ui::label(dc,id==Upload?L"Upload":L"Download",label,ui::TextSize::caption,ui::muted,false,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            auto message=b->status;
            if(b->busy){std::lock_guard lock(b->progressLock);if(!b->current.empty())message=b->current+L" · "+std::to_wstring(b->done.load())+L" / "+std::to_wstring(b->total.load())+L" bytes";}
            ui::label(dc,message,ui::rect(16,h-62,w-32,52),ui::TextSize::caption,ui::text,false,DT_LEFT|DT_WORDBREAK);
            if(b->busy&&b->total){int length=(int)((w-32)*std::min(1.0,(double)b->done/b->total));ui::fill(dc,ui::rect(16,h-68,length,3),ui::accent);}
            EndPaint(hwnd,&ps);return 0;
        }
        case WM_DESTROY:KillTimer(hwnd,1);if(b->tooltips)DestroyWindow(b->tooltips);b->stop();return 0;
        case WM_NCDESTROY:SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);delete b;return DefWindowProcW(hwnd,msg,wp,lp);
        }
    }catch(const std::exception&e){b->status=wook::wide(e.what());b->state();ui::error(b->owner,e);}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
HWND createSftpView(HWND owner,HANDLE job,const std::wstring &session,bool saved) {
    WNDCLASSW wc{};wc.lpfnWndProc=viewProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"wShell.SFTP";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    auto b=std::make_unique<Browser>(owner,job,session,saved);
    auto hwnd=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"SFTP",WS_CHILD|WS_CLIPCHILDREN,0,0,800,600,owner,nullptr,wc.hInstance,&b);
    if(!hwnd)throw std::runtime_error("Cannot create the SFTP tab.");return hwnd;
}
