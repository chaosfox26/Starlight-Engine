#pragma once
#include "native_inventory.hpp"
#include <regex>
namespace starlight::audit {
inline std::vector<std::string> source_lines(const std::string& text){
    std::vector<std::string> lines;size_t begin=0;
    for(size_t i=0;i<text.size();++i){size_t width=0;unsigned char c=text[i];
        if(c=='\r')width=(i+1<text.size() && text[i+1]=='\n')?2:1;
        else if(c=='\n' || c=='\v' || c=='\f' || (c>=0x1c && c<=0x1e))width=1;
        else if(text.compare(i,2,"\xc2\x85")==0)width=2;
        else if(text.compare(i,3,"\xe2\x80\xa8")==0 || text.compare(i,3,"\xe2\x80\xa9")==0)width=3;
        if(width){lines.push_back(text.substr(begin,i-begin));i+=width-1;begin=i+1;}
    }if(begin<text.size())lines.push_back(text.substr(begin));return lines;
}
inline std::string strip_source(std::string text){auto first=text.find_first_not_of(" \t\r\n\v\f"),last=text.find_last_not_of(" \t\r\n\v\f");return first==std::string::npos?"":text.substr(first,last-first+1);}
struct SourceAnalysis {json index=json::array(),evidence=json::array(),capabilities=json::array(),contracts,build;std::vector<std::pair<std::string,std::string>> sources;};
inline json analyze_build(const fs::path& root,const std::vector<std::pair<std::string,std::string>>& sources){
    json declarations=json::array(),compiled=json::array(),includes=json::array();std::map<std::string,std::string> byPath(sources.begin(),sources.end());
    std::regex cmake(R"(\b(add_library|add_executable|add_test|target_link_libraries|target_sources|add_subdirectory|option|set)\s*\(([^)]*)\))");
    for(const std::string rel:{"CMakeLists.txt","deep/Physics/CMakeLists.txt"}){const auto& text=byPath[rel];for(std::sregex_iterator it(text.begin(),text.end(),cmake),end;it!=end;++it)declarations.push_back({{"file",rel},{"line",1+std::count(text.begin(),text.begin()+it->position(),'\n')},{"command",(*it)[1].str()},{"arguments",strip_source((*it)[2].str())}});}
    auto generated=root/"out/classic/CMakeFiles";
    if(fs::is_directory(generated))for(const auto& file:fs::directory_iterator(generated)){auto name=file.path().filename().wstring();if(!name.starts_with(L"impl-") || !name.ends_with(L".ninja"))continue;
        auto lines=source_lines(utf8(wide(read_file(file.path()))));for(size_t i=0;i<lines.size();++i)if(lines[i].starts_with("build ") && lines[i].find("CXX_COMPILER")!=std::string::npos)compiled.push_back({{"buildFile",utf8(file.path().lexically_relative(root).generic_wstring())},{"line",i+1},{"rule",lines[i]}});
    }
    std::regex include(R"rule(^\s*#\s*include\s*"([^"]+)")rule",std::regex::ECMAScript|std::regex::multiline);
    for(const auto& [rel,text]:sources)for(std::sregex_iterator it(text.begin(),text.end(),include),end;it!=end;++it){auto name=(*it)[1].str();json resolved=nullptr;
        for(const auto& base:{(root/fs::u8path(rel)).parent_path(),root/"editor/cpp",root/"deep/engine",root/"out/classic/generated"}){auto path=base/fs::u8path(name);if(fs::is_regular_file(path)){resolved=utf8(fs::canonical(path).wstring());break;}}
        includes.push_back({{"from",rel},{"include",name},{"resolved",resolved},{"method","heuristic quoted-include resolution; not compiler reachability"}});
    }
    return {{"cmake_declarations",declarations},{"existing_ninja_compile_rules",compiled},{"include_edges",includes},{"limits","CMake parsing is lexical; conditions/variables and generated rules require review. No linker reachability or dead-code proof."}};
}
inline SourceAnalysis analyze_sources(const fs::path& root,const json& files,const json& rules){
    if(rules.at("format")!="starlight-source-rules-v1")throw std::runtime_error("Unsupported source rules");
    SourceAnalysis result;std::vector<std::pair<std::string,std::regex>> tokens,contracts;
    for(const auto& row:rules.at("tokens"))tokens.emplace_back(row[0].get<std::string>(),std::regex(row[1].get<std::string>()));
    for(const auto& row:rules.at("contracts"))contracts.emplace_back(row[0].get<std::string>(),std::regex(row[1].get<std::string>()));
    const auto extensions=rules.at("source_extensions").get<std::set<std::string>>(),models=rules.at("models").get<std::set<std::string>>();
    result.contracts={{"records",json::array()},{"feature_catalog",nullptr},{"limits","Lexical source extracts, not a type-checked ABI/layout model. Packed/wire types and authoring types may differ."}};
    std::regex modelPattern(R"(\b(?:double|float|int|bool|uint\w*|std::|static constexpr|struct|class)\b)");
    for(const auto& file:files){auto rel=file.at("path").get<std::string>(),kind=file.value("category",std::string());auto path=fs::u8path(rel);auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(kind!="current source candidate" && kind!="analysis/migration tooling" && rel!="CMakeLists.txt" && rel!="Build-Native.py")continue;
        if(!extensions.contains(ext) && path.filename()!="CMakeLists.txt")continue;
        if(path.filename()=="dxr_shader.h" || path.filename()=="atmosphere_shader.h" || path.filename()=="render_device_editor.h"){
            result.index.push_back({{"path",rel},{"classification","generated source, byte inventory only"},{"sha256",file.value("sha256",json())}});continue;
        }
        try{auto raw=read_file(long_path(root/path));auto text=utf8(wide(raw));auto lines=source_lines(text);result.sources.emplace_back(rel,text);json counts=json::object(),symbols=json::array(),includes=json::array();
            for(size_t i=0;i<lines.size();++i){const auto& line=lines[i];
                for(const auto& [name,pattern]:tokens)for(std::sregex_iterator it(line.begin(),line.end(),pattern),end;it!=end;++it){
                    counts[name]=counts.value(name,0)+1;result.evidence.push_back({{"file",rel},{"line",i+1},{"kind",name},{"text",strip_source(line)}});
                    if(name=="types" || name=="native_exports" || name=="function_candidates")symbols.push_back({{"kind",name},{"name",(*it)[1].str()},{"line",i+1}});
                    if(name=="includes")includes.push_back((*it)[1].str());
                }
                for(const auto& [name,pattern]:contracts)if(std::regex_search(line,pattern))result.contracts["records"].push_back({{"file",rel},{"line",i+1},{"kind",name},{"text",strip_source(line)}});
                if(models.contains(rel) && std::regex_search(line,modelPattern))result.contracts["records"].push_back({{"file",rel},{"line",i+1},{"kind","data_model_declaration"},{"text",strip_source(line)}});
            }
            auto hash=hash_bytes(raw);std::transform(hash.begin(),hash.end(),hash.begin(),[](unsigned char c){return char(std::tolower(c));});result.index.push_back({{"path",rel},{"lines",lines.size()},{"sha256",hash},{"matches_inventory",hash==file.value("sha256",std::string())},{"lexical_counts",counts},{"symbols",symbols},{"includes",includes}});
        }catch(const std::exception& e){result.index.push_back({{"path",rel},{"error",e.what()}});}
    }
    std::map<std::string,std::string> sources(result.sources.begin(),result.sources.end());
    for(const auto& row:rules.at("capabilities")){json evidence=json::array(),missing=json::array();
        for(const auto& required:row[2]){auto rel=required[0].get<std::string>(),pattern=required[1].get<std::string>();auto text=sources.contains(rel)?sources.at(rel):std::string();std::smatch match;
            if(std::regex_search(text,match,std::regex(pattern,std::regex::ECMAScript|std::regex::icase))){auto number=1+std::count(text.begin(),text.begin()+match.position(),'\n');auto lines=source_lines(text);evidence.push_back({{"file",rel},{"line",number},{"matched",match.str()},{"excerpt",lines.at(number-1)}});}
            else missing.push_back({{"file",rel},{"pattern",pattern}});
        }
        result.capabilities.push_back({{"title",row[0]},{"interpretation",row[1]},{"status",missing.empty()?"source markers found; interpretation requires review":"incomplete source support—do not assume implemented"},{"evidence",evidence},{"missing",missing},{"limits",row[3]}});
    }
    if(sources.contains("editor/cpp/features.hpp")){const auto& text=sources.at("editor/cpp/features.hpp");auto start=text.find("R\"FEATURES(");if(start!=std::string::npos){start+=11;auto end=text.find(")FEATURES\"",start);if(end!=std::string::npos)try{result.contracts["feature_catalog"]=json::parse(text.substr(start,end-start));}catch(const std::exception& e){result.contracts["catalog_error"]=e.what();}}}
    result.build=analyze_build(root,result.sources);return result;
}
}
