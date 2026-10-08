// Shared accessors must not let many primitives multiply a model's decoded size (audit CORE-02).
static void glb_index_budget_checks(const auto& require,const std::filesystem::path& folder){
    auto build=[&](int primitives,size_t indexCount){
        std::vector<uint8_t> bin(36+indexCount,0);
        const float tri[9]={0,0,0,1,0,0,0,1,0};
        std::memcpy(bin.data(),tri,36);
        for(size_t i=0;i<indexCount;++i)bin[36+i]=uint8_t(i%3);
        json prims=json::array();
        for(int i=0;i<primitives;++i)prims.push_back({{"attributes",{{"POSITION",0}}},{"indices",1}});
        json doc={{"asset",{{"version","2.0"}}},{"buffers",json::array({{{"byteLength",bin.size()}}})},
            {"bufferViews",json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",36}},{{"buffer",0},{"byteOffset",36},{"byteLength",indexCount}}})},
            {"accessors",json::array({{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"}},{{"bufferView",1},{"componentType",5121},{"count",indexCount},{"type","SCALAR"}}})},
            {"meshes",json::array({{{"primitives",prims}}})},{"nodes",json::array({{{"mesh",0}}})},{"scenes",json::array({{{"nodes",json::array({0})}}})},{"scene",0}};
        std::string text=doc.dump();while(text.size()%4)text.push_back(' ');
        std::string out;auto put=[&](uint32_t v){out.append(reinterpret_cast<const char*>(&v),4);};
        put(0x46546c67);put(2);put(uint32_t(12+8+text.size()+8+bin.size()));
        put(uint32_t(text.size()));put(0x4e4f534a);out+=text;
        put(uint32_t(bin.size()));put(0x004e4942);out.append(reinterpret_cast<const char*>(bin.data()),bin.size());
        auto path=folder/"index-budget.glb";
        std::ofstream(path,std::ios::binary).write(out.data(),std::streamsize(out.size()));
        return path;
    };
    require(load_glb(build(2,1999998)).size()==2,"CORE-02: a shared index accessor within the total index budget loads");
    bool rejected=false;
    try{load_glb(build(4,1999998));}catch(const std::exception&){rejected=true;}
    require(rejected,"CORE-02: primitives that share one index accessor cannot exceed the total index budget");
}
