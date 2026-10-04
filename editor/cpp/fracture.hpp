#pragma once
#include "geometry.hpp"
#include <memory>
#include <map>
#include <tuple>
#include <set>
#include <unordered_map>
#include <bit>
namespace starlight::editor {
struct FracturePiece {std::shared_ptr<const MeshPart> part;Vec3 center;double radius=0;std::shared_ptr<const void> hull;double hullDensity=0;}; // hull: optional prebuilt physics hull of part, valid for hullDensity
inline bool closed_convex_mesh(const Mesh& mesh){
    if(mesh.vertices.size()<4||mesh.vertices.size()>80000||mesh.indices.size()<12||mesh.indices.size()>240000)return false;
    mesh.validate();Vec3 center{};for(const auto& v:mesh.vertices)center=center+v.position;center=center/double(mesh.vertices.size());
    using Key=std::tuple<int64_t,int64_t,int64_t>;std::map<Key,int> welded;std::vector<int> ids;
    for(const auto& v:mesh.vertices){auto p=v.position;if(std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>1e9)return false;Key key{std::llround(p.x*1e7),std::llround(p.y*1e7),std::llround(p.z*1e7)};auto [it,added]=welded.emplace(key,int(welded.size()));ids.push_back(it->second);}
    std::map<std::pair<int,int>,int> edges;std::set<std::array<int64_t,4>> planes;double volume=0;
    for(size_t i=0;i<mesh.indices.size();i+=3){int indices[]={mesh.indices[i],mesh.indices[i+1],mesh.indices[i+2]};auto a=mesh.vertices[indices[0]].position,b=mesh.vertices[indices[1]].position,c=mesh.vertices[indices[2]].position;auto normal=(b-a).cross(c-a);if(normal.length()<1e-10)continue;
        if(normal.dot((a+b+c)/3.-center)<0)normal=normal*-1;volume+=std::abs((a-center).dot((b-center).cross(c-center)))/6.;normal=normal.unit();
        std::array<int64_t,4> plane{std::llround(normal.x*1e6),std::llround(normal.y*1e6),std::llround(normal.z*1e6),std::llround(normal.dot(a)*1e6)};
        if(planes.insert(plane).second){if(planes.size()*mesh.vertices.size()>100000000)return false;for(const auto& v:mesh.vertices)if(normal.dot(v.position-a)>2e-6)return false;}
        for(int j=0;j<3;++j){int x=ids[indices[j]],y=ids[indices[(j+1)%3]];if(x>y)std::swap(x,y);++edges[{x,y}];}
    }
    return volume>1e-9&&!edges.empty()&&std::all_of(edges.begin(),edges.end(),[](const auto& e){return e.second==2;});
}
// Plane cuts with closed caps for convex source meshes. Concave fracture and
// multi-material interior authoring require a separate decomposition stage.
inline Mesh fracture_half(const Mesh& source,int axis,double plane,bool lower){
    Mesh out;std::vector<Vec3> rim;
    auto component=[&](Vec3 p){return axis==0?p.x:axis==1?p.y:p.z;};
    auto emit=[&](Vertex a,Vertex b,Vertex c){if((b.position-a.position).cross(c.position-a.position).length()<1e-10)return;int n=int(out.vertices.size());out.vertices.insert(out.vertices.end(),{a,b,c});out.indices.insert(out.indices.end(),{n,n+1,n+2});};
    for(size_t i=0;i<source.indices.size();i+=3){
        std::array<Vertex,3> triangle{source.vertices[source.indices[i]],source.vertices[source.indices[i+1]],source.vertices[source.indices[i+2]]};
        std::vector<Vertex> clipped;clipped.reserve(4);
        for(int j=0;j<3;++j){auto a=triangle[j],b=triangle[(j+1)%3];double da=component(a.position)-plane,db=component(b.position)-plane;bool inside=lower?da<=0:da>=0,next=lower?db<=0:db>=0;
            if(inside)clipped.push_back(a);
            if(inside!=next){double t=da/(da-db);Vertex v{a.position+(b.position-a.position)*t,a.normal+(b.normal-a.normal)*t,a.color+(b.color-a.color)*t,a.kind};clipped.push_back(v);if(std::none_of(rim.begin(),rim.end(),[&](Vec3 p){return (p-v.position).length()<1e-7;}))rim.push_back(v.position);}
        }
        for(size_t j=1;j+1<clipped.size();++j)emit(clipped[0],clipped[j],clipped[j+1]);
    }
    if(rim.size()>=3){
        Vec3 center{};for(auto p:rim)center=center+p;center=center/double(rim.size());
        Vec3 normal=axis==0?Vec3{1,0,0}:axis==1?Vec3{0,1,0}:Vec3{0,0,1};if(!lower)normal=normal*-1;
        Vec3 u=axis==0?Vec3{0,1,0}:Vec3{1,0,0},v=normal.cross(u);
        std::sort(rim.begin(),rim.end(),[&](Vec3 a,Vec3 b){a=a-center;b=b-center;return std::atan2(a.dot(v),a.dot(u))<std::atan2(b.dot(v),b.dot(u));});
        auto vertex=[&](Vec3 p){return Vertex{p,normal,{1,1,1},source.vertices.empty()?0:source.vertices.front().kind};};
        for(size_t j=0;j<rim.size();++j)emit(vertex(center),vertex(rim[j]),vertex(rim[(j+1)%rim.size()]));
    }
    if(!out.indices.empty())out.normals();return out;
}
inline std::vector<FracturePiece> fracture_convex(Mesh world,Vec3 color,bool emissive=false){
    // Authored primitives may use inward winding; cuts require one consistent
    // outward orientation to retain a closed volume through every split.
    Vec3 centroid{};for(const auto& v:world.vertices)centroid=centroid+v.position;
    if(world.vertices.empty())return {};centroid=centroid/double(world.vertices.size());
    for(size_t i=0;i<world.indices.size();i+=3){auto a=world.vertices[world.indices[i]].position,b=world.vertices[world.indices[i+1]].position,c=world.vertices[world.indices[i+2]].position;if((b-a).cross(c-a).dot((a+b+c)/3.-centroid)<0)std::swap(world.indices[i+1],world.indices[i+2]);}
    std::vector<Mesh> pieces;pieces.push_back(std::move(world));
    for(int cut=0;cut<4;++cut){std::vector<Mesh> next;int axis=cut%3;
        for(const auto& piece:pieces){double low=INFINITY,high=-INFINITY;for(const auto& v:piece.vertices){double p=axis==0?v.position.x:axis==1?v.position.y:v.position.z;low=std::min(low,p);high=std::max(high,p);}double plane=(low+high)*.5;
            for(bool lower:{true,false}){auto half=fracture_half(piece,axis,plane,lower);if(!half.indices.empty())next.push_back(std::move(half));}
        }pieces=std::move(next);
    }
    std::vector<FracturePiece> out;
    for(auto& mesh:pieces){Vec3 low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};for(const auto& v:mesh.vertices){auto p=v.position;low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}
        auto center=(low+high)*.5;double radius=0;for(auto& v:mesh.vertices){v.position=v.position-center;radius=std::max(radius,v.position.length());}
        auto part=std::make_shared<MeshPart>();part->mesh=std::move(mesh);part->color=color;part->emissive=emissive;out.push_back({std::move(part),center,radius});
    }return out;
}
// Exact tetrahedral decomposition for closed meshes with a nonempty visibility
// kernel. This includes concave L/cross-shaped solids; non-star-shaped solids
// require a more general decomposition and are deliberately rejected here.
inline std::vector<FracturePiece> fracture_star_shaped(Mesh mesh,Vec3 color,bool emissive=false){
    if(mesh.indices.size()<12||mesh.indices.size()>384)return {};mesh.validate();
    using Key=std::tuple<int64_t,int64_t,int64_t>;std::map<Key,int> welded;std::vector<int> ids;Vec3 center{};
    for(const auto& v:mesh.vertices){auto p=v.position;if(std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>1e9)return {};auto [it,added]=welded.emplace(Key{std::llround(p.x*1e7),std::llround(p.y*1e7),std::llround(p.z*1e7)},int(welded.size()));ids.push_back(it->second);center=center+p;}
    center=center/double(mesh.vertices.size());struct Edge{int triangle,direction;};std::map<std::pair<int,int>,std::vector<Edge>> edges;
    const int count=int(mesh.indices.size()/3);for(int t=0;t<count;++t)for(int j=0;j<3;++j){int a=ids[mesh.indices[t*3+j]],b=ids[mesh.indices[t*3+(j+1)%3]];if(a==b)return {};int direction=a<b?1:-1;if(a>b)std::swap(a,b);edges[{a,b}].push_back({t,direction});}
    std::vector<std::vector<std::pair<int,int>>> neighbors(count);for(const auto& [edge,faces]:edges){if(faces.size()!=2)return {};int relation=-faces[0].direction*faces[1].direction;neighbors[faces[0].triangle].push_back({faces[1].triangle,relation});neighbors[faces[1].triangle].push_back({faces[0].triangle,relation});}
    std::vector<int> orientation(count);orientation[0]=1;std::vector<int> queue{0};for(size_t q=0;q<queue.size();++q)for(auto [next,relation]:neighbors[queue[q]]){int sign=orientation[queue[q]]*relation;if(!orientation[next]){orientation[next]=sign;queue.push_back(next);}else if(orientation[next]!=sign)return {};}
    if(queue.size()!=size_t(count))return {};double volume=0;for(int t=0;t<count;++t){if(orientation[t]<0)std::swap(mesh.indices[t*3+1],mesh.indices[t*3+2]);auto a=mesh.vertices[mesh.indices[t*3]].position-center,b=mesh.vertices[mesh.indices[t*3+1]].position-center,c=mesh.vertices[mesh.indices[t*3+2]].position-center;volume+=a.dot(b.cross(c))/6.;}
    if(std::abs(volume)<1e-9)return {};if(volume<0)for(int t=0;t<count;++t)std::swap(mesh.indices[t*3+1],mesh.indices[t*3+2]);
    struct Plane{Vec3 point,normal;};std::vector<Plane> planes;for(int t=0;t<count;++t){auto a=mesh.vertices[mesh.indices[t*3]].position,b=mesh.vertices[mesh.indices[t*3+1]].position,c=mesh.vertices[mesh.indices[t*3+2]].position;auto n=(b-a).cross(c-a);if(n.length()<1e-10)return {};planes.push_back({a,n.unit()});}
    Vec3 low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};for(const auto& v:mesh.vertices){auto p=v.position;low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}double inset=std::min({high.x-low.x,high.y-low.y,high.z-low.z})*.02;
    for(int iteration=0;iteration<256;++iteration){double worst=0;for(const auto& plane:planes){double d=plane.normal.dot(center-plane.point)+inset;if(d>0){center=center-plane.normal*d;worst=std::max(worst,d);}}if(worst<1e-8)break;}
    for(const auto& plane:planes)if(plane.normal.dot(center-plane.point)>1e-7)return {};
    std::vector<FracturePiece> result;
    for(int t=0;t<count;++t){auto a=mesh.vertices[mesh.indices[t*3]],b=mesh.vertices[mesh.indices[t*3+1]],c=mesh.vertices[mesh.indices[t*3+2]];if(std::abs((a.position-center).dot((b.position-center).cross(c.position-center)))<1e-9)continue;
        Vertex inner{center,{},(a.color+b.color+c.color)/3.,a.kind};Mesh piece;piece.vertices={a,b,c,inner};piece.indices={0,1,2,0,3,1,1,3,2,2,3,0};Vec3 midpoint=(a.position+b.position+c.position+center)/4.;double radius=0;for(auto& v:piece.vertices){v.position=v.position-midpoint;radius=std::max(radius,v.position.length());}piece.normals();auto part=std::make_shared<MeshPart>();part->mesh=std::move(piece);part->color=color;part->emissive=emissive;result.push_back({std::move(part),midpoint,radius});
    }return result;
}
// Concave plane caps are traced as boundary loops, not angle-sorted around a
// centroid. Ear clipping retains reentrant corners and disconnected cut islands.
inline std::optional<Mesh> fracture_concave_half(const Mesh& source,int axis,double plane,bool lower,float capKind=-1,bool preserveNormals=false,std::string* failure=nullptr){
    auto fail=[&](const char* reason)->std::optional<Mesh>{if(failure)*failure=reason;return {};};
    Mesh out;std::vector<Vec3> nodes;std::vector<std::pair<int,int>> edges; // ascending, like the set it replaces
    auto component=[&](Vec3 p){return axis==0?p.x:axis==1?p.y:p.z;};
    // Candidate index only: the original distance predicate and earliest ID remain authoritative.
    using NodeKey=std::array<int64_t,3>;
    struct NodeHash {size_t operator()(const NodeKey& k)const noexcept {size_t h=0;for(auto v:k)h^=std::hash<int64_t>{}(v)+size_t(0x9e3779b97f4a7c15ULL)+(h<<6)+(h>>2);return h;}};
    std::unordered_map<NodeKey,std::vector<int>,NodeHash> nodeBuckets;std::vector<int> unindexedNodes;
    auto nodeKey=[](Vec3 p,NodeKey& k){double q[]={p.x/4e-7,p.y/4e-7,p.z/4e-7};for(int a=0;a<3;++a){if(!std::isfinite(q[a])||std::abs(q[a])>1099511627776.)return false;k[a]=int64_t(std::floor(q[a]));}return true;};
    using ExactNodeKey=std::array<uint64_t,3>;struct ExactNodeHash{size_t operator()(const ExactNodeKey& k)const noexcept{size_t h=0;for(auto v:k)h^=std::hash<uint64_t>{}(v)+size_t(0x9e3779b97f4a7c15ULL)+(h<<6)+(h>>2);return h;}};
    std::unordered_map<ExactNodeKey,int,ExactNodeHash> exactNodeMemo;
    auto node=[&](Vec3 p){bool memo=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);ExactNodeKey exact{std::bit_cast<uint64_t>(p.x),std::bit_cast<uint64_t>(p.y),std::bit_cast<uint64_t>(p.z)};if(memo){auto found=exactNodeMemo.find(exact);if(found!=exactNodeMemo.end())return found->second;}NodeKey key{};bool indexed=nodeKey(p,key);int match=-1;
        auto consider=[&](int i){if((match<0||i<match)&&(nodes[i]-p).length()<1e-7)match=i;};
        if(indexed){
            // A match lies within 1e-7 (a quarter of a 4e-7 cell), so neighbouring cells only matter near the matching cell edge.
            int lowest[3],highest[3];const double q[]={p.x/4e-7,p.y/4e-7,p.z/4e-7};
            for(int a=0;a<3;++a){double fraction=q[a]-double(key[a]);lowest[a]=fraction<.3?-1:0;highest[a]=fraction>.7?1:0;}
            for(int x=lowest[0];x<=highest[0];++x)for(int y=lowest[1];y<=highest[1];++y)for(int z=lowest[2];z<=highest[2];++z){auto found=nodeBuckets.find(NodeKey{key[0]+x,key[1]+y,key[2]+z});if(found!=nodeBuckets.end())for(int i:found->second)consider(i);}
            for(int i:unindexedNodes)consider(i);}
        else for(size_t i=0;i<nodes.size();++i)consider(int(i));
        if(match>=0){if(memo)exactNodeMemo.emplace(exact,match);return match;}int id=int(nodes.size());nodes.push_back(p);if(indexed)nodeBuckets[key].push_back(id);else unindexedNodes.push_back(id);if(memo)exactNodeMemo.emplace(exact,id);return id;
    };
    // Small valid facets remain part of the closed shell. Dropping them here
    // opens a previously valid solid on the next cut of detailed rock faces.
    auto emit=[&](Vertex a,Vertex b,Vertex c){if((b.position-a.position).cross(c.position-a.position).length()<1e-16)return;int n=int(out.vertices.size());out.vertices.insert(out.vertices.end(),{a,b,c});out.indices.insert(out.indices.end(),{n,n+1,n+2});};
    out.vertices.reserve(source.indices.size()+64);out.indices.reserve(source.indices.size()+64);
    for(size_t t=0;t<source.indices.size();t+=3){std::array<Vertex,4> clipped;size_t clippedCount=0;bool positive=false,negative=false;
        for(int j=0;j<3;++j){auto a=source.vertices[source.indices[t+j]],b=source.vertices[source.indices[t+(j+1)%3]];double da=component(a.position)-plane,db=component(b.position)-plane;positive|=da>1e-8;negative|=da< -1e-8;
            bool in=lower?da<=0:da>=0,next=lower?db<=0:db>=0;if(in)clipped[clippedCount++]=a;
            if(in!=next){double fraction=da/(da-db);Vertex v{a.position+(b.position-a.position)*fraction,a.normal+(b.normal-a.normal)*fraction,a.color+(b.color-a.color)*fraction,a.kind};clipped[clippedCount++]=v;}
        }
        if(!positive&&!negative){auto a=source.vertices[source.indices[t]].position,b=source.vertices[source.indices[t+1]].position,c=source.vertices[source.indices[t+2]].position;auto n=(b-a).cross(c-a);if(component(n)*(lower?1:-1)<=0)continue;}
        for(size_t j=1;j+1<clippedCount;++j)emit(clipped[0],clipped[j],clipped[j+1]);
    }
    std::vector<std::pair<int,int>> boundary;
    for(size_t t=0;t<out.indices.size();t+=3)for(int j=0;j<3;++j){auto a=out.vertices[out.indices[t+j]].position,b=out.vertices[out.indices[t+(j+1)%3]].position;if(std::abs(component(a)-plane)<1e-7&&std::abs(component(b)-plane)<1e-7){int x=node(a),y=node(b);if(x==y)continue;if(x>y)std::swap(x,y);boundary.emplace_back(x,y);}}
    std::sort(boundary.begin(),boundary.end());
    for(size_t first=0;first<boundary.size();){size_t last=first;while(last<boundary.size()&&boundary[last]==boundary[first])++last;const size_t count=last-first;if(count==1)edges.push_back(boundary[first]);else if(count!=2)return fail("Cap edge multiplicity");first=last;}
    if(edges.empty())return out;
    std::vector<std::vector<int>> neighbors(nodes.size());for(auto [a,b]:edges){neighbors[a].push_back(b);neighbors[b].push_back(a);}for(const auto& row:neighbors)if(!row.empty()&&row.size()!=2)return fail("Cap boundary degree");
    Vec3 normal=axis==0?Vec3{1,0,0}:axis==1?Vec3{0,1,0}:Vec3{0,0,1};if(!lower)normal=normal*-1;
    std::vector<bool> seen(nodes.size());std::vector<std::vector<int>> loops;
    for(size_t first=0;first<nodes.size();++first)if(!seen[first]&&!neighbors[first].empty()){std::vector<int> loop;int previous=-1,current=int(first);do{if(seen[current])return fail("Repeated cap loop node");seen[current]=true;loop.push_back(current);int next=neighbors[current][0]==previous?neighbors[current][1]:neighbors[current][0];previous=current;current=next;}while(current!=int(first));if(loop.size()<3)return fail("Short cap loop");loops.push_back(std::move(loop));}
    // Nested contours need a hole-aware cap; reject rather than filling a cavity.
    auto u=normal.cross({0,1,0});if(u.length()<.1)u=normal.cross({1,0,0});u=u.unit();auto v=normal.cross(u);
    auto contains=[&](Vec3 p,const std::vector<int>& polygon){bool inside=false;double x=p.dot(u),y=p.dot(v);for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){auto a=nodes[polygon[i]],b=nodes[polygon[j]];double ax=a.dot(u),ay=a.dot(v),bx=b.dot(u),by=b.dot(v);if((ay>y)!=(by>y)&&x<(bx-ax)*(y-ay)/(by-ay)+ax)inside=!inside;}return inside;};
    for(size_t a=0;a<loops.size();++a)for(size_t b=0;b<loops.size();++b)if(a!=b&&contains(nodes[loops[a][0]],loops[b]))return fail("Nested cap loops");
    auto vertex=[&](int i){return Vertex{nodes[i],normal,{1,1,1},capKind<0?source.vertices.front().kind:capKind};};
    // Conservative broad phase; exact original half-plane predicates remain below.
    auto earX=[&](Vec3 p){return axis==0?p.y:p.x;};auto earY=[&](Vec3 p){return axis==2?p.y:p.z;};
    double earMax=1;for(auto p:nodes)earMax=std::max({earMax,std::abs(earX(p)),std::abs(earY(p))});
    const double earRound=128*std::numeric_limits<double>::epsilon()*earMax*earMax;
    const double earPad=128*std::numeric_limits<double>::epsilon()*earMax;
    for(auto loop:loops){double area=0;for(size_t i=0;i<loop.size();++i)area+=nodes[loop[i]].cross(nodes[loop[(i+1)%loop.size()]]).dot(normal);if(area<0)std::reverse(loop.begin(),loop.end());
        struct EarSweepNode{double x;int id;};std::vector<EarSweepNode> earSweep;std::vector<uint8_t> earAlive;
        bool earSweepEnabled=loop.size()>=64;
        if(earSweepEnabled)for(int id:loop)if(!std::isfinite(earX(nodes[id]))||!std::isfinite(earY(nodes[id]))){earSweepEnabled=false;break;}
        if(earSweepEnabled){earSweep.reserve(loop.size());earAlive.resize(nodes.size());for(int id:loop){earSweep.push_back({earX(nodes[id]),id});earAlive[id]=1;}std::sort(earSweep.begin(),earSweep.end(),[](const EarSweepNode&a,const EarSweepNode&b){return a.x<b.x||(a.x==b.x&&a.id<b.id);});}
        while(loop.size()>3){bool clipped=false;for(size_t i=0;i<loop.size();++i){size_t prior=(i+loop.size()-1)%loop.size(),next=(i+1)%loop.size();auto a=nodes[loop[prior]],b=nodes[loop[i]],c=nodes[loop[next]];double turn=(b-a).cross(c-b).dot(normal);if(turn<1e-14)continue;bool occupied=false;
            const double earArea=turn-earRound;bool earFilter=std::isfinite(earArea)&&earArea>0;
            double earLoX=0,earHiX=0,earLoY=0,earHiY=0;
            if(earFilter){double weight=(1e-14+earRound)/earArea;auto bounds=[&](double x,double y,double z){double lo=std::min({x,y,z}),hi=std::max({x,y,z});return std::pair<double,double>{lo-weight*((x-lo)+(y-lo)+(z-lo))-earPad,hi+weight*((hi-x)+(hi-y)+(hi-z))+earPad};};auto x=bounds(earX(a),earX(b),earX(c)),y=bounds(earY(a),earY(b),earY(c));earLoX=x.first;earHiX=x.second;earLoY=y.first;earHiY=y.second;}

            if(earSweepEnabled&&earFilter&&std::isfinite(earLoX)&&std::isfinite(earHiX)&&std::isfinite(earLoY)&&std::isfinite(earHiY)&&earLoX<=earHiX&&earLoY<=earHiY){
                auto begin=std::lower_bound(earSweep.begin(),earSweep.end(),earLoX,[](const EarSweepNode&node,double value){return node.x<value;});
                auto end=std::upper_bound(begin,earSweep.end(),earHiX,[](double value,const EarSweepNode&node){return value<node.x;});
                for(auto query=begin;query!=end;++query){int id=query->id;if(!earAlive[id]||id==loop[prior]||id==loop[i]||id==loop[next])continue;auto p=nodes[id];if(earFilter&&(earX(p)<earLoX||earX(p)>earHiX||earY(p)<earLoY||earY(p)>earHiY))continue;if((b-a).cross(p-a).dot(normal)>=-1e-14&&(c-b).cross(p-b).dot(normal)>=-1e-14&&(a-c).cross(p-c).dot(normal)>=-1e-14){occupied=true;break;}}
            }else{
            for(size_t j=0;j<loop.size();++j)if(j!=prior&&j!=i&&j!=next){auto p=nodes[loop[j]];if(earFilter&&(earX(p)<earLoX||earX(p)>earHiX||earY(p)<earLoY||earY(p)>earHiY))continue;if((b-a).cross(p-a).dot(normal)>=-1e-14&&(c-b).cross(p-b).dot(normal)>=-1e-14&&(a-c).cross(p-c).dot(normal)>=-1e-14){occupied=true;break;}}
            }
            if(!occupied){emit(vertex(loop[prior]),vertex(loop[i]),vertex(loop[next]));if(earSweepEnabled)earAlive[loop[i]]=0;loop.erase(loop.begin()+i);clipped=true;break;}
        }if(!clipped)return fail("Cap ear clipping failed");}
        emit(vertex(loop[0]),vertex(loop[1]),vertex(loop[2]));
    }
    // Boundary tracing merges very close intersections. Make the retained
    // surface use those exact same nodes as its cap, rather than leaving a
    // microscopic seam which appears after repeated cuts of detailed faces.
    for(auto& vert:out.vertices)if(std::abs(component(vert.position)-plane)<1e-7){int match=node(vert.position);vert.position=nodes[match];}
    if(!out.indices.empty()){if(!preserveNormals)out.normals();else for(auto& v:out.vertices)v.normal=v.normal.unit();}
    return out;
}
inline bool orient_fracture_mesh(Mesh& mesh){ 
    if(mesh.indices.empty())return false;mesh.validate();using Key=std::tuple<int64_t,int64_t,int64_t>;struct WeldHash {size_t operator()(const Key& key)const noexcept {size_t h=0;for(auto v:{std::get<0>(key),std::get<1>(key),std::get<2>(key)})h^=std::hash<int64_t>{}(v)+size_t(0x9e3779b97f4a7c15ULL)+(h<<6)+(h>>2);return h;}};std::unordered_map<Key,int,WeldHash> welded;welded.reserve(mesh.vertices.size());std::vector<int> ids;ids.reserve(mesh.vertices.size());
    for(const auto& v:mesh.vertices){auto p=v.position;if(std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>1e9)return false;auto [it,added]=welded.emplace(Key{std::llround(p.x*1e7),std::llround(p.y*1e7),std::llround(p.z*1e7)},int(welded.size()));ids.push_back(it->second);}
    struct EdgeHash {size_t operator()(const std::pair<int,int>& edge)const noexcept{return size_t(uint64_t(uint32_t(edge.first))<<32|uint32_t(edge.second));}};struct EdgeFaces{std::array<std::pair<int,int>,2> faces{};size_t count=0;};std::unordered_map<std::pair<int,int>,EdgeFaces,EdgeHash> edges;edges.reserve(mesh.indices.size());int count=int(mesh.indices.size()/3);for(int t=0;t<count;++t)for(int j=0;j<3;++j){int a=ids[mesh.indices[t*3+j]],b=ids[mesh.indices[t*3+(j+1)%3]];if(a==b)return false;int sign=a<b?1:-1;if(a>b)std::swap(a,b);auto& faces=edges[{a,b}];if(faces.count<2)faces.faces[faces.count]={t,sign};++faces.count;}
    std::vector<std::pair<int,int>> orderedEdges;orderedEdges.reserve(edges.size());for(const auto& entry:edges)orderedEdges.push_back(entry.first);std::sort(orderedEdges.begin(),orderedEdges.end());
    struct NeighborRow{std::array<std::pair<int,int>,3> values{};size_t count=0;void push_back(std::pair<int,int> value){values[count++]=value;}auto begin()const{return values.begin();}auto end()const{return values.begin()+count;}};std::vector<NeighborRow> neighbors(count);for(const auto& edge:orderedEdges){const auto& entry=edges.find(edge)->second;if(entry.count!=2)return false;const auto& faces=entry.faces;int sign=-faces[0].second*faces[1].second;neighbors[faces[0].first].push_back({faces[1].first,sign});neighbors[faces[1].first].push_back({faces[0].first,sign});}
    std::vector<int> orientations(count);for(int first=0;first<count;++first)if(!orientations[first]){orientations[first]=1;std::vector<int> queue;queue.reserve(count);queue.push_back(first);for(size_t i=0;i<queue.size();++i)for(auto [next,sign]:neighbors[queue[i]]){int value=orientations[queue[i]]*sign;if(!orientations[next]){orientations[next]=value;queue.push_back(next);}else if(orientations[next]!=value)return false;}
        double volume=0;auto origin=mesh.vertices[mesh.indices[first*3]].position;for(int t:queue){if(orientations[t]<0)std::swap(mesh.indices[t*3+1],mesh.indices[t*3+2]);auto a=mesh.vertices[mesh.indices[t*3]].position-origin,b=mesh.vertices[mesh.indices[t*3+1]].position-origin,c=mesh.vertices[mesh.indices[t*3+2]].position-origin;volume+=a.dot(b.cross(c))/6.;}if(std::abs(volume)<1e-10)return false;if(volume<0)for(int t:queue)std::swap(mesh.indices[t*3+1],mesh.indices[t*3+2]);
    }return true;
}
inline std::vector<FracturePiece> fracture_concave(Mesh mesh,Vec3 color,bool emissive){
    if(mesh.indices.size()>24000||!orient_fracture_mesh(mesh))return {};std::vector<Mesh> pending{std::move(mesh)},solids;
    for(int depth=0;depth<8&&!pending.empty();++depth){std::vector<Mesh> next;for(auto& piece:pending){if(closed_convex_mesh(piece)){solids.push_back(std::move(piece));continue;}
        Vec3 low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};for(const auto& v:piece.vertices){auto p=v.position;low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}double extents[]={high.x-low.x,high.y-low.y,high.z-low.z},lows[]={low.x,low.y,low.z};std::array<int,3> axes{0,1,2};std::sort(axes.begin(),axes.end(),[&](int a,int b){return extents[a]>extents[b];});bool split=false;
        for(int axis:axes){double plane=lows[axis]+extents[axis]*.503;double best=INFINITY;for(const auto& vertex:piece.vertices){auto p=vertex.position;double value=axis==0?p.x:axis==1?p.y:p.z;if(value<=lows[axis]+extents[axis]*.05||value>=lows[axis]+extents[axis]*.95)continue;double distance=std::abs(value-lows[axis]-extents[axis]*.5);if(distance<best){best=distance;plane=value;}}auto a=fracture_concave_half(piece,axis,plane,true),b=fracture_concave_half(piece,axis,plane,false);if(a&&b&&!a->indices.empty()&&!b->indices.empty()){next.push_back(std::move(*a));next.push_back(std::move(*b));split=true;break;}}
        if(!split||next.size()+solids.size()>128)return {};
    }pending=std::move(next);}
    if(!pending.empty()||solids.empty())return {};std::vector<FracturePiece> result;
    for(auto& mesh:solids){Vec3 center{};for(const auto& v:mesh.vertices)center=center+v.position;center=center/double(mesh.vertices.size());double radius=0;for(auto& v:mesh.vertices){v.position=v.position-center;radius=std::max(radius,v.position.length());}auto part=std::make_shared<MeshPart>();part->mesh=std::move(mesh);part->color=color;part->emissive=emissive;result.push_back({part,center,radius});}return result;
}
// Surface shells are the physical fallback for open panels and hollow meshes
// whose nested caps cannot use solid splitting. Preserve their source faces
// with explicit thickness; spatial grouping bounds the collision body count.
inline std::vector<FracturePiece> fracture_surface(const Mesh& source,Vec3 color,bool emissive){
    if(source.indices.empty()||source.indices.size()>24000)return {};source.validate();using Key=std::tuple<int64_t,int64_t,int64_t>;std::map<Key,int> welded;std::vector<int> ids;Vec3 low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};
    for(const auto& v:source.vertices){auto p=v.position;if(std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)})>1e9)return {};auto [it,added]=welded.emplace(Key{std::llround(p.x*1e7),std::llround(p.y*1e7),std::llround(p.z*1e7)},int(welded.size()));ids.push_back(it->second);low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}
    std::map<std::pair<int,int>,int> edges;for(size_t i=0;i<source.indices.size();i+=3)for(int j=0;j<3;++j){int a=ids[source.indices[i+j]],b=ids[source.indices[i+(j+1)%3]];if(a==b)return {};if(a>b)std::swap(a,b);if(++edges[{a,b}]>2)return {};}
    auto extent=high-low;double thickness=std::clamp(std::max({extent.x,extent.y,extent.z})*.005,.005,.02);std::map<int,Mesh> groups;
    for(size_t i=0;i<source.indices.size();i+=3){auto a=source.vertices[source.indices[i]],b=source.vertices[source.indices[i+1]],c=source.vertices[source.indices[i+2]];auto n=(b.position-a.position).cross(c.position-a.position);if(n.length()<1e-10)continue;n=n.unit();auto center=(a.position+b.position+c.position)/3.;int x=std::clamp(int((center.x-low.x)/std::max(extent.x,1e-9)*4),0,3),y=std::clamp(int((center.y-low.y)/std::max(extent.y,1e-9)*4),0,3),z=std::clamp(int((center.z-low.z)/std::max(extent.z,1e-9)*4),0,3);auto& mesh=groups[x+y*4+z*16];int base=int(mesh.vertices.size());
        for(double side:{-1.,1.})for(auto v:{a,b,c}){v.position=v.position+n*(side*thickness*.5);mesh.vertices.push_back(v);}for(int index:{0,2,1,3,4,5,0,1,4,0,4,3,1,2,5,1,5,4,2,0,3,2,3,5})mesh.indices.push_back(base+index);
    }
    std::vector<FracturePiece> result;for(auto& [key,mesh]:groups){Vec3 center{};for(const auto& v:mesh.vertices)center=center+v.position;center=center/double(mesh.vertices.size());double radius=0;for(auto& v:mesh.vertices){v.position=v.position-center;radius=std::max(radius,v.position.length());}mesh.normals();auto part=std::make_shared<MeshPart>();part->mesh=std::move(mesh);part->color=color;part->emissive=emissive;result.push_back({part,center,radius});}return result;
}
inline std::vector<FracturePiece> fracture_mesh(Mesh mesh,Vec3 color,bool emissive=false){if(closed_convex_mesh(mesh))return fracture_convex(std::move(mesh),color,emissive);auto star=fracture_star_shaped(mesh,color,emissive);if(!star.empty())return star;auto concave=fracture_concave(mesh,color,emissive);if(!concave.empty())return concave;return fracture_surface(mesh,color,emissive);}

}
