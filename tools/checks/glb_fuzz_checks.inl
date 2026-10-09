// Deterministic mutation fuzzing of the editor's GLB loader (parse_glb, validate_model_bytes, load_glb). The skinned model loader has the same fuzzer in texture_checks.cpp.
// Short and fixed by default; STARLIGHT_GLB_FUZZ_ITERATIONS=<n> runs a long one (see tools/checks/glb_fuzz.hpp for the switches and the rules every input must meet).
#include "glb_fuzz.hpp"
static std::string glb_fuzz_part_problem(const std::vector<MeshPart>& parts){
    size_t vertices=0;
    for(const auto& part:parts){
        vertices+=part.mesh.vertices.size();
        try{part.mesh.validate();}catch(const std::exception& e){return std::string("an accepted part fails its own validation: ")+e.what();}
        for(const auto& v:part.mesh.vertices){
            if(!std::isfinite(v.normal.x)||!std::isfinite(v.normal.y)||!std::isfinite(v.normal.z))return "a vertex normal is not finite";
            if(v.position.y<-1e-9||v.position.y>1.8+1e-9)return "a vertex lies outside the normalised 0..1.8 height";
        }
    }   // a part may hold no triangles (its consumers skip it), the model may not hold no vertices
    return vertices?std::string():std::string("accepted with no vertices");
}
static void glb_fuzz_checks(const auto& require,const std::filesystem::path& folder,const std::filesystem::path& root){
    std::vector<glbfuzz::Seed> seeds;
    seeds.push_back({"makeGlb",glbfuzz::makeGlb()});
    glbfuzz::GlbSpec step;step.step=true;seeds.push_back({"makeGlb-step",glbfuzz::makeGlb(step)});
    glbfuzz::GlbSpec matrix;matrix.matrixJoint=true;seeds.push_back({"makeGlb-matrix",glbfuzz::makeGlb(matrix)});
    glbfuzz::GlbSpec three;three.primitives=3;seeds.push_back({"makeGlb-3-primitives",glbfuzz::makeGlb(three)});
    seeds.push_back({"sparse",glbfuzz::makeSparseGlb(76)});
    seeds.push_back({"interleaved",glbfuzz::makeInterleavedGlb()});
    seeds.push_back({"data-uri",glbfuzz::makeDataUriGlb()});
    for(const char* name:{"migration/fixtures/triangle.glb","migration/fixtures/deep-node-graph.glb"}){
        std::ifstream in(root/name,std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        if(bytes.empty()){std::cout<<"  NOT RUN, repo GLB fixture missing for the editor fuzzer: "<<name<<"\n";continue;}
        seeds.push_back({name,std::move(bytes)});
    }
    for(size_t s:{size_t(0),size_t(1),size_t(2),size_t(3),size_t(5)}){
        const auto& bytes=seeds[s].bytes;
        std::string problem;
        try{problem=glb_fuzz_part_problem(parse_glb(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));}catch(const std::exception& e){problem=e.what();}
        if(!problem.empty())std::cout<<"  unmutated fixture "<<seeds[s].name<<": "<<problem<<"\n";
        require(problem.empty(),"GLB fuzz: each unmutated model fixture loads in the editor and is valid");
    }
    const auto scratch=folder/"glb-fuzz-editor";
    std::filesystem::create_directories(scratch);
    size_t counter=0;
    const size_t iterations=size_t(glbfuzz::envNumber("STARLIGHT_GLB_FUZZ_ITERATIONS",8000));
    const auto report=glbfuzz::run("editor",seeds,iterations,glbfuzz::envNumber("STARLIGHT_GLB_FUZZ_SEED",20261008),scratch,[&](const std::vector<uint8_t>& bytes){
        glbfuzz::Outcome out;
        const std::string_view view(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        try{
            auto parts=parse_glb(view);
            out.accepted=true;
            const std::string problem=glb_fuzz_part_problem(parts);
            if(!problem.empty()){out.valid=false;out.what=problem;}
        }catch(const std::exception& e){
            if(std::string(e.what()).empty()){out.valid=false;out.what="a rejection with no message";}
        }
        try{validate_model_bytes(view);}catch(const std::exception&){}
        if(out.valid&&++counter%50==0){   // the file entry point agrees with the memory one
            const auto file=scratch/"fuzz-input.glb";
            std::ofstream(file,std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
            bool viaFile=false;
            try{viaFile=!load_glb(file).empty();}catch(const std::exception&){}
            if(viaFile!=out.accepted){out.valid=false;out.what="load_glb and parse_glb disagree about the same bytes";}
        }
        return out;
    });
    require(report.failures==0,"GLB fuzz: the editor loader never crashes, hangs, over-allocates, or accepts an invalid model");
    require(report.accepted>0&&report.rejected>0,"GLB fuzz: the editor corpus exercises both outcomes");
}
