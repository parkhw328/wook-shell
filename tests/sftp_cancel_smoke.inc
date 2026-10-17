// Cancel real in-flight transfers through the visible button, then reconnect.
template<typename Check> bool testSftpCancel(App &app, Check require) {
    static int step=0;
    static HWND original{};
    auto local=std::filesystem::path(app.directory)/L"local";
    if(step==0) {
        wchar_t port[32]{},pin[200]{};
        require(GetEnvironmentVariableW(L"WOOK_TEST_PORT",port,32)&&GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT",pin,200),"Missing cancellation fixture");
        Profile p;p.name=L"SFTP cancellation";p.host=L"127.0.0.1";p.port=_wtoi(port);p.user=L"none";
        wook::saveProfile(p);
        auto path=wsPath(L"sessions",wook::utf8(p.name).c_str());auto store=wsOpen(path,1);free(path);
        require(store!=nullptr,"Cannot open cancellation fixture profile");
        wsSet(store,"SSHManualHostKeys",wook::utf8(pin).c_str());int saved=wsSave(store);wsClose(store);
        require(saved!=0,"Cannot save cancellation profile");
        app.connect(p,true,false,false,true);++step;return false;
    }
    auto view=app.tabs[0]->terminal;
    auto state=SendMessageW(view,WM_APP+121,0,0);
    auto cancel=GetDlgItem(view,7);
    auto progress=[&]{wchar_t text[2048]{};SendMessageW(view,WM_APP+122,2048,(LPARAM)text);return std::wstring(text);};
    auto select=[&](int id,const wchar_t *name,bool clear) {
        auto list=GetDlgItem(view,id);auto text=std::wstring(L"   ")+name;
        LVFINDINFOW find{};find.flags=LVFI_STRING;find.psz=text.c_str();
        int row=ListView_FindItem(list,-1,&find);require(row>=0,"Missing cancellation fixture file");
        if(clear)ListView_SetItemState(list,-1,0,LVIS_SELECTED);
        ListView_SetItemState(list,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        return row;
    };
    if(step==1 && state==1) {
        original=view;SetWindowTextW(GetDlgItem(view,10),local.c_str());SendMessageW(view,WM_COMMAND,9,0);
        auto row=select(11,L"upload.bin",true);wchar_t size[80]{};
        ListView_GetItemText(GetDlgItem(view,11),row,1,size,80);
        require(std::wstring(size)==L"67,108,864","Local file sizes must use comma grouping");
        select(11,L"zz-never-uploaded.bin",false);
        SendMessageW(view,WM_COMMAND,1,0);++step;
    } else if((step==2 || step==6) && state==0) {
        auto text=progress();
        if(text.find(L" / 67,108,864 bytes")==std::wstring::npos || text.find(L" · 0 / ")!=std::wstring::npos)return false;
        require(IsWindowVisible(cancel)&&IsWindowEnabled(cancel),"Cancel transfer must be visible and enabled during transfer");
        RECT button{},area{},list{};GetWindowRect(cancel,&button);GetClientRect(view,&area);
        MapWindowPoints(view,nullptr,(POINT *)&area,2);GetWindowRect(GetDlgItem(view,15),&list);
        require(button.left>=area.left && button.right<=area.right && button.bottom<=area.bottom && button.top>=list.bottom,
                "Cancel must fit below the file lists");
        captureTestWindow(app.hwnd,step==2?L"ui-sftp-cancel-upload.bmp":L"ui-sftp-cancel-download.bmp");
        SendMessageW(cancel,BM_CLICK,0,0);
        require(!IsWindowEnabled(cancel) && progress().find(L"Cancelling")!=std::wstring::npos,"Cancel must immediately show pending state and disable duplicate requests");
        ++step;
    } else if((step==3 || step==7) && state!=0) {
        require(state==-1 && progress().find(L"Cancelled")!=std::wstring::npos,"Cancelled transfer must stop and offer reconnect");
        require(IsWindowEnabled(GetDlgItem(view,6))&&!IsWindowEnabled(cancel),"Reconnect must become usable after cancellation");
        require(!std::filesystem::exists(local/L"slow.bin")&&!std::filesystem::exists(local/L"zz-never-downloaded.bin"),"Cancelled batch must not publish unfinished or queued downloads");
        for(const auto &entry:std::filesystem::directory_iterator(local))
            require(!entry.path().filename().wstring().starts_with(L".wshell-"),"Cancelled download must remove staging files");
        auto before=progress();SendMessageW(view,WM_COMMAND,7,0);
        require(progress()==before,"Idle cancellation must do nothing");
        SendMessageW(view,WM_COMMAND,6,0);++step;
    } else if(step==4 && state==1) {
        require(view==original,"Reconnect must reuse the file tab");
        auto row=select(15,L"slow.bin",true);wchar_t size[80]{};
        ListView_GetItemText(GetDlgItem(view,15),row,1,size,80);
        require(std::wstring(size)==L"67,108,864","Remote file sizes must use comma grouping");
        select(15,L"zz-never-downloaded.bin",false);
        SendMessageW(view,WM_COMMAND,2,0);step=6;
    } else if(step==8 && state==1) {
        select(15,L"allowed.txt",true);SendMessageW(view,WM_COMMAND,2,0);++step;
    } else if(step==9 && state==1) {
        require(view==original&&std::filesystem::exists(local/L"allowed.txt"),"A new download must succeed after both cancellations");
        captureTestWindow(app.hwnd,L"ui-sftp-cancel-reconnected.bmp");app.closeTab(0,true);return true;
    }
    return false;
}
