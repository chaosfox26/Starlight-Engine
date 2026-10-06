// A failed physics update must not keep its time (audit EL-05, error-path part): the backlog used to stay in the accumulator, so the next successful call
// replayed it. The accumulator limit itself (an owner decision) is not changed.
extern "C" {
__declspec(dllimport) int slp_step(void *, double, const void *, int, void *, int);
__declspec(dllimport) void slp_debug_fail_updates(int);
}
static void physics_step_checks(const auto& require){
    struct Body{uint32_t id;float x,y,z,qx,qy,qz,qw;};
    const float cube[24]={-.5f,-.5f,-.5f, .5f,-.5f,-.5f, .5f,.5f,-.5f, -.5f,.5f,-.5f, -.5f,-.5f,.5f, .5f,-.5f,.5f, .5f,.5f,.5f, -.5f,.5f,.5f};
    const uint32_t faces[36]={0,1,2,0,2,3, 4,6,5,4,7,6, 0,4,5,0,5,1, 1,5,6,1,6,2, 2,6,7,2,7,3, 3,7,4,3,4,0};
    const std::vector<float> flat(97*97,0.f);
    // A world only steps once its terrain tiles are ready, so give both worlds the same flat terrain and wait for it.
    auto make=[&]{void* world=slp_create();if(!world)return world;
        if(slp_terrain(world,flat.data())!=1){slp_destroy(world);return static_cast<void*>(nullptr);}
        int state=0;for(int i=0;i<5000&&state==0;++i){state=slp_ready(world);if(state==0)std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        if(state!=1){slp_destroy(world);return static_cast<void*>(nullptr);}
        slp_add(world,cube,8,faces,36,1,1,0,10,0,0,0,0,1);return world;};
    void* control=make();void* failing=make();
    require(control&&failing,"EL-05: Two identical physics worlds are created for the step-recovery check");
    if(!(control&&failing)){if(control)slp_destroy(control);if(failing)slp_destroy(failing);return;}
    Body a[4]{},b[4]{};
    const int controlCount=slp_step(control,1./60,nullptr,0,a,4);
    slp_debug_fail_updates(1);
    const int failed=slp_step(failing,1./60,nullptr,0,b,4);
    slp_debug_fail_updates(0);
    require(controlCount==1&&failed==-1&&std::string(slp_error()).find("capacity")!=std::string::npos,"EL-05: A failed update is reported as a step error");
    Body c[4]{};
    const int recovered=slp_step(failing,1./60,nullptr,0,c,4);
    require(recovered==1,"EL-05: The next step after a failed one succeeds");
    require(recovered==1&&std::memcmp(&a[0],&c[0],sizeof(Body))==0,"EL-05: After a failed update the next step advances exactly one tick, as it would have without the failure (no replayed backlog)");
    slp_destroy(control);slp_destroy(failing);
}
