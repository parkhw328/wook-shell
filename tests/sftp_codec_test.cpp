#include "wsftp.h"
#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cstring>
using Bytes=std::vector<unsigned char>;
void u32(Bytes&b,uint32_t n){for(int i=24;i>=0;i-=8)b.push_back((unsigned char)(n>>i));}
void str(Bytes&b,const std::string&s){u32(b,(uint32_t)s.size());b.insert(b.end(),s.begin(),s.end());}
struct Mock {
    Bytes input,output;size_t offset=0;WsFtp *client;
    Mock():client(wsftp_create(this,[](void*p,void*b,int n){auto&m=*(Mock*)p;size_t count=std::min<size_t>({(size_t)n,m.input.size()-m.offset,7});memcpy(b,m.input.data()+m.offset,count);m.offset+=count;return(int)count;},
        [](void*p,const void*b,int n){auto&m=*(Mock*)p;int count=std::min(n,11);auto bytes=(const unsigned char*)b;m.output.insert(m.output.end(),bytes,bytes+count);return count;})){}
    ~Mock(){wsftp_free(client);}
    void packet(unsigned type,unsigned id,Bytes payload={}){Bytes b{(unsigned char)type};u32(b,id);b.insert(b.end(),payload.begin(),payload.end());u32(input,(uint32_t)b.size());input.insert(input.end(),b.begin(),b.end());}
    void version(bool extension=false){Bytes b;if(extension){str(b,"posix-rename@openssh.com");str(b,"1");}packet(2,3,b);}
    void status(unsigned id,unsigned code){Bytes b;u32(b,code);str(b,"fixture");str(b,"");packet(101,id,b);}
};
int checks=0;void require(bool ok,const char*message){if(!ok)throw std::runtime_error(message);++checks;}
int main(){try{
    {Mock m;m.version();require(wsftp_init(m.client),"short transport I/O init");require(!wsftp_atomic_replace(m.client),"unadvertised extension");require(!wsftp_rename(m.client,"a","b",1),"unsafe replace accepted");}
    {Mock m;m.version(true);m.status(2,0);require(wsftp_init(m.client)&&wsftp_atomic_replace(m.client),"extension negotiation");require(wsftp_rename(m.client,"a","b",1),"atomic rename");require(std::string(m.output.begin(),m.output.end()).find("posix-rename@openssh.com")!=std::string::npos,"extension request missing");}
    {Mock m;m.packet(2,6);require(!wsftp_init(m.client),"unsupported version accepted");}
    {Mock m;u32(m.input,1024*1024+1);require(!wsftp_init(m.client),"oversize accepted");}
    {Mock m;m.version();require(wsftp_init(m.client),"init");m.packet(105,999,Bytes(4));WsFtpAttrs a;require(!wsftp_stat(m.client,"x",&a),"mismatched ID accepted");}
    {Mock m;m.version();require(wsftp_init(m.client),"init");m.packet(105,2,{0,0,0,1,0});WsFtpAttrs a;require(!wsftp_stat(m.client,"x",&a)&&strlen(wsftp_error(m.client)),"truncated attributes accepted");}
    {Mock m;m.version();require(wsftp_init(m.client),"init");m.packet(105,2,{0,0,0,16});WsFtpAttrs a;require(!wsftp_stat(m.client,"x",&a),"unknown attrs accepted");}
    {Mock m;m.version();require(wsftp_init(m.client),"init");m.status(2,2);WsFtpAttrs a;require(!wsftp_stat(m.client,"x",&a)&&wsftp_status(m.client)==2,"missing file status");}
    {Mock m;m.version();require(wsftp_init(m.client),"init");Bytes b;u32(b,1);str(b,std::string("x\0y",3));str(b,"");u32(b,0);m.packet(104,2,b);char path[64];require(!wsftp_realpath(m.client,".",path,sizeof(path)),"NUL path accepted");}
    {Mock m;m.version();require(wsftp_init(m.client),"init");Bytes b;str(b,"h");m.packet(102,2,b);b.clear();u32(b,1);str(b,"../escape");str(b,"");u32(b,0);m.packet(104,3,b);require(!wsftp_list(m.client,"/",[](void*,const char*,const WsFtpAttrs*){return 1;},nullptr),"traversal entry accepted");}
    {Mock m;m.version();require(wsftp_init(m.client),"init");Bytes b;str(b,"h");m.packet(102,2,b);m.status(3,0);m.status(3,0);int count=0;
        require(!wsftp_upload(m.client,"/new",65536,[](void*p,void*b,int n){memset(b,0,n);*(int*)p+=n;return n;},nullptr,&count),"duplicate upload acknowledgement accepted");}
    std::cout<<"PASS: "<<checks<<" SFTP framing, negotiation and malformed-reply checks\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
