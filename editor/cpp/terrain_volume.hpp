#pragma once
#include "terrain_refinement.hpp"
#include <unordered_map>
#include <numbers>
#include <mutex>
namespace starlight::editor {
inline Mesh build_terrain_volume_geometry(const TerrainData& terrain,int xBegin=0,int xEnd=97,int zBegin=0,int zEnd=97){
    terrain.validate();
    constexpr double step=.25,start=-12.125;
    const int countX=xEnd-xBegin+1,countZ=zEnd-zBegin+1;
    double top=*std::max_element(terrain.Heights.begin(),terrain.Heights.end());
    for(const auto& edit:terrain.Volumes)if(!edit.Subtract)top=std::max(top,edit.Y+edit.Radius);
    double bottom=-21.125;
    int layers=int(std::ceil((top+.375-bottom)/step))+1;
    auto index=[&](int x,int y,int z){return (y*countZ+z)*countX+x;};
    auto position=[&](int x,int y,int z){return Vec3{start+(x+xBegin)*step,bottom+y*step,start+(z+zBegin)*step};};
    std::vector<float> field(size_t(countX)*countZ*layers);
    for(int y=0;y<layers;++y)for(int z=0;z<countZ;++z)for(int x=0;x<countX;++x){auto p=position(x,y,z);field[index(x,y,z)]=float(terrain.density_at(p.x,p.y,p.z));}
    Mesh mesh;std::unordered_map<uint64_t,int> shared;
    Vec3 palette[]={{.278,.451,.271},{.502,.388,.29},{.784,.71,.506},{.467,.49,.525},{.769,.824,.863}};
    auto gradient=[&](Vec3 p){constexpr double e=.01;return Vec3{terrain.density_at(p.x+e,p.y,p.z)-terrain.density_at(p.x-e,p.y,p.z),terrain.density_at(p.x,p.y+e,p.z)-terrain.density_at(p.x,p.y-e,p.z),terrain.density_at(p.x,p.y,p.z+e)-terrain.density_at(p.x,p.y,p.z-e)}.unit();};
    auto surfaceVertex=[&](Vec3 p){
        auto n=gradient(p);
        double gx=std::clamp((p.x+12)*4,0.,96.),gz=std::clamp((p.z+12)*4,0.,96.);
        int ix=std::min(int(gx),95),iz=std::min(int(gz),95);double u=gx-ix,v=gz-iz;
        int samples[]={iz*97+ix,iz*97+ix+1,(iz+1)*97+ix,(iz+1)*97+ix+1};double weights[]={(1-u)*(1-v),u*(1-v),(1-u)*v,u*v};
        Vec3 color;double kind=0;for(int k=0;k<4;++k){int paint=terrain.Paint[samples[k]];color=color+palette[paint]*weights[k];kind+=(paint+1)*weights[k];}
        auto smooth=[](double a,double b,double x){double t=std::clamp((x-a)/(b-a),0.,1.);return t*t*(3-2*t);};
        double rock=std::max(1-smooth(.35,.8,n.y),1-smooth(-.6,-.05,p.y-terrain.height_at(p.x,p.z)));
        color=color*(1-rock)+palette[3]*rock;kind=kind*(1-rock)+4*rock;
        return Vertex{p,n,color,float(kind)};
    };
    auto vertex=[&](int a,int b,Vec3 pa,Vec3 pb){
        uint64_t key=(uint64_t(std::min(a,b))<<32)|uint32_t(std::max(a,b));auto existing=shared.find(key);if(existing!=shared.end())return existing->second;
        double lo=0,hi=1,fa=field[a],fb=field[b],t=fa/(fa-fb);Vec3 p;
        for(int iteration=0;iteration<4;++iteration){
            p=pa+(pb-pa)*t;double density=terrain.density_at(p.x,p.y,p.z);if(std::abs(density)<1e-6)break;
            if((density<0)==(fa<0)){lo=t;fa=density;}else{hi=t;fb=density;}
            if(iteration<3)t=lo+(hi-lo)*fa/(fa-fb);
        }
        int result=int(mesh.vertices.size());mesh.vertices.push_back(surfaceVertex(p));shared.emplace(key,result);return result;
    };
    constexpr int tetrahedra[6][4]={{0,1,3,7},{0,3,2,7},{0,2,6,7},{0,6,4,7},{0,4,5,7},{0,5,1,7}};
    constexpr int edges[6][2]={{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}};
    for(int y=0;y<layers-1;++y)for(int z=0;z<countZ-1;++z)for(int x=0;x<countX-1;++x){
        int ids[8];Vec3 points[8];int inside=0;
        for(int c=0;c<8;++c){int xx=x+(c&1),yy=y+((c>>1)&1),zz=z+((c>>2)&1);ids[c]=index(xx,yy,zz);points[c]=position(xx,yy,zz);inside+=field[ids[c]]<0;}
        if(inside==0 || inside==8)continue;
        for(const auto& tet:tetrahedra){
            std::vector<int> polygon;
            for(const auto& edge:edges){int a=tet[edge[0]],b=tet[edge[1]];if((field[ids[a]]<0)!=(field[ids[b]]<0))polygon.push_back(vertex(ids[a],ids[b],points[a],points[b]));}
            if(polygon.size()<3)continue;
            Vec3 center;for(int v:polygon)center=center+mesh.vertices[v].position;center=center/double(polygon.size());
            auto e1=points[tet[1]]-points[tet[0]],e2=points[tet[2]]-points[tet[0]],e3=points[tet[3]]-points[tet[0]];
            double f0=field[ids[tet[0]]];
            auto normal=((e2.cross(e3)*(field[ids[tet[1]]]-f0)+e3.cross(e1)*(field[ids[tet[2]]]-f0)+e1.cross(e2)*(field[ids[tet[3]]]-f0))/e1.dot(e2.cross(e3))).unit();
            auto axis=normal.cross(std::abs(normal.y)<.9?Vec3{0,1,0}:Vec3{1,0,0}).unit(),other=normal.cross(axis);
            std::sort(polygon.begin(),polygon.end(),[&](int a,int b){auto pa=mesh.vertices[a].position-center,pb=mesh.vertices[b].position-center;return std::atan2(pa.dot(other),pa.dot(axis))<std::atan2(pb.dot(other),pb.dot(axis));});
            for(size_t i=1;i+1<polygon.size();++i){auto a=mesh.vertices[polygon[0]].position,b=mesh.vertices[polygon[i]].position,c=mesh.vertices[polygon[i+1]].position;if((b-a).cross(c-a).length()>1e-10)mesh.indices.insert(mesh.indices.end(),{polygon[0],polygon[i],polygon[i+1]});}
        }
    }
    refine_terrain_surface(mesh,terrain,surfaceVertex);
    return mesh;
}
struct TerrainVolumeCache {
    std::optional<TerrainData> terrain;
    std::array<Mesh,169> chunks;
    Mesh mesh;
    size_t rebuiltChunks=0;
    Mesh get(const TerrainData& next){
        next.validate();rebuiltChunks=0;
        bool unchangedBase=terrain && terrain->Heights==next.Heights && terrain->Paint==next.Paint;
        if(unchangedBase && terrain->Volumes==next.Volumes)return mesh;
        size_t common=0;
        if(unchangedBase)while(common<std::min(terrain->Volumes.size(),next.Volumes.size()) && terrain->Volumes[common]==next.Volumes[common])++common;
        auto touches=[&](const TerrainVolume& edit,int x,int z){double r=edit.Radius+1.;double loX=-12.125+x*2,loZ=-12.125+z*2,hiX=std::min(12.125,loX+2),hiZ=std::min(12.125,loZ+2);return edit.X+r>=loX && edit.X-r<=hiX && edit.Z+r>=loZ && edit.Z-r<=hiZ;};
        std::array<std::optional<Mesh>,169> replacements;
        for(int z=0;z<13;++z)for(int x=0;x<13;++x){
            bool dirty=!unchangedBase;
            if(unchangedBase){for(size_t i=common;i<terrain->Volumes.size();++i)dirty=dirty||touches(terrain->Volumes[i],x,z);for(size_t i=common;i<next.Volumes.size();++i)dirty=dirty||touches(next.Volumes[i],x,z);}
            if(dirty){replacements[z*13+x]=build_terrain_volume_geometry(next,x*8,std::min(97,x*8+8),z*8,std::min(97,z*8+8));++rebuiltChunks;}
        }
        struct Position {int64_t x,y,z;bool operator==(const Position&)const=default;};
        struct Hash {size_t operator()(const Position& p)const{return size_t(uint64_t(p.x)*73856093ull)^size_t(uint64_t(p.y)*19349663ull)^size_t(uint64_t(p.z)*83492791ull);}};
        std::unordered_map<Position,int,Hash> vertices;
        Mesh assembled;
        for(size_t tile=0;tile<chunks.size();++tile){
            const auto& chunk=replacements[tile]?*replacements[tile]:chunks[tile];
            std::vector<int> remap;remap.reserve(chunk.vertices.size());
            for(const auto& v:chunk.vertices){Position p{std::llround(v.position.x*1e7),std::llround(v.position.y*1e7),std::llround(v.position.z*1e7)};auto [at,added]=vertices.emplace(p,int(assembled.vertices.size()));if(added)assembled.vertices.push_back(v);remap.push_back(at->second);}
            for(size_t i=0;i<chunk.indices.size();i+=3){int a=remap[chunk.indices[i]],b=remap[chunk.indices[i+1]],c=remap[chunk.indices[i+2]];if(a!=b && b!=c && c!=a)assembled.indices.insert(assembled.indices.end(),{a,b,c});}
        }
        auto savedTerrain=next;
        for(size_t tile=0;tile<chunks.size();++tile)if(replacements[tile])chunks[tile]=std::move(*replacements[tile]);
        terrain=std::move(savedTerrain);mesh=std::move(assembled);return mesh;
    }
};
inline Mesh terrain_volume_geometry(const TerrainData& terrain){
    static TerrainVolumeCache cache;static std::mutex mutex;
    std::lock_guard guard(mutex);return cache.get(terrain);
}
}
