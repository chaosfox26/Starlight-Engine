// Native captures exercise the directional catalogue through the real sky
// pipeline, including chart boundaries, resolution and camera motion.
static json night_sky_checks(void* context,const std::filesystem::path& directory){
 std::filesystem::create_directories(directory);json rows=json::array();
 auto skyPacket=[](float hour){std::vector<unsigned char> bytes;auto put=[&](auto value){auto* p=reinterpret_cast<const unsigned char*>(&value);bytes.insert(bytes.end(),p,p+sizeof(value));};put(uint32_t(0x35564c53));put(uint32_t(0));put(uint32_t(0));put(hour);put(0.f);put(180.f);put(0.f);put(uint32_t(0));return bytes;};
 auto capture=[&](const char* name,float hour,float x,float y,float z,float px=0,float pz=0,float storm=0){
  auto packet=skyPacket(hour);if(!sl_scene(context,packet.data(),int(packet.size()))||!sl_surface_weather(context,storm,storm,0,0,nullptr,0))throw std::runtime_error(sl_error());
  sl_camera(context,px,2,pz,px+x,2+y,pz+z);if(!sl_effect_clock(context,0))throw std::runtime_error(sl_error());sl_freeze(context,1);
  for(int frame=0;frame<8;++frame)if(!sl_frame(context,0,13))throw std::runtime_error(sl_error());
  // sl_shot queues the next frame; consume it before changing the scene.
  auto shot=directory/(std::string(name)+".png");sl_shot(context,shot.c_str());
  if(!sl_frame(context,0,13)||!std::filesystem::exists(shot))throw std::runtime_error("Night sky capture did not finish");
  rows.push_back({{"name",name},{"image",shot.generic_string()},{"gpuMs",sl_gpu_ms(context)},{"hour",hour},{"renderWidth",sl_render_width(context)}});
 };
 capture("night-forward",24,0,.35f,1);capture("night-stable",24,0,.35f,1);
 capture("night-translated",24,0,.35f,1,100,100);
 capture("night-micro-motion",24,.0003f,.35f,1);
 capture("night-left",24,-1,.35f,0);capture("night-right",24,1,.35f,0);capture("night-back",24,0,.35f,-1);
 capture("night-zenith",24,.1f,1,.1f);capture("night-seam",24,1,.35f,1);
 capture("night-seam-left",24,.9997f,.35f,1);capture("night-seam-right",24,1,.35f,.9997f);
 capture("night-storm",24,0,.35f,1,0,0,1);capture("day-forward",12,0,.35f,1);
 if(!sl_resize(context,1280,720))throw std::runtime_error(sl_error());capture("night-720",24,0,.35f,1);
 if(!sl_resize(context,2560,1440))throw std::runtime_error(sl_error());capture("night-resized",24,0,.35f,1);
 capture("before-sunset",17.99f,1,.1f,.35f);capture("after-sunset",18.01f,1,.1f,.35f);
 capture("noon",12,0,.6f,1);capture("afternoon",15,1,.4f,.5f);capture("sunset",17.7f,1,.08f,.35f);
 return {{"captures",rows},{"count",rows.size()},{"nativeRaster",true}};
}
