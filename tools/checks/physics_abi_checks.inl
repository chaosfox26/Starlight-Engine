// Input-rejection checks for the physics C ABI (audit EL-07). Each hostile call must return an error and leave the world usable; a valid call afterwards proves it.
extern "C" {
__declspec(dllimport) void *slp_create();
__declspec(dllimport) void slp_destroy(void *);
__declspec(dllimport) const char *slp_error();
__declspec(dllimport) uint32_t slp_add(void *, const float *, int, const uint32_t *, int, int, float, float, float, float, float, float, float, float);
__declspec(dllimport) void slp_body_velocity(void *, uint32_t, float, float, float, float, float, float);
__declspec(dllimport) void slp_character_remove(void *, uint32_t);
__declspec(dllimport) void slp_character_platform(void *, uint32_t, float, float, float, float);
__declspec(dllimport) void slp_character_actor(void *, uint32_t, float, float, float, float, float, float);
__declspec(dllimport) int slp_terrain(void *, const float *);
__declspec(dllimport) int slp_rope(void *, uint32_t, uint32_t, const float *, int, float);
}
static void physics_abi_checks(const auto& require){
    const uint32_t refused=0xffffffffu;
    const float cube[24]={-.5f,-.5f,-.5f, .5f,-.5f,-.5f, .5f,.5f,-.5f, -.5f,.5f,-.5f, -.5f,-.5f,.5f, .5f,-.5f,.5f, .5f,.5f,.5f, -.5f,.5f,.5f};
    const uint32_t faces[36]={0,1,2,0,2,3, 4,6,5,4,7,6, 0,4,5,0,5,1, 1,5,6,1,6,2, 2,6,7,2,7,3, 3,7,4,3,4,0};
    const float nan=std::numeric_limits<float>::quiet_NaN();
    void* world=slp_create();
    require(world!=nullptr,"EL-07: A physics world is created for the input checks");
    if(!world)return;
    auto add=[&](bool dynamic,float density,float x,float qx,float qw,int indexCount=36,const float* points=nullptr){return slp_add(world,points?points:cube,8,faces,indexCount,dynamic?1:0,density,x,1,0,qx,0,0,qw);};
    uint32_t good=add(true,1,0,0,1);
    require(good!=refused,"EL-07: A valid dynamic body is still accepted");
    require(add(true,1,nan,0,1)==refused&&std::string(slp_error()).find("pose")!=std::string::npos,"EL-07: A body at a non-finite position is refused with a pose error");
    require(add(true,1,0,0,0)==refused&&std::string(slp_error()).find("rotation")!=std::string::npos,"EL-07: A zero quaternion is refused instead of normalising to NaN");
    require(add(true,1,0,nan,1)==refused,"EL-07: A non-finite rotation is refused");
    require(add(true,0,0,0,1)==refused&&add(true,-1,0,0,1)==refused&&add(true,nan,0,0,1)==refused,"EL-07: Zero, negative and non-finite density are refused for a dynamic body");
    require(add(false,nan,0,0,1)==refused,"EL-07: A non-finite density is refused even for a static body");
    require(add(true,1,0,0,1,6000003)==refused,"EL-07: An index count above the 6,000,000 cap is refused before any index is read");
    require(slp_add(world,nullptr,8,faces,36,1,1,0,1,0,0,0,0,1)==refused&&slp_add(world,cube,8,nullptr,36,1,1,0,1,0,0,0,0,1)==refused,"EL-07: Null point or index arrays are refused");
    float damaged[24];std::memcpy(damaged,cube,sizeof(cube));damaged[3]=nan;
    require(add(false,1,0,0,1,36,damaged)==refused&&std::string(slp_error()).find("vertex")!=std::string::npos,"EL-07: A static mesh with a non-finite vertex is refused");
    require(slp_terrain(world,nullptr)==0&&std::string(slp_error()).find("terrain")!=std::string::npos,"EL-07: A null terrain height array is refused");
    uint32_t after=add(true,1,3,0,1);
    require(after!=refused&&after!=good,"EL-07: The world still accepts a valid body after every refusal");
    slp_body_velocity(world,good,nan,0,0,0,0,0);
    require(std::string(slp_error()).find("velocity")!=std::string::npos,"EL-07: A non-finite body velocity is refused with an error instead of reaching Jolt");
    slp_body_velocity(world,0x7ffffff0u,0,0,0,0,0,0);
    require(std::string(slp_error()).find("no longer exists")!=std::string::npos,"EL-07: A velocity for a body that does not exist is refused");
    slp_character_platform(world,good,0,1,0,0);
    require(std::string(slp_error()).find("interval")!=std::string::npos,"EL-07: A platform move with dt of zero is refused");
    slp_character_platform(world,good,nan,1,0,.016f);
    require(std::string(slp_error()).find("Nonfinite")!=std::string::npos,"EL-07: A non-finite platform pose is refused");
    slp_character_actor(world,good,0,1,0,nan,0,.016f);
    require(std::string(slp_error()).find("Nonfinite")!=std::string::npos,"EL-07: A non-finite actor yaw is refused");
    slp_character_remove(world,after);
    slp_character_remove(world,after);
    require(std::string(slp_error()).find("no longer exists")!=std::string::npos,"EL-07: Removing the same body twice is refused the second time instead of reaching Jolt");
    require(add(true,1,5,0,1)!=refused,"EL-07: The world still accepts a valid body after a double remove");
    // EL-08: a rope that fails part-way must not leave half-built links in the world.
    std::vector<uint32_t> fillers;
    for(int i=0;i<9000;++i){uint32_t id=slp_add(world,cube,8,faces,36,0,1,float(i)*.01f,-5,0,0,0,0,1);if(id==refused)break;fillers.push_back(id);}
    require(fillers.size()>1000&&fillers.size()<9000,"EL-08: The test world is filled to its body capacity");
    for(int i=0;i<4;++i){slp_character_remove(world,fillers.back());fillers.pop_back();}
    uint32_t anchor=add(true,1,0,0,1);
    require(anchor!=refused,"EL-08: One slot is left for the rope's source body");
    const float settings[10]={0,1,0, 2,1,0, 0,0,0, 3};
    require(slp_rope(world,anchor,UINT32_MAX,settings,8,.05f)==0&&std::string(slp_error()).find("capacity")!=std::string::npos,"EL-08: A rope that runs out of body slots part-way is refused with a capacity error");
    int regained=0;for(int i=0;i<3;++i)if(slp_add(world,cube,8,faces,36,0,1,float(i)*.01f,-6,0,0,0,0,1)!=refused)++regained;
    require(regained==3,"EL-08: The slots used by the failed rope's links are given back");
    slp_destroy(world);
}
