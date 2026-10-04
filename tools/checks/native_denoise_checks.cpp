#include "platform.hpp"
#include "../audit/denoise_data.hpp"
#include <iostream>
#include <set>
using namespace starlight::editor;
using starlight::audit::DiagnosticImage;
extern "C" {
__declspec(dllimport) void* sl_create(HWND,int,int);
__declspec(dllimport) int sl_scene(void*,const unsigned char*,int);
__declspec(dllimport) int sl_frame(void*,double,float);
__declspec(dllimport) void sl_camera(void*,float,float,float,float,float,float);
__declspec(dllimport) void sl_destroy(void*);
__declspec(dllimport) void sl_rt(void*,int,int);
__declspec(dllimport) void sl_freeze(void*,int);
__declspec(dllimport) void sl_denoiser(void*,int);
__declspec(dllimport) int sl_denoise_capture(void*,int,const wchar_t*);
__declspec(dllimport) int sl_denoise_history_index(void*);
__declspec(dllimport) int sl_rr_active(void*);
__declspec(dllimport) const char* sl_rr_status(void*);
__declspec(dllimport) const char* sl_error();
__declspec(dllimport) void sl_shot(void*,const wchar_t*);
}
int wmain(int argc,wchar_t** argv){
    fs::path packetPath,out,build;std::wstring config=L"Release";json metrics=json::object(),checks=json::array(),result,denoiserState=json::object();std::set<std::string> recorded;
    try{
        for(int i=1;i<argc;i+=2){if(i+1>=argc)throw std::runtime_error("Missing argument");std::wstring key=argv[i];if(key==L"--packet")packetPath=fs::absolute(argv[i+1]);else if(key==L"--output")out=fs::absolute(argv[i+1]);else if(key==L"--build-dir")build=fs::absolute(argv[i+1]);else if(key==L"--config")config=argv[i+1];else throw std::runtime_error("Unknown option");}
        if(!build.empty()){wchar_t loaded[32768]{};auto module=GetModuleHandleW(L"Starlight.Viewport.dll");if(!module || !GetModuleFileNameW(module,loaded,32768) || !fs::equivalent(loaded,build/config/L"Starlight.Viewport.dll"))throw std::runtime_error("Loaded viewport does not match requested build");}
        if(packetPath.empty() || out.empty() || !fs::create_directories(out))throw std::runtime_error("Packet and fresh output required");
        SetEnvironmentVariableW(L"STARLIGHT_D3D_DEBUG",L"1");SetEnvironmentVariableW(L"STARLIGHT_NATIVE_LOG",(out/L"debug.log").c_str());auto packet=read_file(packetPath);
        auto require=[&](bool ok,const std::string& label){if(!ok)throw std::runtime_error(label);if(recorded.insert(label).second)checks.push_back(label);};
        for(int mode=0;mode<2;++mode){
            HWND window=CreateWindowExW(0,L"STATIC",L"Starlight denoiser validation",0,0,0,1280,720,nullptr,nullptr,nullptr,nullptr);if(!window)throw std::runtime_error("Cannot create diagnostic window");
            struct Context {HWND window;void* value;~Context(){if(value)sl_destroy(value);DestroyWindow(window);}} context{window,sl_create(window,1280,720)};auto ctx=context.value;
            require(ctx!=nullptr,"create context");
            auto active=[&](){auto status=sl_rr_status(ctx);denoiserState={{"requestedMode",mode},{"rayReconstructionActive",bool(sl_rr_active(ctx))},{"status",status?status:""}};return bool(sl_rr_active(ctx));};
            auto advance=[&](int n,double seconds=0){for(int i=0;i<n;++i)require(sl_frame(ctx,seconds,13)==1,"frame");};
            auto dump=[&](int index,const std::string& label){auto path=out/(wide(label)+L".bin");require(sl_denoise_capture(ctx,index,path.c_str())==1,"capture "+label);auto image=starlight::audit::decode_capture(read_file(path));require(std::all_of(image.values.begin(),image.values.end(),[](float x){return std::isfinite(x);}),"finite "+label);return image;};
            auto prefix=std::to_string(mode)+"-";
            require(sl_scene(ctx,reinterpret_cast<const unsigned char*>(packet.data()),int(packet.size()))==1,"scene V7 accepted");sl_rt(ctx,4,3);sl_denoiser(ctx,mode);sl_freeze(ctx,1);sl_camera(ctx,0,9.36f,13,0,0,0);advance(72);
            std::vector<DiagnosticImage> raw,filtered;for(int i=0;i<8;++i){advance(2);raw.push_back(dump(0,prefix+"raw-"+std::to_string(i)));filtered.push_back(dump(23,prefix+"filtered-"+std::to_string(i)));}
            auto normals=dump(1,prefix+"normals"),emission=dump(6,prefix+"emission"),depth=dump(4,prefix+"depth");size_t pixels=normals.width*normals.height,ground=0;double rv=0,fv=0;
            for(size_t p=0;p<pixels;++p)if(normals.at(p,1)>.85f && depth.at(p,0)<399 && emission.luma(p)<.01f){++ground;double rmean=0,fmean=0;for(int i=0;i<8;++i){rmean+=raw[i].luma(p)/8.;fmean+=filtered[i].luma(p)/8.;}for(int i=0;i<8;++i){rv+=std::pow(raw[i].luma(p)-rmean,2)/8.;fv+=std::pow(filtered[i].luma(p)-fmean,2)/8.;}}
            require(ground>100,"ground mask populated");double ratio=(fv/ground)/(std::max)(rv/ground,1e-10);metrics[prefix+"groundTemporalVarianceRatio"]=ratio;require(ratio<.8,"mode "+std::to_string(mode)+" reduces static ground temporal variance");require(active()==bool(mode),"mode "+std::to_string(mode)+" actual RR telemetry");
            DiagnosticImage oldGuide,oldId;if(mode==0){oldGuide=dump(sl_denoise_history_index(ctx)+3,"old-guide");oldId=dump(sl_denoise_history_index(ctx)+1,"old-id");}
            sl_camera(ctx,1.5f,9.36f,13,1.5f,0,0);advance(1);auto motion=dump(5,prefix+"motion");dump(23,prefix+"camera-output");float maximum=0;for(size_t p=0;p<pixels;++p)maximum=(std::max)({maximum,std::abs(motion.at(p,0)),std::abs(motion.at(p,1))});require(maximum>1,"camera produces nonzero pixel motion");
            if(mode==0){auto current=dump(1,"moving-normal"),lengths=dump(sl_denoise_history_index(ctx),"moving-history");size_t invalid=0,rejected=0;
                for(size_t p=0;p<pixels;++p){int x=int(p%motion.width),y=int(p/motion.width);int qx=int(std::floor(x+motion.at(p,0)+.5f)),qy=int(std::floor(y+motion.at(p,1)+.5f));bool inside=qx>=0 && qy>=0 && qx<int(motion.width) && qy<int(motion.height);qx=std::clamp(qx,0,int(motion.width)-1);qy=std::clamp(qy,0,int(motion.height)-1);size_t q=size_t(qy)*motion.width+qx;float dot=0;for(int c=0;c<3;++c)dot+=oldGuide.at(q,c)*current.at(p,c);
                    bool valid=inside && motion.at(p,3)>.5f && oldId.at(q,3)==motion.at(p,3) && std::abs(oldGuide.at(q,3)-motion.at(p,2))<(std::max)(.025f,motion.at(p,2)*.012f) && (dot>.92f || (depth.at(p,0)>=399 && oldGuide.at(q,3)>=399));if(!valid){++invalid;if(lengths.at(p,3)==1)++rejected;}}
                require(invalid>100,"camera reveals rejected history pixels");double fraction=double(rejected)/invalid;metrics["native-disocclusionRejectedFraction"]=fraction;require(fraction>.995,"native invalid history starts at length one");
            }
            sl_freeze(ctx,0);for(int i=0;i<40;++i){sl_camera(ctx,float(1.5+std::sin(i*.05)),9.36f,13,1.5f,0,0);advance(1,i/60.);}
            dump(23,prefix+"live-output");sl_shot(ctx,(out/(wide(prefix)+L"live.bmp")).c_str());advance(1);require(active()==bool(mode),"live sequence retains requested denoiser");
            sl_camera(ctx,-7,6,8,-4,0,0);advance(1);dump(23,prefix+"cut-output");if(mode==0){auto image=dump(sl_denoise_history_index(ctx),"cut-history");float longest=0;for(size_t p=0;p<pixels;++p)longest=(std::max)(longest,image.at(p,3));require(longest==1,"camera cut resets native history");}
            auto movingPacket=starlight::audit::identified_diagnostic_packet(packet);
            require(sl_scene(ctx,reinterpret_cast<const unsigned char*>(movingPacket.data()),int(movingPacket.size()))==1,"identified V9 motion fixture accepted");
            sl_denoiser(ctx,mode);sl_camera(ctx,0,9.36f,13,0,0,0);sl_freeze(ctx,1);advance(5);auto [offset,objectId]=starlight::audit::prop_offset(movingPacket);float x=starlight::audit::data_value<float>(movingPacket,offset);
            for(int step=0;step<6;++step){auto changed=movingPacket;float moved=float(double(x)+(step+1)*.1);std::memcpy(changed.data()+offset,&moved,4);auto preference=starlight::audit::preference_offset(movingPacket);std::memcpy(changed.data()+preference,&mode,4);require(sl_scene(ctx,reinterpret_cast<const unsigned char*>(changed.data()),int(changed.size()))==1,"moving prop packet accepted");advance(1);}
            auto propMotion=dump(5,prefix+"prop-motion");float encoded=starlight::audit::half_float(uint16_t(0x3c00+objectId));size_t count=0;double magnitude=0;
            for(size_t p=0;p<pixels;++p)if(propMotion.at(p,3)==encoded){++count;magnitude+=std::hypot(propMotion.at(p,0),propMotion.at(p,1));}
            metrics[prefix+"movingPropPixels"]=count;metrics[prefix+"movingPropMeanPixels"]=count?magnitude/count:0;require(count>8,"moving prop guide pixels found");require(magnitude/count>.1,"dynamic transform produces object motion vectors");dump(23,prefix+"prop-output");sl_denoiser(ctx,1-mode);advance(4);bool switched=active()!=bool(mode);denoiserState["requestedMode"]=1-mode;require(switched,"runtime denoiser switch succeeds");
        }
        result={{"success",true},{"scope","1280x720 fixed scene, image variance, camera reprojection/disocclusion/cut, live shader animation and runtime switching; not a universal zero-ghosting claim"}};
    }catch(const std::exception& e){auto native=sl_error();result={{"success",false},{"error",e.what()},{"nativeError",native?native:""}};}
    result["checks"]=checks;result["metrics"]=metrics;result["denoiserState"]=denoiserState;if(!out.empty() && fs::exists(out))atomic_write(out/L"result.json",result.dump(2));std::cout<<result.dump(2)<<'\n';return result.at("success").get<bool>()?0:1;
}
