#pragma once
#include "adventure.hpp"
namespace starlight::editor {
// Cosmetic geometry shares world coordinates. Batch equal materials so every
// moving cone edge does not allocate its own pair of GPU buffers and draw call.
inline std::vector<MeshPart> batch_presentation(std::vector<MeshPart> parts){
    std::vector<MeshPart> batches;
    for(auto& part:parts){
        auto match=std::find_if(batches.begin(),batches.end(),[&](const MeshPart& b){
            return b.emissive==part.emissive && b.color.x==part.color.x && b.color.y==part.color.y && b.color.z==part.color.z;
        });
        if(match==batches.end()){batches.push_back(std::move(part));continue;}
        const int offset=static_cast<int>(match->mesh.vertices.size());
        match->mesh.vertices.insert(match->mesh.vertices.end(),std::make_move_iterator(part.mesh.vertices.begin()),std::make_move_iterator(part.mesh.vertices.end()));
        for(int index:part.mesh.indices)match->mesh.indices.push_back(index+offset);
    }
    return batches;
}
// Temporary geometry only: never inserted into gameplay, collision or saved scenes.
inline std::vector<MeshPart> player_presentation(const AdventurePlayer::Presentation& p){
    std::vector<MeshPart> parts;
    auto line=[&](Vec3 a,Vec3 b,Vec3 color,double width){
        MeshPart part;part.color=color;part.emissive=true;auto direction=(b-a).unit();
        for(auto axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}){auto side=direction.cross(axis);if(side.length()<1e-8)continue;
            side=side.unit()*width;int k=int(part.mesh.vertices.size());
            for(auto at:{a+side,b+side,b-side,a-side})part.mesh.vertices.push_back({at,{0,1,0}});
            part.mesh.indices.insert(part.mesh.indices.end(),{k,k+1,k+2,k,k+2,k+3,k+2,k+1,k,k+3,k+2,k});
        }
        if(!part.mesh.indices.empty())parts.push_back(std::move(part));
    };
    auto frame=[&](const std::array<Vec3,8>& points,Vec3 color){
        for(int i=0;i<4;i++){line(points[i],points[(i+1)%4],color,.018);line(points[i+4],points[(i+1)%4+4],color,.018);line(points[i],points[i+4],color,.018);}
    };
    if(p.showTelegraph){
        auto aim=p.attackAim.unit(),right=aim.cross({0,1,0}).unit();if(right.length()<.5)right={1,0,0};
        auto a=p.attackOrigin+Vec3{0,telegraph_elevation(p.telegraphKind),0};Vec3 previous{};
        const auto color=telegraph_color(p.telegraphKind);
        MeshPart fill;fill.color=color*.4;fill.emissive=true;
        const double radius=p.telegraphRange*std::clamp(p.channelProgress,0.,1.);
        auto surface=[&](double angle,double distance){
            auto point=a+(aim*std::cos(angle)+right*std::sin(angle))*distance;
            return telegraph_surface(p.telegraphTerrain,p.attackOrigin,p.attackAim,point);
        };
        const int rings=std::max(1,int(std::ceil(p.telegraphRange/.5)));
        for(int i=0;i<=24;i++){
            double angle=-p.telegraphHalfAngle+2*p.telegraphHalfAngle*i/24;
            auto endpoint=surface(angle,p.telegraphRange);
            if(i==0 || i==24)for(int j=0;j<rings;j++)line(surface(angle,p.telegraphRange*j/rings),surface(angle,p.telegraphRange*(j+1)/rings),color,.035);
            if(i>0){
                line(previous,endpoint,color,.035);
                double prior=-p.telegraphHalfAngle+2*p.telegraphHalfAngle*(i-1)/24;
                for(int j=0;j<rings;j++){
                    double inner=radius*j/rings,outer=radius*(j+1)/rings;
                    int k=int(fill.mesh.vertices.size());
                    for(auto at:{surface(prior,inner),surface(prior,outer),surface(angle,outer),surface(angle,inner)})fill.mesh.vertices.push_back({at,{0,1,0}});
                    fill.mesh.indices.insert(fill.mesh.indices.end(),{k,k+1,k+2,k,k+2,k+3,k+2,k+1,k,k+3,k+2,k});
                }
            }
            previous=endpoint;
        }
        if(radius>.001)parts.push_back(std::move(fill));
    }
    if(p.targetBounds){auto a=p.targetBounds->low-Vec3{.02,.02,.02},b=p.targetBounds->high+Vec3{.02,.02,.02};
        frame({Vec3{a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,b.y,a.z},{a.x,b.y,a.z},{a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{a.x,b.y,b.z}},{1,.7,.1});
    }
    auto box=[&](Vec3 center,Vec3 size,Vec3 color){
        Character c;c.Shape="cube";
        auto shapes=shape_geometry(c);
        for(auto& part:shapes){
            part.color=color;
            for(auto& v:part.mesh.vertices){
                auto q=v.position;
                q={q.x*size.x,(q.y-.5)*size.y,q.z*size.z};
                v.position=center+Vec3{q.x*std::cos(p.facing)+q.z*std::sin(p.facing),q.y,-q.x*std::sin(p.facing)+q.z*std::cos(p.facing)};
            }
            part.mesh.normals();parts.push_back(std::move(part));
        }
    };
    for(auto at:p.fragments)box(at,{.12,.12,.12},{.65,.8,.9});
    if(p.showWeapons){
        // Original temporary dual pistols. First-person geometry follows the
        // camera basis, while third-person weapons follow the attack facing.
        const auto forward=(p.showBody?p.attackAim:p.viewAim).unit();
        auto right=forward.cross(p.showBody?Vec3{0,1,0}:p.viewUp).unit();
        if(right.length()<.5)right={1,0,0};
        const auto up=right.cross(forward).unit();
        const auto base=p.showBody?p.feet+Vec3{0,p.bodyHeight*.68,0}:p.eye-up*.23;
        auto weaponBox=[&](Vec3 center,Vec3 size,Vec3 color,bool glow=false){
            Character c;c.Shape="cube";
            for(auto part:shape_geometry(c)){
                part.color=color;part.emissive=glow;
                for(auto& v:part.mesh.vertices){auto q=v.position;v.position=center+right*(q.x*size.x)+up*((q.y-.5)*size.y)+forward*(q.z*size.z);}
                part.mesh.normals();parts.push_back(std::move(part));
            }
        };
        for(double side:{-1.,1.}){
            auto grip=base+right*(side*(p.showBody?.32:.24))+forward*(.48-p.shotFlash*.055);
            weaponBox(grip,{.09,.21,.12},{.10,.14,.20});
            weaponBox(grip+up*.11+forward*.10,{.12,.12,.34},{.35,.43,.55});
            weaponBox(grip+up*.12+forward*.29,{.08,.075,.07},{.12,.75,.95},true);
            if(p.shotFlash>0)weaponBox(grip+up*.12+forward*.37,{.11*p.shotFlash,.11*p.shotFlash,.15},{1,.78,.25},true);
        }
    }
    if(p.showBody){
        double height=p.control==ControlEffect::Sleep?.35:p.control==ControlEffect::Knockdown?p.bodyHeight*.6:p.bodyHeight;
        Vec3 sway{};if(p.control==ControlEffect::Stun || p.control==ControlEffect::Disorient)sway={std::sin(p.poseTime*5)*.07,0,std::cos(p.poseTime*5)*.07};
        box(p.feet+sway+Vec3{0,height*.43,0},{.45,height*.7,p.control==ControlEffect::Sleep?1.2:.3},{.18,.65,.85});
        box(p.feet+sway+Vec3{0,height*.9,0},{.28,height*.2,.28},{.85,.9,.95});
        if(p.control==ControlEffect::Stun)for(int i=0;i<3;i++){
            double a=p.poseTime*3+i*2.0943951024;auto bird=p.feet+Vec3{std::cos(a)*.35,height+.22,std::sin(a)*.35};
            box(bird,{.1,.07,.08},{1,.85,.2});line(bird-Vec3{.12,0,0},bird+Vec3{.12,0,0},{1,.85,.2},.015);
        }
    }
    if(p.showTether){
        const auto a=p.feet+Vec3{0,p.bodyHeight*.7,0},b=p.tetherAnchor;
        const auto direction=(b-a).unit();
        MeshPart rope;rope.color={.9,.7,.2};rope.emissive=true;
        for(auto axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}){
            auto side=direction.cross(axis);if(side.length()<1e-8)continue;
            side=side.unit()*.018;const int k=int(rope.mesh.vertices.size());
            for(auto at:{a+side,b+side,b-side,a-side})rope.mesh.vertices.push_back({at,{0,1,0}});
            rope.mesh.indices.insert(rope.mesh.indices.end(),{k,k+1,k+2,k,k+2,k+3,k+2,k+1,k,k+3,k+2,k});
        }
        if(!rope.mesh.indices.empty())parts.push_back(std::move(rope));
    }
    if(p.showTether)box(p.tetherAnchor,{.12,.12,.12},{.8,.9,1});
    for(const auto& actor:p.actorTelegraphs){
        AdventurePlayer::Presentation overlay;overlay.showBody=false;overlay.showTelegraph=true;
        overlay.telegraphTerrain=p.telegraphTerrain;overlay.telegraphRange=3;overlay.telegraphHalfAngle=.7853981633974483;
        overlay.attackOrigin=actor.origin;overlay.attackAim=actor.aim;overlay.channelProgress=actor.progress;overlay.telegraphKind=actor.kind;
        auto rendered=player_presentation(overlay);parts.insert(parts.end(),std::make_move_iterator(rendered.begin()),std::make_move_iterator(rendered.end()));
    }
    return batch_presentation(std::move(parts));
}
}
