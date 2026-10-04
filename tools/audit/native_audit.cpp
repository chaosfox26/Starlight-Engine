#include "native_audit_records.hpp"
#include "native_process.hpp"
#include "native_cleanup.hpp"
#include "native_result_summary.hpp"
#include "native_runtime.hpp"
#include <iostream>
using namespace starlight::audit;
namespace {
void progress(const fs::path& out,int number,const std::string& text){atomic_write(out/"progress.txt",std::to_string(number)+"\n"+text);}
bool beneath(const fs::path& path,const fs::path& root){auto a=path.lexically_normal().wstring(),b=root.lexically_normal().wstring();while(!b.empty()&&(b.back()==L'\\'||b.back()==L'/'))b.pop_back();return a.size()>b.size()&&!_wcsnicmp(a.c_str(),b.c_str(),b.size())&&(a[b.size()]==L'\\'||a[b.size()]==L'/');}
void write(const fs::path& path,const json& value){atomic_write(path,value.dump(2));}
fs::path previous_runtime(const fs::path& root,const fs::path& out){
 fs::path newest;fs::file_time_type stamp{};
 for(const auto& entry:fs::directory_iterator(out.parent_path())){
  if(!entry.is_directory()||entry.path()==out)continue;auto candidate=entry.path()/"audit-summary.json";
  if(!fs::is_regular_file(candidate)||!fs::is_regular_file(entry.path()/"runtime-evidence.json"))continue;
  try{auto summary=json::parse(read_file(candidate));auto identity=summary.value("root",std::string());if(identity.empty()&&fs::is_regular_file(entry.path()/"run-state.json"))identity=json::parse(read_file(entry.path()/"run-state.json")).value("root",std::string());
   if(!identity.empty()&&fs::weakly_canonical(fs::u8path(identity))==root&&summary.value("runtimeExecuted",false)&&(newest.empty()||fs::last_write_time(candidate)>stamp)){newest=entry.path();stamp=fs::last_write_time(candidate);}
  }catch(...) {}
 }return newest;
}
std::string shareable_report(const fs::path& root,const fs::path& out,const json& summary,const json& findings){
 std::vector<std::string> lines={"# Starlight audit — shareable result","","Mode: "+summary.at("mode").get<std::string>(),"",summary.at("limits").get<std::string>(),"","Feature inventory alone does not run tests. Failures and not-run checks are not passes.",""};
 for(const auto& row:summary.at("stages")){auto name=row.at("name").get<std::string>();lines.push_back("- "+name+": **"+row.at("outcome").get<std::string>()+"** — "+python_text(row.value("error",row.value("reason",json("")))));lines.push_back("  Evidence: "+utf8((out/(name=="build"?"build.log":"stages.partial.json")).wstring()));}
 if(summary.at("stages").empty())lines.push_back("No runtime checks requested.");
 if(!summary.at("reusedRuntime").is_null())lines.insert(lines.end(),{"","Runtime results below are preserved from "+summary.at("reusedRuntime").get<std::string>()+"; tests were not rerun in this report pass."});
 lines.insert(lines.end(),{"","## Explicit findings",""});
 for(const auto& finding:findings)lines.push_back("- "+finding.at("detail").get<std::string>()+" — "+finding.at("file").get<std::string>());
 if(findings.empty())lines.push_back("No explicit findings extracted; inspect stage failures and coverage. This is not an error-free certification.");
 lines.insert(lines.end(),{"","## Coverage still outside this standard pass","","- Specialized denoiser and fallback checks run only in error/combined mode; missing fixture inputs are reported as not-run.","- Legacy-versus-native comparative profiling is not part of this standard Gate 0 pass.","- Human GUI appearance acceptance, all nested menus/DPI, long-soak leak freedom and all future features are not certified."});
 for(auto name:{"GENERATED-FEATURE-BRIEFING.md","ENGINE-COMPARISON.md"})if(fs::is_regular_file(out/name))lines.insert(lines.end(),{"",read_file(out/name)});
 if(summary.at("mode")!="errors")for(auto name:{"STARLIGHT-PRODUCT-DIRECTION.md","STARLIGHT-SCULPTING-FEASIBILITY-2026-09-18.md"}){auto path=root/"Starlight Documents/Current"/name;if(!fs::is_regular_file(path))path=root/name;if(fs::is_regular_file(path))lines.insert(lines.end(),{"","# Planned scope — not implementation proof","",read_file(path)});}
 if(summary.value("executionProfile",std::string())=="prebuilt-runtime")lines.insert(lines.end(),{"","Checks use the supplied compiled programs. Source compilation was not requested or performed; source-to-binary equivalence is not certified."});
 std::string text;for(size_t i=0;i<lines.size();++i){if(i)text+='\n';text+=lines[i];}return text;
}
}
int wmain(int argc,wchar_t** argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);fs::path root,out;bool owned=false;try{
 fs::path runtime,rules,reuse,testPlan,buildToolchain;std::string mode="features";bool keep=false;
 for(int i=1;i<argc;++i){std::wstring arg=argv[i];if(arg==L"--keep-artifacts"){keep=true;continue;}if(i+1>=argc)throw std::runtime_error("Missing audit option value");std::wstring value=argv[++i];if(arg==L"--root")root=value;else if(arg==L"--output")out=value;else if(arg==L"--runtime-dir")runtime=value;else if(arg==L"--rules")rules=value;else if(arg==L"--reuse-runtime")reuse=value;else if(arg==L"--test-plan")testPlan=value;else if(arg==L"--build-toolchain")buildToolchain=fs::absolute(value);else if(arg==L"--audit-mode")mode=utf8(value);else throw std::runtime_error("Unknown native audit option");}
 if(root.empty()||out.empty())throw std::runtime_error("NativeAudit requires --root and --output");
 if(mode!="features"&&mode!="errors"&&mode!="both")throw std::runtime_error("Unknown audit mode");if(mode!="features"&&!reuse.empty())throw std::runtime_error("Reused runtime requires features mode");
 if(!buildToolchain.empty() && mode=="features")throw std::runtime_error("Source build requires errors or both mode");
 root=fs::canonical(root);out=fs::weakly_canonical(fs::absolute(out));
 if(!SetEnvironmentVariableW(L"STARLIGHT_RESOURCE_ROOT",root.c_str()))throw std::runtime_error("Cannot set runtime resource directory");
 if(out==root||(beneath(out,root)&&!beneath(out,root/"analysis")))throw std::runtime_error("Internal reports must use a run directory under analysis");
 if(fs::exists(out/"audit-summary.json")||fs::exists(out/"run-state.json"))throw std::runtime_error("Existing audit output cannot be overwritten");
 if(runtime.empty()){wchar_t self[32768];auto n=GetModuleFileNameW(nullptr,self,32768);if(!n||n>=32768)throw std::runtime_error("Cannot locate runtime");runtime=fs::path(self).parent_path();}runtime=fs::canonical(runtime);
 if(rules.empty())rules=root/"tools/audit";rules=fs::canonical(rules);
 auto hash=hash_bytes(utf8(root.wstring()));std::transform(hash.begin(),hash.end(),hash.begin(),[](unsigned char c){return char(std::tolower(c));});
 auto lockName=L"Local\\StarlightAuditEngine-"+wide(hash.substr(0,16));starlight::audit::Handle mutex(CreateMutexW(nullptr,FALSE,lockName.c_str()));
 if(!mutex.value)throw std::runtime_error("Cannot create audit lock");if(GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Another Starlight audit is already running");
 fs::create_directories(out);std::set<std::string> preexisting;for(const auto& p:cleanup_files(out))preexisting.insert(utf8(p.generic_wstring()));owned=true;
 json records=json::array(),evidence=json::array();if(mode=="features"&&reuse.empty())reuse=previous_runtime(root,out);
 if(!reuse.empty()){reuse=fs::canonical(reuse);auto prior=json::parse(read_file(reuse/"audit-summary.json"));if(fs::weakly_canonical(fs::u8path(prior.at("root").get<std::string>()))!=root)throw std::runtime_error("Saved runtime belongs to another engine root");
  records=prior.at("stages");evidence=json::parse(read_file(reuse/"runtime-evidence.json"));if(!records.is_array()||!evidence.is_array())throw std::runtime_error("Invalid saved runtime records");
  write(out/"reused-runtime-provenance.json",{{"source",utf8(reuse.wstring())},{"sha256",{{"audit-summary.json",digest(reuse/"audit-summary.json").first},{"runtime-evidence.json",digest(reuse/"runtime-evidence.json").first}}},{"scope","Saved observations, not new test executions or proof of current-source equivalence"}});
 }
 write(out/"execution-source-hashes.json",audit_source_hashes(root,audit_source_paths(root)));
 bool executed=false,sourceBuildExecuted=false,buildPassed=true;
 if(!buildToolchain.empty()){
  sourceBuildExecuted=true;auto build=out/"source-build";json buildRecord={{"name","build"},{"outcome","fail"}};
  try {
   progress(out,1,"Building source with the selected native toolchain...");
   auto before=audit_source_hashes(root,audit_source_paths(root));
   auto child=run(runtime/"Starlight.NativeBuild.exe",{L"--root",root.wstring(),L"--build-dir",build.wstring(),L"--toolchain",buildToolchain.wstring()},root,out/"build.log",3600000);
   buildPassed=!child.timedOut && child.exitCode==0;
   buildRecord["exitCode"]=child.exitCode;buildRecord["timedOut"]=child.timedOut;
   if(before!=audit_source_hashes(root,audit_source_paths(root))){buildPassed=false;buildRecord["error"]="Source changed during compilation";}
   buildRecord["outcome"]=buildPassed?"pass":"fail";
   if(buildPassed)runtime=fs::canonical(build/"Release");
  }catch(const std::exception& error){buildPassed=false;buildRecord["error"]=error.what();}
  records.push_back(buildRecord);write(out/"stages.partial.json",records);
 }
 if(mode!="features"){
  auto plan=json::parse(read_file(testPlan.empty()?rules/"runtime-checks.json":testPlan));
  if(buildPassed)executed=run_runtime_plan(root,runtime,out,plan,records,evidence);
  else for(const auto& stage:plan.at("stages"))records.push_back({{"name",stage.at("name")},{"outcome","not-run"},{"reason","Source build failed; old binaries were not substituted"}});
 }
 bool all=!records.empty();for(const auto& row:records)all&=row.at("outcome")=="pass";
 json summary={{"root",utf8(root.wstring())},{"mode",mode},{"stages",records},{"runtimeExecuted",executed},{"sourceBuildExecuted",sourceBuildExecuted},{"executionProfile",sourceBuildExecuted?"source-build-and-runtime":mode=="features"?"feature-inventory":"prebuilt-runtime"},{"reusedRuntime",reuse.empty()?json():json(utf8(reuse.wstring()))},{"allRequestedStagesPassed",all},{"limits","Bounded checks only. Not all features, visual states, hardware or long-soak conditions are tested. Gate 0 certification remains separate."}};
 write(out/"audit-summary.json",summary);write(out/"runtime-evidence.json",evidence);auto findings=collect_audit_findings(reuse.empty()?out:reuse,!reuse.empty());write(out/"audit-findings.json",findings);
 progress(out,65,"Generating the feature report with explicit runtime evidence.");bool reportFailed=false;
 if(mode!="errors")try{auto result=run(runtime/"Starlight.NativeAnalysis.exe",{L"--root",root.wstring(),L"--output",out.wstring(),L"--rules",rules.wstring(),L"--extra-evidence",(out/"runtime-evidence.json").wstring()},root,out/"feature-generation.log",1200000);reportFailed=result.timedOut||result.exitCode!=0;}catch(const std::exception& e){atomic_write(out/"failure.txt",e.what());reportFailed=true;}
 if(reportFailed){records.push_back({{"name","feature-report-generation"},{"outcome","fail"},{"error","See feature-generation.log and failure.txt; runtime evidence is preserved"}});summary["stages"]=records;summary["allRequestedStagesPassed"]=false;write(out/"audit-summary.json",summary);}
 if(mode=="errors")atomic_write(out/"STARLIGHT-START-HERE.md","# Error-check audit\n\nSee [shareable report](AUDIT-SHAREABLE-REPORT.md).\n");
 auto status=reportFailed?"Report generation failed. Runtime evidence is preserved; see feature-generation.log.":final_status(summary,findings);atomic_write(out/"final-status.txt",status);
 auto text=shareable_report(root,out,summary,findings)+"\n\n## Final result\n\n"+status+"\n\nDefault retention keeps written summaries and structured results. Raw captures and temporary packages may be removed; old evidence links describe their original locations. Use --keep-artifacts for a future detailed diagnostic run.\n";
 atomic_write(out/"AUDIT-SHAREABLE-REPORT.md",text);json seal=json::object();for(auto name:{"AUDIT-SHAREABLE-REPORT.md","audit-summary.json","runtime-evidence.json"})seal[name]=digest(out/name).first;write(out/"audit-deliverable-hashes.json",seal);
 if(!keep&&beneath(out,root/"analysis"))try{progress(out,99,"Reports saved. Removing temporary captures and test packages...");auto plan=cleanup_plan(root,out,"report",preexisting);auto journal=out/"cleanup-records";fs::create_directories(journal);auto result=apply_cleanup(root,out,plan,journal/(L"native-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".json"));remove_empty_report_directories(root,out);write(out/"summary-retention.json",{{"files",result}});for(const auto& row:result)if(row.at("status")!="removed")throw std::runtime_error("Some generated files remain locked or could not be removed");}catch(const std::exception& e){write(out/"summary-retention-warning.json",{{"error",e.what()}});status+="\nSome temporary files could not be cleaned up; see the report folder.";atomic_write(out/"final-status.txt",status);}
 progress(out,100,status);
 if(!keep&&beneath(out,root/"analysis"))try{
  auto plan=capture_retention_plan(root,out,3);auto journal=out/"cleanup-records";fs::create_directories(journal);
  auto result=apply_cleanup(root,root/"analysis",plan,journal/(L"captures-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".json"));
  write(out/"capture-retention.json",{{"keepRecentRuns",3},{"files",result}});
  for(const auto& row:result)if(row.at("status")!="removed")throw std::runtime_error("Some old captures could not be removed");
 }catch(const std::exception& e){write(out/"capture-retention-warning.json",{{"error",e.what()}});status+="\nSome old captures were retained; see the cleanup warning.";atomic_write(out/"final-status.txt",status);progress(out,100,status);}
 return records.empty()||summary.at("allRequestedStagesPassed")==true?0:4;
}catch(const std::exception& e){if(owned)try{atomic_write(out/"failure.txt",e.what());progress(out,-1,"Audit failed; see failure.txt. Existing evidence is preserved.");}catch(...){}std::cerr<<e.what()<<'\n';return 2;}}
