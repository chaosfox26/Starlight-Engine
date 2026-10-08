// Reducing a built-in mesh to a share of its triangles (mesh_reduce.hpp): the right amount, still one closed surface, hardly any change of shape or volume.
#include "mesh_reduce.hpp"
#include "rock_geometry.hpp"
static void mesh_reduce_checks(const auto& require){
    auto edgesAreClosed=[](const Mesh& m){
        std::map<std::pair<int,int>,int> use;
        for(size_t i=0;i<m.indices.size();i+=3)for(int k=0;k<3;++k){int a=m.indices[i+k],b=m.indices[i+(k+1)%3];++use[{std::min(a,b),std::max(a,b)}];}
        for(const auto& [edge,count]:use)if(count!=2)return false;
        return true;
    };
    auto euler=[](const Mesh& m){
        std::set<std::pair<int,int>> edges;std::set<int> used;
        for(size_t i=0;i<m.indices.size();i+=3)for(int k=0;k<3;++k){int a=m.indices[i+k],b=m.indices[i+(k+1)%3];edges.insert({std::min(a,b),std::max(a,b)});used.insert(a);}
        return int(used.size())-int(edges.size())+int(m.indices.size()/3);
    };
    auto volume=[](const Mesh& m){
        double v=0;for(size_t i=0;i<m.indices.size();i+=3){auto a=m.vertices[m.indices[i]].position,b=m.vertices[m.indices[i+1]].position,c=m.vertices[m.indices[i+2]].position;v+=a.dot(b.cross(c))/6;}
        return std::abs(v);
    };
    // Distance from a point to a triangle, to measure how far the reduced surface strays from the original vertices.
    auto pointTriangle=[](Vec3 p,Vec3 a,Vec3 b,Vec3 c){
        Vec3 ab=b-a,ac=c-a,ap=p-a;double d1=ab.dot(ap),d2=ac.dot(ap);if(d1<=0&&d2<=0)return (p-a).length();
        Vec3 bp=p-b;double d3=ab.dot(bp),d4=ac.dot(bp);if(d3>=0&&d4<=d3)return (p-b).length();
        double vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0){double v=d1/(d1-d3);return (p-(a+ab*v)).length();}
        Vec3 cp=p-c;double d5=ab.dot(cp),d6=ac.dot(cp);if(d6>=0&&d5<=d6)return (p-c).length();
        double vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0){double w=d2/(d2-d6);return (p-(a+ac*w)).length();}
        double va=d3*d6-d5*d4;if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0){double w=(d4-d3)/((d4-d3)+(d5-d6));return (p-(b+(c-b)*w)).length();}
        double denom=1/(va+vb+vc),v=vb*denom,w=vc*denom;return (p-(a+ab*v+ac*w)).length();
    };
    struct Form{const char* name;RockForm form;int subdivisions;double height;uint32_t seed;};
    for(const auto& f:std::vector<Form>{{"outcrop rock",RockForm::Outcrop,4,.62,8241},{"boulder",RockForm::Boulder,4,1.05,9173},{"crystal",RockForm::Crystal,4,.85,15317},{"pebble",RockForm::Pebble,3,.62,8241}}){
        const Mesh full=rock_surface(f.height,f.subdivisions,f.seed,f.form);
        for(double keep:{.67,.5}){
            Mesh reduced=reduce_mesh(full,keep);
            const double got=double(reduced.indices.size()/3)/double(full.indices.size()/3);
            const std::string label=std::string(f.name)+" at "+(keep>.6?"67%":"50%");
            static std::deque<std::string> names;
            auto name=[&](const char* tail){names.push_back("Mesh reduction: "+label+" "+tail);return names.back().c_str();};
            require(std::abs(got-keep)<.02,name("keeps the requested share of triangles"));
            require(edgesAreClosed(reduced)&&euler(reduced)==2,name("is still one closed surface without holes or extra handles"));
            double worst=0,size=0;
            for(const auto& v:full.vertices)size=std::max(size,v.position.length());
            for(const auto& v:full.vertices){double best=1e9;for(size_t i=0;i<reduced.indices.size();i+=3)best=std::min(best,pointTriangle(v.position,reduced.vertices[reduced.indices[i]].position,reduced.vertices[reduced.indices[i+1]].position,reduced.vertices[reduced.indices[i+2]].position));worst=std::max(worst,best);}
            require(worst<.012*size,name("never strays more than 1.2% of the rock's size from the original surface"));
            require(std::abs(volume(reduced)-volume(full))<.02*volume(full),name("keeps its volume within 2%"));
        }
    }
    // Small meshes and a keep of 1 are returned as they are.
    Mesh cube=rock_surface(1,1,1);
    require(reduce_mesh(cube,.5).indices.size()==cube.indices.size(),"Mesh reduction: a small mesh is left alone");
    const Mesh rock=rock_surface(.62,4,8241,RockForm::Outcrop);
    require(reduce_mesh(rock,1.0).indices==rock.indices,"Mesh reduction: keeping everything changes nothing");
    require(reduce_mesh(rock,.67).indices==reduce_mesh(rock,.67).indices,"Mesh reduction: the same input always gives the same result");
    // Smooth reduction (reduce_mesh_smooth): the normals are recomputed for a mesh that really lost triangles, and agree with the surface.
    for(const auto& f:std::vector<Form>{{"outcrop rock",RockForm::Outcrop,4,.62,8241},{"boulder",RockForm::Boulder,4,1.05,9173},{"crystal",RockForm::Crystal,4,.85,15317},{"pebble",RockForm::Pebble,3,.62,8241}}){
        const Mesh full=rock_surface(f.height,f.subdivisions,f.seed,f.form);
        for(double keep:{.67,.33}){
            const Mesh smooth=reduce_mesh_smooth(full,keep),plain=reduce_mesh(full,keep);
            const std::string label=std::string("Mesh reduction: ")+f.name+" at "+(keep>.5?"67%":"33%")+" ";
            static std::deque<std::string> names;
            auto name=[&](const char* tail){names.push_back(label+tail);return names.back().c_str();};
            require(smooth.indices==plain.indices&&smooth.vertices.size()==plain.vertices.size(),name("smooth reduction removes exactly the same triangles"));
            std::vector<Vec3> sum(smooth.vertices.size());
            for(size_t i=0;i<smooth.indices.size();i+=3){const auto& a=smooth.vertices[smooth.indices[i]].position;const auto n=(smooth.vertices[smooth.indices[i+1]].position-a).cross(smooth.vertices[smooth.indices[i+2]].position-a);for(int k=0;k<3;++k)sum[smooth.indices[i+k]]=sum[smooth.indices[i+k]]+n;}
            bool unit=true,fromFaces=true,facing=true;
            for(size_t i=0;i<smooth.vertices.size();++i){
                const Vec3 n=smooth.vertices[i].normal;
                unit=unit&&std::abs(n.length()-1)<1e-9;
                fromFaces=fromFaces&&(n-sum[i].unit()).length()<1e-9;
                double best=1e9;Vec3 original{};   // the full mesh's analytic normal at the nearest original vertex: the new normal must point the same way
                for(const auto& v:full.vertices){double d=(v.position-smooth.vertices[i].position).length();if(d<best){best=d;original=v.normal;}}
                facing=facing&&n.dot(original)>.3;
            }
            require(unit,name("normals are unit length"));
            require(fromFaces,name("normals are the sum of the surrounding face normals"));
            require(facing,name("normals point the same way as the original surface (within about 70 degrees, the sharp crystal edges included)"));
        }
    }
    {   // not reduced: the analytic normals are kept as they are
        const Mesh smallRock=rock_surface(1,1,1),sameRock=reduce_mesh_smooth(smallRock,.5);
        bool same=sameRock.vertices.size()==smallRock.vertices.size();
        for(size_t i=0;same&&i<smallRock.vertices.size();++i)same=(sameRock.vertices[i].normal-smallRock.vertices[i].normal).length()==0;
        require(same,"Mesh reduction: a mesh that is not reduced keeps its own normals");
        const Mesh whole=rock_surface(.62,4,8241,RockForm::Outcrop),kept=reduce_mesh_smooth(whole,1.0);
        bool keptNormals=kept.vertices.size()==whole.vertices.size();
        for(size_t i=0;keptNormals&&i<whole.vertices.size();++i)keptNormals=(kept.vertices[i].normal-whole.vertices[i].normal).length()==0;
        require(keptNormals,"Mesh reduction: keeping everything keeps the analytic normals");
    }}
