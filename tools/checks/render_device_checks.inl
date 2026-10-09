// Pure parts of the render device (audit RND): the snapshot smoothing, which must survive bad input.
#include <limits>
#include "../../deep/engine/smooth_xf.h"
#include "../../deep/engine/report_text.h"
static void render_device_checks(const auto& require){
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    {
        SmoothXf s;
        s.follow(1,2,3,0.5f,0.5f);
        require(s.init&&s.x==1&&s.z==2&&s.y==3&&s.yaw==0.5f,"RND-1: The first sample is stored as is");
        s.follow(3,2,3,0.5f,0.5f);
        require(s.x==2,"RND-1: A finite sample moves the state toward the target");
        for(float bad:{1e30f,-1e30f,inf,-inf,nan}){
            const SmoothXf before=s;
            s.follow(5,5,5,bad,0.5f);
            s.follow(bad,5,5,0.f,0.5f);
            s.follow(5,bad,5,0.f,0.5f);
            s.follow(5,5,bad,0.f,0.5f);
            if(std::isfinite(bad)){require(std::isfinite(s.yaw),"RND-1: A huge finite yaw returns and stays finite");s=before;}
            else require(s.x==before.x&&s.z==before.z&&s.y==before.y&&s.yaw==before.yaw,"RND-1: A non-finite value leaves the smoothed state untouched");
        }
        s.follow(2,2,3,0.5f,0.5f);
        require(std::isfinite(s.x)&&std::isfinite(s.yaw),"RND-1: The state recovers on the next finite sample");
    }
    {
        SmoothXf s;
        s.follow(0,0,0,6.0f,1.f);
        s.follow(0,0,0,0.1f,0.5f);
        require(std::fabs(s.yaw-(6.0f+0.5f*(0.1f+6.2831853f-6.0f)))<1e-4f,"RND-1: Yaw still turns the short way across the wrap");
        SmoothXf first;first.follow(nan,0,0,0,1.f);
        require(!first.init,"RND-1: A non-finite first sample does not initialise the entity");
    }
    {
        std::string models="[";
        for(int i=0;i<40;++i)models+=report_text::format("%s{\"name\":\"%s\",\"verts\":%zu}",i?",":"",std::string(100,'m').c_str(),(size_t)i);
        models+="]";
        const std::string report=report_text::format("{\"models\":%s,\"tail\":%d}\n",models.c_str(),7);
        require(report.size()>4000&&report.find("\"tail\":7}")!=std::string::npos&&report.find("\"verts\":39")!=std::string::npos,"RND-6: A report longer than any fixed buffer is written whole");
        require(report_text::format("%s","").empty()&&report_text::format("plain")=="plain","RND-6: Empty and plain formats work");
        require(report_text::escape(std::string("a\nb\tc\"d\\e\x01"))=="a\\u000ab\\u0009c\\\"d\\\\e\\u0001","RND-6: Control characters, quotes and backslashes are escaped");
    }
}
