#include "rejection_note.hpp"
// Proposed authoritative native checks. Compile/run only after root integration.
static void foliage_material_checks(const auto& require,const fs::path& folder){
    bool legacy=true;
    for(int id=0;id<=11;++id){Character c;c.Shape="cube";c.Surface=id;SceneData s;s.Characters={c};auto r=SceneData::decode(s.encode());legacy&=r.Characters[0].Surface==id&&!r.Characters[0].Foliage;for(auto& v:object_mesh(r.Characters[0]).vertices)legacy&=v.kind==id;}
    require(legacy,"Existing authored material IDs 0 through 11 preserve encode/decode and geometry semantics");
    Character leaf;leaf.Shape.reset();leaf.Surface=13;leaf.Breakable=true;leaf.Foliage=FoliageMaterial{};
    SculptMesh sculpt;sculpt.Positions={0,0,0,.1,0,0,0,.2,0,0,0,.002};sculpt.Indices={0,2,1,0,1,3,0,3,2,1,2,3};sculpt.MaterialCoordinates={0,0,.37,1,0,.37,0,1,.37,0,0,.37};leaf.Sculpt=sculpt;
    Character bark;bark.Shape="cube";bark.Surface=12;bark.Foliage=FoliageMaterial{};
    SceneData scene;scene.Characters={bark,leaf};auto decoded=SceneData::decode(scene.encode());
    require(decoded.SchemaVersion==16&&json(decoded.Characters)==json(scene.Characters),"Bark and Leaf controls and coordinates survive actual native scene encode/decode");
    auto before=leaf.Sculpt->MaterialCoordinates;
    sculpt_brush(leaf,{0,0,0},{0,0,1},.3,.7,"pull",false,.05);
    require(leaf.Sculpt->MaterialCoordinates==before&&leaf.Sculpt->Positions!=sculpt.Positions,"Sculpt position edits retain leaf material coordinates and seed identity");
    auto mesh=object_mesh(decoded.Characters[1]);bool leafPayload=true;
    for(size_t i=0;i<mesh.vertices.size();++i){auto& v=mesh.vertices[i];leafPayload&=v.kind==13&&v.color.x==sculpt.MaterialCoordinates[i*3]&&v.color.y==sculpt.MaterialCoordinates[i*3+1]&&v.color.z>=0&&v.color.z<=16777215;}
    require(leafPayload,"Leaf local coordinates and exact 24-bit controls reach geometry payload");
    // Actual packet serialization, not just a reimplementation of the tag helper.
    auto bytes=render_scene(decoded);bool packetTags=true;
    for(size_t object=0;object<2;++object){const auto vertex=object_mesh(decoded.Characters[object]).vertices.front();float values[10]={float(vertex.position.x),float(vertex.position.y),float(vertex.position.z),float(vertex.normal.x),float(vertex.normal.y),float(vertex.normal.z),float(vertex.color.x),float(vertex.color.y),float(vertex.color.z),object==0?-40.f:-41.f};std::vector<uint8_t> expected(sizeof(values));std::memcpy(expected.data(),values,sizeof(values));packetTags&=std::search(bytes.begin(),bytes.end(),expected.begin(),expected.end())!=bytes.end();}
    require(packetTags,"Actual Bark and Leaf packets use opaque negative tags outside terrain and fire/smoke ranges");
    auto out=folder/L"foliage-portable";export_portable_scene(decoded,out);auto loaded=load_scene(out/L"scene.starscene");
    require(loaded.SchemaVersion==16&&json(loaded.Characters)==json(decoded.Characters),"Portable native export preserves authored foliage controls and all coordinates");
    bool invalid=false;try{auto bad=leaf;bad.Sculpt->MaterialCoordinates.pop_back();bad.validate();}catch(...){invalid=true;note_rejection(__FILE__,__LINE__);}
    require(invalid,"Malformed leaf coordinate arrays are rejected");
    invalid=false;try{auto bad=leaf;bad.Sculpt->MaterialCoordinates[2]=NAN;bad.validate();}catch(...){invalid=true;note_rejection(__FILE__,__LINE__);}
    require(invalid,"Nonfinite leaf material coordinates are rejected");
    invalid=false;try{auto bad=leaf;bad.Sculpt->MaterialCoordinates[5]=.2;bad.validate();}catch(...){invalid=true;note_rejection(__FILE__,__LINE__);}
    require(invalid,"Interpolated per-triangle leaf seed controls are rejected");
    auto rayScene=decoded;rayScene.RaytracingEnabled=true;rayScene.validate();auto rayDecoded=SceneData::decode(rayScene.encode());auto rayBytes=render_scene(rayDecoded);bool rayFallback=rayDecoded.RaytracingEnabled&&json(rayDecoded.Characters)==json(decoded.Characters);
    for(auto& c:rayDecoded.Characters){auto vertex=object_mesh(c).vertices.front();float values[10]={float(vertex.position.x),float(vertex.position.y),float(vertex.position.z),float(vertex.normal.x),float(vertex.normal.y),float(vertex.normal.z),1,1,1,0};std::vector<uint8_t> expected(sizeof(values));std::memcpy(expected.data(),values,sizeof(values));rayFallback&=std::search(rayBytes.begin(),rayBytes.end(),expected.begin(),expected.end())!=rayBytes.end();}
    require(rayFallback,"Optional ray path keeps saved foliage controls and emits regular plain tint geometry");
    auto fragmentSource=object_mesh(leaf),original=fragmentSource;foliage_fragment_fallback(fragmentSource,leaf);bool realGeometry=fragmentSource.indices==original.indices;
    for(size_t i=0;i<fragmentSource.vertices.size();++i){auto& v=fragmentSource.vertices[i];realGeometry&=(v.position-original.vertices[i].position).length()==0&&v.kind==0&&v.color.x==1&&v.color.y==1&&v.color.z==1&&!v.materialCoordinate;}
    require(leaf.Breakable&&realGeometry,"Breakable leaf fallback retains real geometry and removes unsupported fragment UV controls");
    auto physicalPieces=fracture_mesh(fragmentSource,rgb(leaf.Color));bool fragments=!physicalPieces.empty();
    for(auto& piece:physicalPieces){fragments&=closed_convex_mesh(piece.part->mesh)&&!piece.part->emissive;for(auto& v:piece.part->mesh.vertices)fragments&=v.kind==0&&v.color.x==1&&v.color.y==1&&v.color.z==1&&!v.materialCoordinate;}
    require(fragments,"Real closed leaf source still produces visible ordinary fracture meshes without fire/smoke or coordinate payload tags");
    auto large=original;large.indices.resize(240003,0);invalid=false;try{prepare_sculpt(large);}catch(...){invalid=true;note_rejection(__FILE__,__LINE__);}
    require(invalid,"Authored material coordinates do not bypass existing sculpt preparation size cap");
}
