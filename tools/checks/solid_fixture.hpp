#pragma once
#include "platform.hpp"
#include "geometry.hpp"
#include <array>
#include <cstdint>
namespace starlight::editor {
inline fs::path solid_glb_fixture(const fs::path& folder){
 const std::array<float,12> points{0,0,0,1,0,0,0,1,0,0,0,1};const std::array<uint16_t,12> indices{0,2,1,0,1,3,0,3,2,1,2,3};
 auto description=json::parse(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":72}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,1]},{"bufferView":1,"componentType":5123,"count":12,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})").dump();
 while(description.size()%4)description+=' ';std::string bytes;auto word=[&](uint32_t v){bytes.append(reinterpret_cast<const char*>(&v),4);};word(0x46546c67);word(2);word(uint32_t(12+8+description.size()+8+72));word(uint32_t(description.size()));word(0x4e4f534a);bytes+=description;word(72);word(0x004e4942);bytes.append(reinterpret_cast<const char*>(points.data()),48);bytes.append(reinterpret_cast<const char*>(indices.data()),24);auto path=folder/"solid-physics.glb";atomic_write(path,bytes);return path;
}
inline Mesh concave_solid_fixture(){Mesh concave;std::array<Vec3,6> outline{{{0,0,0},{2,0,0},{2,0,1},{1,0,1},{1,0,2},{0,0,2}}};for(double y:{0.,1.})for(auto p:outline){p.y=y;concave.vertices.push_back({p,{},{1,1,1},0});}for(auto triangle:std::array<std::array<int,3>,4>{{{0,1,3},{1,2,3},{0,3,5},{3,4,5}}}){concave.indices.insert(concave.indices.end(),{triangle[0],triangle[2],triangle[1],triangle[0]+6,triangle[1]+6,triangle[2]+6});}for(int i=0;i<6;++i){int j=(i+1)%6;concave.indices.insert(concave.indices.end(),{i,j,j+6,i,j+6,i+6});}concave.normals();return concave;}
inline Mesh u_solid_fixture(){Mesh mesh;std::array<Vec3,8> outline{{{0,0,0},{3,0,0},{3,0,3},{2,0,3},{2,0,1},{1,0,1},{1,0,3},{0,0,3}}};for(double y:{0.,1.})for(auto p:outline){p.y=y;mesh.vertices.push_back({p,{},{1,1,1},0});}for(auto t:std::array<std::array<int,3>,6>{{{0,1,5},{1,4,5},{1,2,4},{2,3,4},{0,5,7},{5,6,7}}})mesh.indices.insert(mesh.indices.end(),{t[0],t[2],t[1],t[0]+8,t[1]+8,t[2]+8});for(int i=0;i<8;++i){int j=(i+1)%8;mesh.indices.insert(mesh.indices.end(),{i,j,j+8,i,j+8,i+8});}mesh.normals();return mesh;}

inline Mesh panel_fixture(){Mesh mesh;for(auto p:{Vec3{-1,0,0},Vec3{1,0,0},Vec3{1,2,0},Vec3{-1,2,0}})mesh.vertices.push_back({p});mesh.indices={0,1,2,0,2,3};mesh.normals();return mesh;}

inline Mesh torus_fixture(){Mesh mesh;constexpr int rings=16,sides=8;for(int i=0;i<rings;++i)for(int j=0;j<sides;++j){double a=i*6.283185307179586/rings,b=j*6.283185307179586/sides;mesh.vertices.push_back({{(2+.5*std::cos(b))*std::cos(a),1+.5*std::sin(b),(2+.5*std::cos(b))*std::sin(a)}});}for(int i=0;i<rings;++i)for(int j=0;j<sides;++j){int a=i*sides+j,b=((i+1)%rings)*sides+j,c=((i+1)%rings)*sides+(j+1)%sides,d=i*sides+(j+1)%sides;mesh.indices.insert(mesh.indices.end(),{a,b,c,a,c,d});}mesh.normals();return mesh;}

}
