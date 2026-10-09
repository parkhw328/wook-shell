#include "sftp.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
namespace fs = std::filesystem;
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int wmain() {
    try {
        wchar_t port[32],pin[256],root[32768];
        GetEnvironmentVariableW(L"WOOK_TEST_PORT",port,32);GetEnvironmentVariableW(L"WOOK_TEST_FINGERPRINT",pin,256);GetEnvironmentVariableW(L"WOOK_SFTP_LOCAL",root,32768);
        auto path=wsPath(L"sessions","SFTP fixture");auto store=wsOpen(path,true);free(path);
        wsSet(store,"HostName","127.0.0.1");wsSet(store,"UserName","none");wsSet(store,"Protocol","ssh");
        wsSet(store,"PortNumber",wook::utf8(port).c_str());wsSet(store,"SSHManualHostKeys",wook::utf8(pin).c_str());require(wsSave(store),"save fixture");wsClose(store);
        for(auto name:{"../x","a/b","a\\b","NUL","con.txt","COM1.log","COM\xc2\xb9.txt","x:","x.","x "})require(!wsftp_local_name(name),"unsafe name accepted");
        require(wsftp_local_name("한글-🙂.txt"),"UTF-8 safe name rejected");
        sftp::Client client(nullptr,nullptr,L"SFTP fixture",true);
        require(client.initialize()==L"/","home directory");
        if (GetEnvironmentVariableW(L"WOOK_SFTP_BENCHMARK",nullptr,0)) {
            auto started=GetTickCount64();
            client.download(L"/payload.bin",(fs::path(root)/L"benchmark.bin").wstring(),false,[](uint64_t,uint64_t){});
            std::cout<<"Download milliseconds: "<<GetTickCount64()-started<<'\n';
            return 0;
        }
        auto entries=client.list(L"/");require(!entries.empty(),"listing empty");
        client.mkdir(L"/새 폴더");require(client.canonical(L"/새 폴더/..")==L"/","canonical parent");
        auto local=(fs::path(root)/L"payload.bin").wstring();auto downloaded=(fs::path(root)/L"한글-🙂.bin").wstring();
        unsigned updates=0;auto progress=[&](uint64_t d,uint64_t t){require(d<=t,"progress overflow");++updates;};
        client.upload(local,L"/새 폴더/한글-🙂.bin",false,progress);
        client.download(L"/새 폴더/한글-🙂.bin",downloaded,false,progress);
        require(fs::file_size(local)==fs::file_size(downloaded),"size mismatch");require(updates>20,"missing progress");
        bool blocked=false;try{client.upload(local,L"/새 폴더/한글-🙂.bin",false,progress);}catch(...){blocked=true;}require(blocked,"unconfirmed overwrite allowed");
        client.rename(L"/새 폴더/한글-🙂.bin",L"/새 폴더/renamed.bin");
        client.download(L"/새 폴더/renamed.bin",downloaded,true,progress);
        auto empty=(fs::path(root)/L"empty.txt").wstring();{std::ofstream f{fs::path(empty)};}
        client.upload(empty,L"/empty",false,progress);client.download(L"/empty",(fs::path(root)/L"empty-copy").wstring(),false,progress);
        client.remove(L"/empty",false);client.remove(L"/새 폴더/renamed.bin",false);client.remove(L"/새 폴더",true);
        // Kill a blocked transfer, preserving an existing destination and deleting its stage.
        bool cancelled=false;
        try{client.download(L"/slow.bin",downloaded,true,[&](uint64_t done,uint64_t){if(done)client.cancel();});}catch(...){cancelled=true;}
        require(cancelled,"cancel did not interrupt transfer");
        for(auto&e:fs::directory_iterator(root))require(!e.path().filename().wstring().starts_with(L".wshell-"),"local stage leaked");
        std::cout<<"PASS: SFTP list, UTF-8, upload/download, empty files, rename, mkdir/delete, overwrite refusal, cancellation and safe names\n";
        return 0;
    } catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
