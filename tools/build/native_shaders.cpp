#include "../audit/native_process.hpp"
#include "../../deep/src/json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <d3dcompiler.h>
#include <wrl/client.h>
namespace fs=std::filesystem;
using json=nlohmann::json;
static std::string read(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("Cannot read shader input: "+path.string());
    std::string text((std::istreambuf_iterator<char>(in)),{});
    // Match the existing generator's universal-newline text reads.
    for(size_t pos=0;(pos=text.find("\r\n",pos))!=std::string::npos;)text.erase(pos,1);
    return text;
}
static void write(const fs::path& path,const std::string& text) {
    std::ofstream out(path,std::ios::binary);out.write(text.data(),text.size());out.close();
    if(!out)throw std::runtime_error("Cannot write shader output: "+path.string());
}
static std::string rasterSource(const std::string& renderer){
    const auto begin=renderer.find("const char* hlsl ="),end=renderer.find("        UINT cflags",begin);
    if(begin==std::string::npos || end==std::string::npos)throw std::runtime_error("Raster shader source anchors missing");
    std::istringstream lines(renderer.substr(begin,end-begin));std::string line,source;
    while(std::getline(lines,line)){
        auto first=line.find_first_not_of(" \t\r");if(first==std::string::npos || line[first]!='\"')continue;
        size_t last=first+1;
        for(;last<line.size();++last){if(line[last]=='\\'){++last;continue;}if(line[last]=='\"')break;}
        if(last>=line.size())throw std::runtime_error("Incomplete raster shader string");
        source+=json::parse(line.substr(first,last-first+1)).get<std::string>();
    }
    if(source.find("PSMain")==std::string::npos || source.find("PSBg")==std::string::npos)throw std::runtime_error("Incomplete raster shader source");
    return source;
}
static void buildRaster(const fs::path& output,const std::string& renderer){
    const auto source=rasterSource(renderer);
    std::string arrays="#pragma once\n#include <cstring>\n#include <cstddef>\nstruct SlRasterBytecode {const void* data;size_t size;};\n";
    std::string table="static SlRasterBytecode slRasterBytecode(const char* entry){\n";
    D3D_SHADER_MACRO macros[]={{"STARLIGHT_LINEAR_HDR","1"},{nullptr,nullptr}};
    for(const auto* entry:{"VSMain","PSMain","VSBg","PSBg","PSAdd","VSSkin","PSSkin","VSFur","PSFur"}){
        Microsoft::WRL::ComPtr<ID3DBlob> blob,error;
        auto result=D3DCompile(source.data(),source.size(),"shaders",macros,nullptr,entry,entry[0]=='V'?"vs_5_0":"ps_5_0",0,0,&blob,&error);
        if(FAILED(result))throw std::runtime_error(std::string("Raster shader ")+entry+": "+(error?static_cast<const char*>(error->GetBufferPointer()):"compile failed"));
        const auto* bytes=static_cast<const unsigned char*>(blob->GetBufferPointer());
        arrays+="static const unsigned char kRaster"+std::string(entry)+"[]={";
        for(size_t i=0;i<blob->GetBufferSize();++i){if(i)arrays+=',';arrays+=std::to_string(bytes[i]);}
        arrays+="};\n";
        table+=" if(std::strcmp(entry,\""+std::string(entry)+"\")==0)return {kRaster"+entry+",sizeof(kRaster"+entry+")};\n";
    }
    table+=" return {nullptr,0};\n}\n";write(output/"raster_shader.h",arrays+table);
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Expected source root, output directory, and DXC executable");
        const auto root=fs::absolute(argv[1]),output=fs::absolute(argv[2]),dxc=fs::absolute(argv[3]);
        fs::create_directories(output);
        auto renderer=read(root/"deep/engine/render_device.h");
        const auto transforms=json::parse(read(root/"tools/build/renderer-transforms.json"));
        for(const auto& op:transforms.at("operations")) {
            const auto kind=op.at("kind").get<std::string>();
            if(kind=="replace") {
                const auto from=op.at("find").get<std::string>(),to=op.at("replacement").get<std::string>();
                if(from.empty() || renderer.find(from)==std::string::npos)throw std::runtime_error("Renderer transform anchor missing: "+from);
                for(size_t pos=0;(pos=renderer.find(from,pos))!=std::string::npos;pos+=to.size())renderer.replace(pos,from.size(),to);
            } else if(kind=="shader-lines") {
                const auto begin=renderer.find(op.at("start").get<std::string>());
                const auto end=renderer.find(op.at("end").get<std::string>(),begin);
                if(begin==std::string::npos || end==std::string::npos)throw std::runtime_error("Shader insertion anchors missing");
                auto shaderSource=read(root/op.at("source").get<std::string>());
                const std::string foliageInclude="#include \"foliage_material.hlsl\"";
                if(auto at=shaderSource.find(foliageInclude);at!=std::string::npos)shaderSource.replace(at,foliageInclude.size(),read(root/"deep/engine/foliage_material.hlsl"));
                const std::string materialInclude="#include \"surface_material.hlsl\"";
                if(auto at=shaderSource.find(materialInclude);at!=std::string::npos)shaderSource.replace(at,materialInclude.size(),read(root/"deep/engine/surface_material.hlsl"));
                const std::string lightingInclude="#include \"surface_lighting.hlsl\"";
                if(auto at=shaderSource.find(lightingInclude);at!=std::string::npos)shaderSource.replace(at,lightingInclude.size(),read(root/"deep/engine/surface_lighting.hlsl"));
                const std::string shadowInclude="#include \"shadow_surface.hlsl\"";
                const std::string skyInclude="#include \"sky_radiance.hlsl\"";
                if(auto at=shaderSource.find(skyInclude);at!=std::string::npos)shaderSource.replace(at,skyInclude.size(),read(root/"deep/engine/sky_radiance.hlsl"));
                const std::string starsInclude="#include \"night_sky.hlsl\"";
                if(auto at=shaderSource.find(starsInclude);at!=std::string::npos)shaderSource.replace(at,starsInclude.size(),read(root/"deep/engine/night_sky.hlsl"));
                const std::string solarInclude="#include \"solar_radiance.hlsl\"";
                if(auto at=shaderSource.find(solarInclude);at!=std::string::npos)shaderSource.replace(at,solarInclude.size(),read(root/"deep/engine/solar_radiance.hlsl"));
                if(auto at=shaderSource.find(shadowInclude);at!=std::string::npos)shaderSource.replace(at,shadowInclude.size(),read(root/"editor/native/shadow_surface.hlsl"));
                std::istringstream lines(shaderSource);
                std::string line,replacement;
                while(std::getline(lines,line))replacement+="            "+json(line+"\n").dump(-1,' ',true)+"\n";
                renderer.replace(begin,end-begin,replacement);
            } else throw std::runtime_error("Unknown renderer transformation");
        }
        write(output/"render_device_editor.h",renderer);
        // Embed the exact release HDR bytecode in the viewport. No shader cache
        // directory or external file is needed in installed/exported games.
        buildRaster(output,renderer);
        write(output/"grass_shader.h","static const char* kGrassShader=R\"GRASS(\n"+read(root/"editor/native/grass_deformation.hlsl")+"\n)GRASS\";\n");
        auto shadow=read(root/"editor/native/shadow_depth.hlsl");
        const std::string windInclude="#include \"grass_wind.hlsl\"";
        if(auto at=shadow.find(windInclude);at!=std::string::npos)shadow.replace(at,windInclude.size(),read(root/"editor/native/grass_wind.hlsl"));
        write(output/"shadow_shader.h","static const char* kSunShadowShader=R\"SUN(\n"+shadow+"\n)SUN\";\n");
        auto atmosphere=read(root/"deep/engine/atmosphere.hlsl");
        const std::string solarInclude="#include \"solar_radiance.hlsl\"";
        if(auto at=atmosphere.find(solarInclude);at!=std::string::npos)atmosphere.replace(at,solarInclude.size(),read(root/"deep/engine/solar_radiance.hlsl"));
        write(output/"atmosphere_shader.h","static const char* kAtmosphereShader=R\"ATMOS(\n"+atmosphere+"\n)ATMOS\";");
        std::string arrays="#pragma once\n";
        for(const auto* name:{"Trace","Prefilter","Temporal","Atrous","Composite","Fog"}) {
            const bool fog=std::string(name)=="Fog";
            const auto binary=output/(std::string(name)+".dxil");
            std::vector<std::wstring> args={L"-T",L"cs_6_5",L"-E",fog?L"Inject":fs::path(name).wstring(),L"-O3"};
            if(fog)args.push_back(L"-DSTARLIGHT_RT_FOG=1");
            args.insert(args.end(),{L"-Fo",binary.wstring(),(root/"deep/engine"/(fog?"atmosphere.hlsl":"dxr.hlsl")).wstring()});
            const auto log=output/(std::string(name)+"-compile.log");
            // Only this generator's fixed per-shader log is replaced on rebuild.
            fs::remove(log);
            const auto result=starlight::audit::run(dxc,args,root,log,120000);
            if(result.timedOut || result.exitCode)throw std::runtime_error("Shader compilation failed: "+std::string(name));
            std::ifstream input(binary,std::ios::binary);
            if(!input)throw std::runtime_error("Compiled shader missing");
            std::string bytes((std::istreambuf_iterator<char>(input)),{});
            if(bytes.empty())throw std::runtime_error("Compiled shader empty");
            if(arrays!="#pragma once\n")arrays+='\n';
            arrays+="static const unsigned char kDxr"+std::string(name)+"[]={";
            for(size_t i=0;i<bytes.size();++i){if(i)arrays+=',';arrays+=std::to_string(static_cast<unsigned char>(bytes[i]));}
            arrays+="};";
        }
        write(output/"dxr_shader.h",arrays);
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
