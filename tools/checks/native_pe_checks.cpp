#include "../audit/native_pe.hpp"
#include <iostream>
static void require(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
int main(){try{
    std::string bytes(512,'\0');bytes.replace(0,2,"MZ");bytes[0x3c]=char(0x80);bytes.replace(0x80,4,std::string("PE\0\0",4));bytes[0x84]=0x64;bytes[0x85]=char(0x86);bytes[0x98]=0x0b;bytes[0x99]=0x02;
    require(starlight::audit::inspect_pe(bytes).at("passed").get<bool>(),"Native x64 rejected");
    auto managed=bytes;managed[0x98+112+14*8+1]=0x10;require(!starlight::audit::inspect_pe(managed).at("passed").get<bool>(),"CLR image accepted");
    auto x86=bytes;x86[0x84]=0x4c;x86[0x85]=0x01;require(!starlight::audit::inspect_pe(x86).at("passed").get<bool>(),"x86 image accepted");
    for(auto malformed:{std::string("MZ"),bytes.substr(0,200),std::string(512,'\0')}){bool rejected=false;try{starlight::audit::inspect_pe(malformed);}catch(...){rejected=true;}require(rejected,"Malformed image accepted");}
    auto badImport=bytes;badImport[0x98+112+8]=0x01;bool rejected=false;try{starlight::audit::inspect_pe(badImport);}catch(...){rejected=true;}require(rejected,"Invalid import RVA accepted");
    std::cout<<"PASS: native x64, CLR, x86, malformed headers, invalid import address\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
