#include "wsftp.h"
#include "../src/transfer_rate.hpp"
#include <functional>
#include <cmath>
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
    std::function<void(const Mock&)> beforeRead;
    Mock():client(wsftp_create(this,[](void*p,void*b,int n){auto&m=*(Mock*)p;if(m.beforeRead)m.beforeRead(m);size_t count=std::min<size_t>({(size_t)n,m.input.size()-m.offset,7});memcpy(b,m.input.data()+m.offset,count);m.offset+=count;return(int)count;},
        [](void*p,const void*b,int n){auto&m=*(Mock*)p;int count=std::min(n,11);auto bytes=(const unsigned char*)b;m.output.insert(m.output.end(),bytes,bytes+count);return count;})){}
    ~Mock(){wsftp_free(client);}
    void packet(unsigned type,unsigned id,Bytes payload={}){Bytes b{(unsigned char)type};u32(b,id);b.insert(b.end(),payload.begin(),payload.end());u32(input,(uint32_t)b.size());input.insert(input.end(),b.begin(),b.end());}
    void version(bool extension=false){Bytes b;if(extension){str(b,"posix-rename@openssh.com");str(b,"1");}packet(2,3,b);}
    void status(unsigned id,unsigned code){Bytes b;u32(b,code);str(b,"fixture");str(b,"");packet(101,id,b);}
    void handle(){Bytes b;str(b,"h");packet(102,2,b);}
    void data(unsigned id,const std::string& value){Bytes b;str(b,value);packet(103,id,b);}
};
int checks=0;void require(bool ok,const char*message){if(!ok)throw std::runtime_error(message);++checks;}
int collect(void*p,const void*b,int n){auto&out=*(std::string*)p;int count=std::min(n,701);out.append((const char*)b,count);return count;}
unsigned readRequests(const Bytes& bytes){
    unsigned count=0;
    for(size_t at=0;at+4<=bytes.size();){
        uint32_t length=0;for(int i=0;i<4;++i)length=(length<<8)|bytes[at+i];
        if(length>bytes.size()-at-4)break;
        if(bytes[at+4]==5)++count;
        at+=length+4;
    }
    return count;
}
int main(){try{
    {sftp::TransferRate rate;rate.reset(100);
        require(std::abs(rate.update(600,5120)-10.0)<0.001,"KB/s calculation");
        require(std::abs(rate.update(1100,10240)-10.0)<0.001,"KB/s steady rate");
        rate.update(1600,10240);require(rate.update(2100,10240)==0,"stalled speed must reach zero");
        rate.reset(2200);require(rate.update(2300,0)==0,"empty file speed");
        require(std::abs(rate.update(2400,2048)-10.0)<0.001,"new file speed reset");}
    {Mock m;m.version();m.handle();m.data(3,"ab");size_t tailBoundary=m.input.size();
        m.data(4,std::string(32768,'B'));m.data(6,std::string(32766,'A'));m.data(5,"end");m.status(7,0);
        bool retried=false;m.beforeRead=[&](const Mock& wire){if(wire.offset==tailBoundary){retried=true;require(readRequests(wire.output)==4,"short read tail must be queued before draining peers");}};
        require(wsftp_init(m.client),"download init");std::string out;
        require(wsftp_download(m.client,"x",65539,collect,nullptr,&out),"short out-of-order download");
        require(retried&&out=="ab"+std::string(32766,'A')+std::string(32768,'B')+"end","short download bytes/order");}
    {Mock m;m.version();m.handle();m.data(3,std::string(32768,'A'));size_t boundary=m.input.size();
        m.data(5,std::string(32768,'C'));m.data(4,std::string(32768,'B'));
        std::string expected;
        for(unsigned i=0;i<65;++i){std::string block(32768,(char)('A'+i%26));expected+=block;if(i>=3)m.data(3+i,block);}
        m.status(68,0);bool refilled=false;
        m.beforeRead=[&](const Mock& wire){if(wire.offset==boundary){refilled=true;require(readRequests(wire.output)==65,"read window must refill before batch drains");}};
        require(wsftp_init(m.client),"window init");std::string out;
        require(wsftp_download(m.client,"x",expected.size(),collect,nullptr,&out),"sliding window download");
        require(refilled&&out==expected,"sliding window bytes/order");}
    for(int fault=0;fault<7;++fault){Mock m;m.version();m.handle();
        if(fault==0)m.data(999,"x"); // Unknown request.
        if(fault==1)m.data(3,"");
        if(fault==2)m.data(3,"too long");
        if(fault==3)m.status(3,1); // File shrank.
        if(fault==4)m.packet(103,3,{0,0,0,3,'x'});
        if(fault==5){m.data(4,"y");m.data(4,"y");} // Duplicate completed block.
        if(fault==6)m.data(3,"x"); // Local write failure.
        require(wsftp_init(m.client),"invalid download init");std::string out;
        require(!wsftp_download(m.client,"x",fault==5?32769:1,fault==6?+[](void*,const void*,int){return -1;}:collect,nullptr,&out),"invalid download accepted");
        auto sent=m.output.size();WsFtpAttrs attrs{};require(!wsftp_stat(m.client,"x",&attrs)&&m.output.size()==sent,"failed pipeline connection reused");}
    {Mock m;m.version();m.handle();m.status(3,0);require(wsftp_init(m.client),"empty init");std::string out;
        require(wsftp_download(m.client,"x",0,collect,nullptr,&out)&&out.empty(),"empty download");}
    {Mock m;m.version();m.handle();require(wsftp_init(m.client),"cancel init");std::string out;
        require(!wsftp_download(m.client,"x",65536,collect,[](void*,uint64_t,uint64_t){return 0;},&out),"initial cancellation ignored");
        require(readRequests(m.output)==0,"reads issued after cancellation");}
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
