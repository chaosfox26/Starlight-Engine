#pragma once
#include "geometry.hpp"
#include <unordered_map>
namespace starlight::editor {
enum class RockForm {Outcrop,Boulder,Pebble,Crystal};
inline Mesh rock_surface(double height,int subdivisions,uint32_t seed,RockForm form=RockForm::Boulder){
    if(!std::isfinite(height)||height<=0)throw std::invalid_argument("Rock height must be finite and positive");
    subdivisions=std::clamp(subdivisions,0,4);
    const double t=(1+std::sqrt(5.))*.5;
    Mesh mesh;
    for(auto p:std::vector<Vec3>{{-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},{0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},{t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}})mesh.vertices.push_back({p.unit()});
    mesh.indices={0,11,5,0,5,1,0,1,7,0,7,10,0,10,11,1,5,9,5,11,4,11,10,2,10,7,6,7,1,8,3,9,4,3,4,2,3,2,6,3,6,8,3,8,9,4,9,5,2,4,11,6,2,10,8,6,7,9,8,1};
    for(int level=0;level<subdivisions;++level){
        std::unordered_map<uint64_t,int> edges;
        auto midpoint=[&](int a,int b){auto key=(uint64_t(std::min(a,b))<<32)|uint32_t(std::max(a,b));auto at=edges.find(key);if(at!=edges.end())return at->second;int index=int(mesh.vertices.size());mesh.vertices.push_back({(mesh.vertices[a].position+mesh.vertices[b].position).unit()});edges.emplace(key,index);return index;};
        std::vector<int> refined;refined.reserve(mesh.indices.size()*4);
        for(size_t i=0;i<mesh.indices.size();i+=3){int a=mesh.indices[i],b=mesh.indices[i+1],c=mesh.indices[i+2],ab=midpoint(a,b),bc=midpoint(b,c),ca=midpoint(c,a);refined.insert(refined.end(),{a,ab,ca,b,bc,ab,c,ca,bc,ab,bc,ca});}
        mesh.indices=std::move(refined);
    }
    auto random=[&]{seed=seed*1664525u+1013904223u;return double(seed>>8)/16777216.;};
    std::vector<std::pair<Vec3,double>> planes;
    // A closed intersection of cleavage planes drives the silhouette. There
    // is no unit-sphere ceiling leaving large rounded potato-shaped regions.
    int plane=0;
    for(auto direction:std::vector<Vec3>{{1,.10,.05},{-1,-.10,-.13},{.06,1,.12},{-.08,-1,.05},{.13,.12,1},{-.06,.06,-1},{.8,.75,.35},{-.7,.6,-.8},{.7,-.65,-.55},{-.55,-.6,.75},{.9,.1,-.8},{-.7,-.1,.9}}){
        if(plane==2&&form!=RockForm::Crystal)direction=form==RockForm::Outcrop?Vec3{-.22,1,.12}:form==RockForm::Boulder?Vec3{.30,1,.23}:Vec3{.18,1,-.23};
        double perturb=form==RockForm::Crystal?.09:form==RockForm::Outcrop?.36:form==RockForm::Boulder?.42:.50;
        auto normal=(direction+Vec3{random()-.5,random()-.5,random()-.5}*perturb).unit();
        double distance=.70+random()*.22;
        if(plane==2)distance=(form==RockForm::Outcrop?.65:form==RockForm::Pebble?.86:form==RockForm::Boulder?1.04:.70)+random()*.12;
        if(plane==3)distance=.66+random()*.10;
        if(plane>=6)distance=(form==RockForm::Crystal?.62:.68)+random()*.17;
        planes.push_back({normal,distance});++plane;
    }
    // Several unequal oblique crown cuts replace the manufactured flat cap.
    // Unbedded boulders/pebbles stay blocky without repeated sediment rings.
    if(form!=RockForm::Crystal)for(auto direction:std::vector<Vec3>{{.72,1,.40},{-.52,1,.65},{.35,1,-.76},{-.80,1,-.36}}){
        auto normal=(direction+Vec3{random()-.5,random()-.5,random()-.5}*.35).unit();
        planes.push_back({normal,(form==RockForm::Outcrop?.63:.70)+random()*.18});
    }
    double phase=random()*6.283185307179586,lean=(random()-.5)*.24;
    double width=form==RockForm::Outcrop?.87:form==RockForm::Pebble?.78:form==RockForm::Crystal?.57:.75;
    double breadth=form==RockForm::Outcrop?.64:form==RockForm::Pebble?.57:form==RockForm::Crystal?.51:.69;
    double dipX=(random()-.5)*.55,dipZ=(random()-.5)*.45,seamCenter=(random()-.5)*.45,seamSpread=.16+random()*.11;
    Vec3 axisU{1,0,0},axisV{0,1,0},axisW{0,0,1},center{},axes{1,1,1};
    double warpX=0,warpZ=0;
    if(form!=RockForm::Crystal){
        // Weathered shoulders belong to a tilted, off-centre ellipsoid;
        // a sparse set of oblique cleavage cuts breaks it into broad lobes.
        // Removing the six-axis plane cage avoids a chamfered cube silhouette.
        double angle=random()*6.283185307179586,tilt=(random()-.5)*.65;
        axisU={std::cos(angle),0,std::sin(angle)};
        axisV={-std::sin(angle)*std::sin(tilt),std::cos(tilt),std::cos(angle)*std::sin(tilt)};axisW=axisU.cross(axisV);
        center={(random()-.5)*.22,(random()-.5)*.12,(random()-.5)*.20};
        axes={.86+random()*.29,(form==RockForm::Outcrop?.68:.86)+random()*.26,.76+random()*.31};
        warpX=(random()-.5)*.25;warpZ=(random()-.5)*.18;
        planes.clear();
        int cuts=form==RockForm::Outcrop?7:form==RockForm::Boulder?5:3;
        double azimuth=random()*6.283185307179586;
        for(int i=0;i<cuts;++i){
            double az=azimuth+i*2.399963229728653+(random()-.5)*.65;
            double elevation=(i%2==0?.34:-.12)+(random()-.5)*.62;
            auto normal=Vec3{std::cos(az),elevation,std::sin(az)}.unit();
            double distance=(form==RockForm::Outcrop?.65:form==RockForm::Boulder?.77:.84)+random()*.18;
            planes.push_back({normal,distance});
        }
        // A broken crown is a local cut; the remaining shoulder stays curved.
        if(form!=RockForm::Pebble)planes.push_back({Vec3{std::cos(azimuth)*.65,1,std::sin(azimuth)*.65}.unit(),.75+random()*.13});
        planes.push_back({Vec3{.08,-1,-.05}.unit(),.61+random()*.12});
    }
    double low=1e9,high=-1e9;
    for(auto& vertex:mesh.vertices){
        auto direction=vertex.position;double radius=10;
        Vec3 cleavage{};
        if(form!=RockForm::Crystal){
            auto metric=[&](Vec3 p){return Vec3{p.dot(axisU)/axes.x,p.dot(axisV)/axes.y,p.dot(axisW)/axes.z};};
            auto d=metric(direction),origin=metric(center);double a=d.dot(d),b=-2*d.dot(origin),c=origin.dot(origin)-1;
            radius=(-b+std::sqrt(b*b-4*a*c))/(2*a);
            auto p=direction*radius-center;
            cleavage=axisU*(p.dot(axisU)/(axes.x*axes.x))+axisV*(p.dot(axisV)/(axes.y*axes.y))+axisW*(p.dot(axisW)/(axes.z*axes.z));
        }
        for(const auto& [normal,distance]:planes){double facing=direction.dot(normal);if(facing<=.01)continue;double candidate=distance/facing;if(candidate<radius){radius=candidate;cleavage=normal;}}
        auto p=direction*radius;
        // One local, dipping weathered ledge occurs only on outcrops. It fades
        // around the side instead of making periodic full-circumference tiers.
        double bedding=form==RockForm::Outcrop?.028:0;
        double q=(p.y+dipX*p.x+dipZ*p.z-seamCenter)/seamSpread,gouge=std::exp(-q*q),gougeSlope=-2*q*gouge/seamSpread;
        double gateValue=std::tanh(p.x*1.7+p.z*.8+phase*.1),gate=.5+.45*gateValue,gateSlope=.45*(1-gateValue*gateValue);
        double layer=1-bedding*gate*gouge;
        double layerX=-bedding*(gateSlope*1.7*gouge+gate*gougeSlope*dipX),layerY=-bedding*gate*gougeSlope,layerZ=-bedding*(gateSlope*.8*gouge+gate*gougeSlope*dipZ);
        double shear=lean*p.y;if(form!=RockForm::Crystal)shear+=warpX*p.y*p.y;
        // Exact cofactor transform of the actual dipping-ledge Jacobian.
        // Its positive determinant preserves outward plane orientation.
        // Broad plane interiors share normals;
        // raster interpolation softens only their narrow shared edge band.
        // Shared vertices/indices stay intact, without flat triangle normals.
        if(form==RockForm::Crystal){
            // Keep the approved crystal's operation order, including zero bits.
            Vec3 jx{width*(layer+p.x*layerX),0,breadth*p.z*layerX},jy{width*(p.x*layerY+lean),1,breadth*p.z*layerY},jz{width*p.x*layerZ,0,breadth*(layer+p.z*layerZ)};
            vertex.normal=jy.cross(jz)*cleavage.x+jz.cross(jx)*cleavage.y+jx.cross(jy)*cleavage.z;
            vertex.position={width*(p.x*layer+shear),p.y,breadth*p.z*layer};
        }else{
            Vec3 jx{width*(layer+p.x*layerX),0,breadth*p.z*layerX},jy{width*(p.x*layerY+lean+2*warpX*p.y),1,breadth*(p.z*layerY+2*warpZ*p.y)},jz{width*p.x*layerZ,0,breadth*(layer+p.z*layerZ)};
            vertex.normal=jy.cross(jz)*cleavage.x+jz.cross(jx)*cleavage.y+jx.cross(jy)*cleavage.z;
            vertex.position={width*(p.x*layer+shear),p.y,breadth*(p.z*layer+warpZ*p.y*p.y)};
        }
        low=std::min(low,p.y);high=std::max(high,p.y);
    }
    double heightScale=height/(high-low);
    for(auto& vertex:mesh.vertices){vertex.position.y=(vertex.position.y-low)*heightScale;vertex.normal.y/=heightScale;vertex.normal=vertex.normal.unit();}
    return mesh;
}
}
