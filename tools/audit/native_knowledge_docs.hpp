#pragma once
#include "native_knowledge.hpp"
namespace starlight::audit {
inline std::string table_text(const json& value){auto input=python_text(value);std::string output;for(char c:input){if(c=='|')output+="\\|";else if(c=='\n')output+=' ';else output+=c;}return output;}
inline std::map<std::string,std::string> knowledge_documents_native(const fs::path& root,const json& model,const json& templates,const std::string& provenanceText={}){
    if(templates.at("format")!="starlight-knowledge-templates-v1")throw std::runtime_error("Unknown knowledge templates");
    const auto& features=model.at("features");const auto& validation=model.at("validation");const auto& provenance=model.at("provenance");const auto& blocks=templates.at("blocks");size_t supported=0;for(const auto& f:features)if(truth(f.at("source_supported")))++supported;
    auto run=truth(validation.at("run"))?python_text(validation.at("run")):"unavailable";
    std::map<std::string,std::string> values={
        {"{{start_0_8}}","Knowledge generator "+model.at("version").get<std::string>()+"; source/evidence snapshot: "+python_text(provenance.value("evidenceTimestamp",json("unknown")))+". Generation mode: "+python_text(provenance.at("mode"))+"."},
        {"{{start_0_34}}","Latest captured audit evidence: **"+run+"**. These tests were not rerun by the knowledge generator."},
        {"{{start_1_7}}",std::to_string(supported)+"/"+std::to_string(features.size())+" curated subsystem descriptions have their required source markers. This count measures documentation support, not product completeness."},
        {"{{start_2_5}}",std::to_string(model.at("evidence_review_required").size())+" evidence files need interpretation; "+std::to_string(model.at("changed_source_review_required").size())+" changed product paths need semantic review. See KNOWLEDGE-COVERAGE.md. These are review flags, not proof of engine failure."},
        {"{{ref_1_3}}","These named interactions were exercised in `"+(truth(validation.at("run"))?python_text(validation.at("run")):"unknown run")+"`. They are a useful detailed behavior index, not fresh verification of future changes."},
        {"{{coverage_0_7}}",provenanceText.empty()?provenance.dump(2):provenanceText}};
    using Lines=std::vector<std::string>;auto append=[&](Lines& lines,const std::string& key){for(const auto& row:blocks.at(key)){auto text=row.get<std::string>();if(text.starts_with("{{")){if(!values.contains(text))throw std::runtime_error("Unresolved report template value");text=values.at(text);}lines.push_back(text);}};
    std::map<std::string,std::string> documents;auto finish=[&](const std::string& filename,const Lines& lines){std::string text;for(const auto& line:lines)text+=line+"\n";documents[filename]=std::move(text);};
    Lines start;append(start,"start_0");for(const auto& row:validation.at("summary"))start.push_back("- "+row.get<std::string>());append(start,"start_1");if(truth(model.value("product_direction",json())))append(start,"direction_0");append(start,"start_2");finish("STARLIGHT-START-HERE.md",start);
    Lines reference;append(reference,"ref_0");for(const auto& f:features)reference.push_back("| "+f["name"].get<std::string>()+" | "+table_text(f["purpose"])+" | "+table_text(f["implementation_status"])+" |");
    for(const auto& f:features){for(const auto& line:Lines{"","## "+f["name"].get<std::string>(),"","**Purpose:** "+f["purpose"].get<std::string>(),"","**Implementation:** "+f["implementation_status"].get<std::string>()+".","","**User workflow / behavior:** "+f["workflow"].get<std::string>(),"","**Connections:** "+f["connections"].get<std::string>(),"","**Limits:** "+f["limits"].get<std::string>(),"","**Evidence:** Source support is corroboration, not a new runtime test."})reference.push_back(line);
        for(const auto& e:f["evidence"]){auto file=e["file"].get<std::string>(),line=python_text(e["line"]);reference.push_back("- ["+file+":"+line+"]("+utf8((root/fs::u8path(file)).generic_wstring())+":"+line+")");}
        for(const auto& missing:f["missing"])reference.push_back("- Required support missing: `"+missing["file"].get<std::string>()+"` / `"+missing["pattern"].get<std::string>()+"`. Description above requires review.");
    }
    append(reference,"ref_1");for(const auto& check:validation.at("ui_checks"))reference.push_back("| "+table_text(check.value("name",json("unnamed")))+" | "+(check.value("pass",json())==true?"Pass":"Fail/unknown")+" |");append(reference,"ref_2");
    for(const auto& item:model.at("catalogue"))reference.push_back("| "+table_text(item.value("Name",json()))+" | "+table_text(item.value("Description",json()))+" | "+table_text(json(python_text(item.value("Status",json()))+" / "+python_text(item.value("Available",json()))))+" | "+table_text(item.value("DependsOn",json()))+" | "+table_text(item.value("Proof",json()))+" |");finish("STARLIGHT-FEATURE-REFERENCE.md",reference);
    Lines workflows;append(workflows,"workflows_0");finish("STARLIGHT-WORKFLOWS.md",workflows);
    Lines coverage;append(coverage,"coverage_0");if(model.at("unmapped_capabilities").empty())coverage.push_back("None in the current curated capability set.");else for(const auto& name:model.at("unmapped_capabilities"))coverage.push_back("- "+name.get<std::string>());
    append(coverage,"coverage_1");for(const auto& path:model.at("source_files_without_direct_feature_citation"))coverage.push_back("- `"+path.get<std::string>()+"`");append(coverage,"coverage_2");
    if(model.at("evidence_review_required").empty())coverage.push_back("None.");else for(const auto& item:model.at("evidence_review_required"))coverage.push_back("- "+item.at("path").get<std::string>()+" — "+item.at("reason").get<std::string>());
    append(coverage,"coverage_3");if(model.at("changed_source_review_required").empty())coverage.push_back("None recorded.");else for(const auto& path:model.at("changed_source_review_required"))coverage.push_back("- "+path.get<std::string>());finish("KNOWLEDGE-COVERAGE.md",coverage);
    return documents;
}
}
