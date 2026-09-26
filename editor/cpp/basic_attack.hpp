#pragma once
#include "mesh.hpp"
namespace starlight::editor {
enum class TelegraphKind { Friendly, Enemy, Healing, EnemyHealing };
// Relative to the viewer; the caster may be a player or an NPC.
inline TelegraphKind telegraph_kind(bool friendly,bool healing){
    return healing?(friendly?TelegraphKind::Healing:TelegraphKind::EnemyHealing):(friendly?TelegraphKind::Friendly:TelegraphKind::Enemy);
}
inline Vec3 telegraph_color(TelegraphKind kind){
    switch(kind){case TelegraphKind::Enemy:return {1,.15,.12};case TelegraphKind::Healing:return {.15,1,.35};case TelegraphKind::EnemyHealing:return {1,.5,.08};default:return {.15,.5,1};}
}
// Small visual-only depth separation prevents competing coplanar channel fills.
inline double telegraph_elevation(TelegraphKind kind){
    switch(kind){case TelegraphKind::Enemy:return .065;case TelegraphKind::EnemyHealing:return .055;case TelegraphKind::Healing:return .045;default:return .035;}
}
struct BasicAttack {
    bool held=false,winding=false,pending=false;
    double windup=0,cooldown=0,cycleSeconds=2.5;
    void press(bool down){if(down && !held && !winding)pending=true;held=down;}
    void cancel(){winding=false;windup=0;pending=false;}
    void clear(){held=false;cancel();}
    // At most one resolution per bounded simulation step; never catch up paused shots.
    bool advance(double dt,bool blocked,double rate=1){
        if(!std::isfinite(dt) || dt<=0 || !std::isfinite(rate) || rate<=0)return false;
        dt=std::min(dt,.05);cooldown=std::max(0.,cooldown-dt*rate);
        if(blocked){cancel();return false;}
        if(!winding && (held || pending) && cooldown<=0){winding=true;pending=false;windup=cycleSeconds;}
        if(winding){windup-=dt*rate;if(windup<=1e-9){cancel();return true;}}
        return false;
    }
};
inline constexpr double basicAttackRange=20;
inline constexpr double basicAttackHalfAngle=.2617993877991494; // adjustable 30-degree full spread
// Sector starts at the feet and widens along the aim, with two metres of height.
inline bool inside_basic_telegraph(Vec3 origin,Vec3 aim,Vec3 point,double range=basicAttackRange,double halfAngle=basicAttackHalfAngle){
    aim=aim.unit();if(aim.length()<.5)return false;
    auto right=aim.cross({0,1,0}).unit();if(right.length()<.5)right={1,0,0};
    auto up=right.cross(aim).unit(),delta=point-origin;
    const double forward=delta.dot(aim),side=delta.dot(right),height=delta.dot(up);
    return forward>=0 && std::hypot(forward,side)<=range && std::abs(side)<=forward*std::tan(halfAngle) && height>=0 && height<=2;
}
// Clip the target's box faces against the sector prism. Return a contact inside
// the overlap so line-of-sight checks do not incorrectly use an outside centre.
inline bool telegraph_contact(Vec3 origin,Vec3 aim,Vec3 low,Vec3 high,Vec3& contact,double range=basicAttackRange,double halfAngle=basicAttackHalfAngle){
    aim=aim.unit();if(aim.length()<.5 || range<=0)return false;
    auto right=aim.cross({0,1,0}).unit();if(right.length()<.5)right={1,0,0};
    auto up=right.cross(aim).unit();
    auto centre=(low+high)*.5;
    if(inside_basic_telegraph(origin,aim,centre,range,halfAngle)){contact=centre;return true;}
    auto contained=[&](Vec3 p){return p.x>=low.x && p.x<=high.x && p.y>=low.y && p.y<=high.y && p.z>=low.z && p.z<=high.z;};
    if(contained(origin+up)){contact=origin+up;return true;}
    struct Plane{Vec3 normal;double offset;};
    std::vector<Plane> planes={{aim*-1,0},{right-aim*std::tan(halfAngle),0},{right*-1-aim*std::tan(halfAngle),0},{up*-1,0},{up,2}};
    // Fine inscribed chords retain the curved range boundary without admitting
    // a target beyond the displayed maximum distance.
    constexpr int segments=64;double step=2*halfAngle/segments;
    for(int i=0;i<segments;i++){double angle=-halfAngle+(i+.5)*step;planes.push_back({aim*std::cos(angle)+right*std::sin(angle),range*std::cos(step*.5)});}
    Vec3 vertices[8];for(int i=0;i<8;i++)vertices[i]=Vec3{(i&1)?high.x:low.x,(i&2)?high.y:low.y,(i&4)?high.z:low.z}-origin;
    const int faces[6][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
    for(auto& face:faces){
        std::vector<Vec3> polygon;for(int index:face)polygon.push_back(vertices[index]);
        for(auto plane:planes){
            if(polygon.empty())break;std::vector<Vec3> clipped;
            auto previous=polygon.back();double before=previous.dot(plane.normal)-plane.offset;
            for(auto current:polygon){double after=current.dot(plane.normal)-plane.offset;
                if((before<=0)!=(after<=0))clipped.push_back(previous+(current-previous)*(before/(before-after)));
                if(after<=0)clipped.push_back(current);previous=current;before=after;
            }polygon=std::move(clipped);
        }
        if(!polygon.empty()){contact=origin;Vec3 sum{};for(auto p:polygon)sum=sum+p;contact=contact+sum*(1./polygon.size());return true;}
    }
    return false;
}
inline bool terrain_telegraph(const TerrainData* terrain,Vec3 origin,Vec3 aim){
    return terrain && std::abs(aim.y)<1e-6 && std::abs(origin.y-terrain->height_at(origin.x,origin.z))<.3;
}
inline Vec3 telegraph_surface(const TerrainData* terrain,Vec3 origin,Vec3 aim,Vec3 point){
    if(terrain_telegraph(terrain,origin,aim))point.y+=terrain->height_at(point.x,point.z)-terrain->height_at(origin.x,origin.z);
    return point;
}
inline bool surface_telegraph_contact(const TerrainData* terrain,Vec3 origin,Vec3 aim,Vec3 low,Vec3 high,Vec3& contact,double range=basicAttackRange,double halfAngle=basicAttackHalfAngle){
    if(!terrain_telegraph(terrain,origin,aim))return telegraph_contact(origin,aim,low,high,contact,range,halfAngle);
    // Subdivide the overlapping footprint; vertical hit height follows the same
    // terrain samples as the ground display, instead of the caster's flat plane.
    low.x=std::max(low.x,origin.x-range);low.z=std::max(low.z,origin.z-range);
    high.x=std::min(high.x,origin.x+range);high.z=std::min(high.z,origin.z+range);
    if(low.x>high.x || low.z>high.z)return false;
    const int nx=std::max(1,int(std::ceil((high.x-low.x)/.5))),nz=std::max(1,int(std::ceil((high.z-low.z)/.5)));
    for(int z=0;z<nz;z++)for(int x=0;x<nx;x++){
        Vec3 a{low.x+(high.x-low.x)*x/nx,low.y,low.z+(high.z-low.z)*z/nz};
        Vec3 b{low.x+(high.x-low.x)*(x+1)/nx,high.y,low.z+(high.z-low.z)*(z+1)/nz};
        double offset=terrain->height_at((a.x+b.x)*.5,(a.z+b.z)*.5)-terrain->height_at(origin.x,origin.z);
        a.y-=offset;b.y-=offset;
        if(telegraph_contact(origin,aim,a,b,contact,range,halfAngle)){contact.y+=offset;return true;}
    }
    return false;
}
}
