// Generated registry describes names supported by this build, not arbitrary source guesses.
#include "settings_registry.generated.h"
static std::string normalized(std::string s) {
    std::string out;for(unsigned char c:s)if(std::isalnum(c))out+=(char)std::tolower(c);return out;
}
static int distance(const std::string&a,const std::string&b) {
    std::vector<int> row(b.size()+1);for(size_t j=0;j<row.size();++j)row[j]=(int)j;
    for(size_t i=0;i<a.size();++i){int diagonal=row[0];row[0]=(int)i+1;for(size_t j=0;j<b.size();++j){int old=row[j+1];row[j+1]=std::min({row[j+1]+1,row[j]+1,diagonal+(a[i]!=b[j])});diagonal=old;}}return row.back();
}
static json preset(const fs::path&path,const json&approved) {
    json original=starlight::preset::read_limited(path),p=original,registry=json::parse(kSettingsRegistry);
    keys(p,{"schemaVersion","name","settings"});if(p.at("schemaVersion")!=1&&p.at("schemaVersion")!=2)throw std::runtime_error("Unsupported preset version; original preserved");int version=p.at("schemaVersion").get<int>();
    if(version!=1&&version!=2)throw std::runtime_error("Unsupported preset version; original preserved");
    if(!p.at("settings").is_object())throw std::runtime_error("Settings must be an object");
    auto settings=p.at("settings");json changes=json::array(),issues=json::array();
    std::set<std::string> names;for(auto&name:registry.at("settings"))names.insert(name.get<std::string>());
    for(auto i=approved.begin();i!=approved.end();++i)if(!settings.contains(i.key())||names.count(i.key()))throw std::runtime_error("Confirmation must name an existing noncanonical setting");
    for(auto i=settings.begin();i!=settings.end();++i) {
        const std::string old=i.key();if(names.count(old))continue;
        std::string target,conversion="identity",reason;
        if(approved.contains(old)) {target=approved.at(old).get<std::string>();reason="user-confirmed";if(!names.count(target))throw std::runtime_error("Confirmed target is not supported by this engine");}
        else if(registry.at("aliases").contains(old)){auto rule=registry.at("aliases").at(old);target=rule.at("target");conversion=rule.at("conversion");reason="verified-alias";}
        else {
            for(auto&name:names)if(normalized(old)==normalized(name)){target=name;reason="equivalent-spelling";break;}
        }
        if(target.empty()) {
            json suggestions=json::array();std::vector<std::pair<int,std::string>> ranked;
            for(auto&name:names){int score=distance(normalized(old),normalized(name));if(score<=3)ranked.push_back({score,name});}
            std::sort(ranked.begin(),ranked.end());for(size_t k=0;k<std::min<size_t>(3,ranked.size());++k)suggestions.push_back(ranked[k].second);
            issues.push_back({{"setting",old},{"reason","No verified equivalent"},{"suggestions",suggestions}});continue;
        }
        json value=i.value();
        if(conversion=="bool-to-display") {if(!value.is_boolean()){issues.push_back({{"setting",old},{"reason","Legacy fullscreen requires a boolean"}});continue;}value=value.get<bool>()?"fullscreen":"windowed";}
        if(p["settings"].contains(target)&&p["settings"][target]!=value){issues.push_back({{"setting",old},{"reason","Conflicts with existing value"},{"target",target}});continue;}
        p["settings"].erase(old);p["settings"][target]=value;
        changes.push_back({{"from",old},{"to",target},{"reason",reason},{"conversion",conversion}});
    }
    fs::path reconciliation=path;reconciliation+=L".reconciliation.json";
    if(!issues.empty()) {bool saved=true;try{write(reconciliation,{{"status","needs-confirmation"},{"issues",issues},{"verifiedProposals",changes},{"originalPreserved",true}});}catch(const std::exception&){saved=false;}throw std::runtime_error(saved?"Preset needs reconciliation; suggestions saved beside it. Original unchanged.":"Preset needs reconciliation, but the suggestions could not be saved beside it (is the folder read-only?). Original unchanged.");}
    p["schemaVersion"]=2;
    try{validate(p);}catch(const std::exception&e){try{write(reconciliation,{{"status","invalid-value"},{"error",e.what()},{"originalPreserved",true}});}catch(const std::exception&){}throw;}   // a failed report write must not hide the validation error that explains the problem
    if(p!=original) {
        fs::path backup=path;backup+=version==1?L".v1.backup":L".reconcile.backup";
        if(fs::exists(backup)){bool same=false;try{same=starlight::preset::read_limited(backup)==original;}catch(const std::exception&){}   // a torn or unreadable backup counts as different (audit LAUNCH-3)
            if(!same)backup+=L"."+std::to_wstring(GetTickCount64());}
        if(!fs::exists(backup))fs::copy_file(path,backup);
        fs::path temp=path;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L".migrating";
        try{write(temp,p);}catch(...){std::error_code ignored;fs::remove(temp,ignored);throw;}
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::error_code ignored;fs::remove(temp,ignored);throw std::runtime_error("Could not save reconciled preset; backup preserved");}
        json report={{"status","reconciled"},{"fromVersion",version},{"toVersion",2},{"changes",changes},{"backup",u8text(backup)},{"registrySource",registry.at("sourceSha256")}};
        try{write(reconciliation,report);fs::path migration=path;migration+=L".migration.json";write(migration,report);}catch(const std::exception&){}   // the preset is already migrated; a report that cannot be written must not fail the launch (audit LAUNCH-3)
    } else if(fs::exists(reconciliation)) {
        // Clear obsolete pending suggestions after a user manually resolves them; an unreadable report is left alone.
        try{auto report=starlight::preset::read_limited(reconciliation);if(report.value("status","")=="needs-confirmation"||report.value("status","")=="invalid-value")write(reconciliation,{{"status","valid"},{"originalPreserved",true}});}catch(const std::exception&){}
    }
    return p;
}
