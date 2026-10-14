// Isolated live ConPTY/SFTP tests, driven by drag-integration.cjs.
void runDragSmoke(App &app) {
    static int phase=0,waits=0,checks=0;
    static App *detached=nullptr;
    static HWND terminal=nullptr,view=nullptr;
    static DWORD pid=0;
    static uint64_t identity=0;
    static bool folderAlert=false;
    static IStream *retainedStream=nullptr;
    auto require=[&](bool ok,const char *message){if(!ok)throw std::runtime_error(message);++checks;};
    auto local=std::filesystem::path(app.directory)/L"local";
    auto output=std::filesystem::path(app.directory)/L"download";
    auto ready=[&]{return view&&SendMessageW(view,WM_APP+121,0,0)==1;};
    auto selectFile=[&](int id,const wchar_t *name){
        auto list=GetDlgItem(view,id);ListView_SetItemState(list,-1,0,LVIS_SELECTED);
        for(int i=0;i<ListView_GetItemCount(list);++i){wchar_t text[512]{};ListView_GetItemText(list,i,0,text,512);
            if(std::wstring(text).substr(3)==name){ListView_SetItemState(list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);return;}}
        throw std::runtime_error("Missing SFTP test item");
    };
    auto send=[&](HWND target,const std::wstring &text){for(wchar_t ch:text)PostMessageW(target,WM_CHAR,ch,1);PostMessageW(target,WM_CHAR,L'\r',1);};
    auto dragTab=[&](App &source,POINT screen){
        RedrawWindow(source.hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
        auto hit=std::find_if(source.hits.begin(),source.hits.end(),[](const auto &h){return !h.close&&h.index==0;});
        require(hit!=source.hits.end(),"The source tab must be visible for dragging");
        POINT start{hit->rect.left+10,hit->rect.top+10},end=screen;ScreenToClient(source.hwnd,&end);
        SendMessageW(source.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));
        require(source.dragTab==0,"Mouse down must arm the tab drag");
        SendMessageW(source.hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
        require(source.dragging,"Mouse movement must start tab dragging");
        SendMessageW(source.hwnd,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));
    };
    try {
        if(++waits>250)throw std::runtime_error("Drag integration timed out at phase "+std::to_string(phase));
        if(phase==0){app.localShell(false);phase=1;}
        else if(phase==1&&!app.tabs.empty()&&IsWindow(app.tabs[0]->terminal)&&app.inputReady(app.tabs[0].get())){
            terminal=app.tabs[0]->terminal;pid=app.tabs[0]->pid;identity=app.tabs[0]->id;
            send(terminal,L"set WSHELL_DETACH_CHECK=kept");
            SetWindowPos(app.hwnd,nullptr,20,20,0,0,SWP_NOSIZE|SWP_NOZORDER);
            RECT bounds{};GetWindowRect(app.hwnd,&bounds);dragTab(app,{bounds.right+50,bounds.top+100});
            require(!detachedWorkspaces.empty(),"Releasing a dragged tab outside its window must create a new window");
            detached=detachedWorkspaces.back().get();SetWindowPos(detached->hwnd,nullptr,500,120,0,0,SWP_NOSIZE|SWP_NOZORDER);
            require(app.tabs.empty()&&detached->tabs.size()==1&&GetParent(terminal)==detached->hwnd,"Detach must reparent the existing terminal");
            require(detached->tabs[0]->id==identity&&detached->tabs[0]->pid==pid,"Detach must preserve tab identity and process");
            send(terminal,L"echo %WSHELL_DETACH_CHECK%>\""+(output/L"session.txt").wstring()+L"\"");phase=2;
        } else if(phase==2&&std::filesystem::exists(output/L"session.txt")){
            std::ifstream f(output/L"session.txt");std::string text;std::getline(f,text);
            require(text=="kept","Detached terminal must retain its environment and execute commands");
            captureTestWindow(detached->hwnd,L"ui-detached.bmp");
            // Validate session transfer independently of other applications covering
            // the destination. Physical drop hit testing is a separate manual check.
            require(detached->moveTab(0,app),"Reattach must succeed");
            require(app.tabs.size()==1&&app.tabs[0]->id==identity&&GetParent(terminal)==app.hwnd&&!IsWindow(detached->hwnd),"Reattach must preserve the session and close the empty detached window");
            wchar_t port[32]{},fingerprint[200]{};GetEnvironmentVariableW(L"WOOK_TEST_PORT",port,32);GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT",fingerprint,200);
            Profile p;p.name=L"Drag SFTP";p.host=L"127.0.0.1";p.user=L"none";p.port=_wtoi(port);
            wook::saveProfile(p);auto path=wsPath(L"sessions",wook::utf8(p.name).c_str());auto store=wsOpen(path,1);free(path);
            wsSet(store,"SSHManualHostKeys",wook::utf8(fingerprint).c_str());require(wsSave(store),"Cannot pin SFTP fixture");wsClose(store);
            app.connect(p,true,false,false,true);phase=3;
        } else if(phase==3&&app.tabs.size()==2){view=app.tabs[1]->terminal;if(!ready())return;
            SetWindowTextW(GetDlgItem(view,10),local.c_str());SendMessageW(view,WM_COMMAND,9,0);
            auto data=fileDrag::local({(local/L"upload-한글.bin").wstring()});
            auto names=fileDrag::paths(data);require(names.size()==1&&names[0]==(local/L"upload-한글.bin").wstring(),"CF_HDROP must preserve Unicode paths");
            require(SendMessageW(view,WM_APP+123,1,(LPARAM)data)!=0,"Explorer file drop must start an upload");data->Release();phase=4;
        } else if(phase==4&&ready()){
            selectFile(15,L"upload-한글.bin");
            fileDrag::probe=[&](IDataObject *data)->HRESULT {
                FORMATETC format{(CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium{};
                require(SUCCEEDED(data->GetData(&format,&medium)),"Remote drag must expose file descriptors");
                auto group=(FILEGROUPDESCRIPTORW *)GlobalLock(medium.hGlobal);
                require(group->cItems==1&&std::wstring(group->fgd[0].cFileName)==L"upload-한글.bin","Virtual filename must match the selected remote file");
                GlobalUnlock(medium.hGlobal);ReleaseStgMedium(&medium);
                format={(CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILECONTENTS),nullptr,DVASPECT_CONTENT,0,TYMED_ISTREAM};
                require(SUCCEEDED(data->GetData(&format,&medium)),"Remote drag must supply an Explorer-compatible stream");
                retainedStream=medium.pstm;retainedStream->AddRef();
                std::ofstream out(output/L"export.bin",std::ios::binary);char buffer[8192];ULONG read=0;HRESULT hr;
                do {hr=medium.pstm->Read(buffer,sizeof(buffer),&read);out.write(buffer,read);}while(hr==S_OK);
                require(hr==S_FALSE,"Remote stream must terminate cleanly");
                LARGE_INTEGER zero{};require(SUCCEEDED(medium.pstm->Seek(zero,STREAM_SEEK_SET,nullptr)),"Explorer stream must support rewind");
                ReleaseStgMedium(&medium);return DRAGDROP_S_DROP;
            };
            SendMessageW(view,WM_APP+124,1,0);fileDrag::probe={};
            require(retainedStream!=nullptr,"Explorer must receive a stream");
            char byte{};ULONG bytes=0;
            require(SUCCEEDED(retainedStream->Read(&byte,1,&bytes))&&bytes==1,"Export stream must remain readable after the drag ends");
            retainedStream->Release();retainedStream=nullptr;
            require(std::filesystem::exists(output/L"export.bin"),"Virtual download must produce a file");
            SetWindowTextW(GetDlgItem(view,10),output.c_str());SendMessageW(view,WM_COMMAND,9,0);
            selectFile(15,L"upload-한글.bin");
            fileDrag::probe=[&](IDataObject *data)->HRESULT {require(SendMessageW(view,WM_APP+123,0,(LPARAM)data)!=0,"Remote-to-local pane drop must start download");return DRAGDROP_S_DROP;};
            SendMessageW(view,WM_APP+124,1,0);fileDrag::probe={};phase=5;
        } else if(phase==5&&ready()){
            require(std::filesystem::exists(output/L"upload-한글.bin"),"Internal download must preserve the filename");
            selectFile(11,L"export.bin");
            fileDrag::probe=[&](IDataObject *data)->HRESULT {require(SendMessageW(view,WM_APP+123,1,(LPARAM)data)!=0,"Local-to-remote pane drop must start upload");return DRAGDROP_S_DROP;};
            SendMessageW(view,WM_APP+124,0,0);fileDrag::probe={};phase=6;
        } else if(phase==6&&ready()){
            static int deletePane=0;
            if(deletePane<2){
                int listId=deletePane==0?11:15;
                auto fixture=(deletePane==0?output:std::filesystem::path(app.directory)/L"remote")/L"delete-me.txt";
                selectFile(listId,L"delete-me.txt");
                auto confirm=[&](UINT key){
                    static UINT answer;static bool seen,defaultNo;answer=key;seen=false;defaultNo=false;
                    SetPropW(app.hwnd,L"wShell.DragActive",(HANDLE)1);
                    auto timer=SetTimer(nullptr,0,80,[](HWND,UINT,UINT_PTR,DWORD){
                        EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL {
                            wchar_t title[128]{};GetWindowTextW(window,title,128);
                            if(!seen&&std::wstring(title)==L"Delete selected items"){
                                seen=true;defaultNo=GetFocus()==GetDlgItem(window,IDNO);
                                // A held Y must not approve deletion; the fresh answer follows it.
                                PostMessageW(window,WM_KEYDOWN,'Y',1LL<<30);
                                PostMessageW(window,WM_KEYDOWN,answer,1);
                            }return TRUE;
                        },0);
                    });
                    auto list=GetDlgItem(view,listId);SetFocus(list);
                    NMLVKEYDOWN keydown{};keydown.hdr={list,(UINT_PTR)listId,LVN_KEYDOWN};keydown.wVKey=VK_DELETE;
                    SendMessageW(view,WM_NOTIFY,listId,(LPARAM)&keydown);
                    KillTimer(nullptr,timer);RemovePropW(app.hwnd,L"wShell.DragActive");
                    require(seen&&defaultNo,"Delete must open a confirmation with No focused");
                };
                for(UINT key:{UINT('N'),UINT(VK_ESCAPE),UINT(VK_RETURN)}){
                    confirm(key);require(std::filesystem::exists(fixture)&&ready(),"N, Escape and default Enter must preserve the selected file");
                }
                confirm('Y');++deletePane;return;
            }
            require(!std::filesystem::exists(output/L"delete-me.txt")&&
                !std::filesystem::exists(std::filesystem::path(app.directory)/L"remote"/L"delete-me.txt"),
                "Y must delete both local and remote selected files");
            selectFile(15,L"export.bin");
            fileDrag::probe=[&](IDataObject *)->HRESULT {return DRAGDROP_S_CANCEL;};
            SendMessageW(view,WM_APP+124,1,0);fileDrag::probe={};
            require(ready(),"Cancelling a drag before content retrieval must keep SFTP connected");
            selectFile(15,L"projects");
            auto timer=SetTimer(nullptr,0,80,[](HWND,UINT,UINT_PTR,DWORD){
                EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL {
                    wchar_t title[256]{};GetWindowTextW(window,title,256);
                    if(std::wstring(title)==L"SFTP · 파일만 전송 가능") {folderAlert=true;PostMessageW(window,WM_COMMAND,IDOK,0);}return TRUE;
                },0);
            });
            SendMessageW(view,WM_APP+124,1,0);KillTimer(nullptr,timer);
            require(folderAlert,"Dragging a remote folder must show the explicit file-only alert");
            require(!std::filesystem::exists(output/L"projects"),"Folder drag must not create a download");
            app.detachTab(1,{440,200});detached=detachedWorkspaces.back().get();
            require(GetParent(view)==detached->hwnd&&ready(),"Connected SFTP tabs must also detach");
            captureTestWindow(detached->hwnd,L"ui-sftp-drag.bmp");
            require(detached->moveTab(0,app),"SFTP tab must reattach");
            app.detachTab(0,{460,220});detached=detachedWorkspaces.back().get();
            detached->localShell(false);
            require(detached->tabs.size()==2&&detached->tabs[0]->storageName!=detached->tabs[1]->storageName,
                "New sessions in a detached window must use independent temporary settings");
            DestroyWindow(app.hwnd);
            require(IsWindow(detached->hwnd)&&WaitForSingleObject(detached->tabs[0]->process,0)==WAIT_TIMEOUT,
                "Closing the original window must leave the detached session alive");
            MSG pending{};require(!PeekMessageW(&pending,nullptr,WM_QUIT,WM_QUIT,PM_NOREMOVE),"The last surviving window must keep the application running");
            send(terminal,L"echo %WSHELL_DETACH_CHECK%>\""+(output/L"survivor.txt").wstring()+L"\"");
            auto deadline=GetTickCount64()+5000;
            while(!std::filesystem::exists(output/L"survivor.txt")&&GetTickCount64()<deadline){
                while(PeekMessageW(&pending,nullptr,0,0,PM_REMOVE)){TranslateMessage(&pending);DispatchMessageW(&pending);}
                MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);
            }
            require(std::filesystem::exists(output/L"survivor.txt"),"Detached shell must still accept commands after original window closes");
            std::ofstream result(std::filesystem::path(app.directory)/L"drag-result.json");result<<"{\"passed\":true,\"checks\":"<<checks<<"}";result.close();
            phase=99;for(auto &tab:detached->tabs)if(IsWindow(tab->terminal))PostMessageW(tab->terminal,WM_CLOSE,0,0);DestroyWindow(detached->hwnd);
        }
    }catch(const std::exception &e){
        if(retainedStream){retainedStream->Release();retainedStream=nullptr;}
        fileDrag::probe={};std::ofstream result(std::filesystem::path(app.directory)/L"drag-result.json");
        result<<"{\"passed\":false,\"phase\":"<<phase<<",\"error\":\""<<e.what()<<"\"}";result.close();phase=99;
        auto windows=workspaces;for(auto window:windows){
            for(auto &tab:window->tabs)if(tab->process)PostMessageW(tab->terminal,WM_CLOSE,0,0);
            DestroyWindow(window->hwnd);
        }
    }
}
