"""Generate a dated, source-backed capability comparison, never an inferred benchmark."""
import argparse
import datetime as dt
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

REVIEWED = '2026-09-23'
ENGINES = ['Unreal Engine 5', 'Unity 6', 'Godot 4']
# These are reviewed descriptions, not live online discovery. URLs may roll forward.
ROWS = [
    ('Animation workflow', ['Static model import','Routes and simulation controls','Crowd runtime'], [
        ('Animation Blueprints support character animation logic.', 'https://dev.epicgames.com/documentation/unreal-engine/animation-blueprints-in-unreal-engine'),
        ('Animation systems support clips and controller-based animation workflows.', 'https://docs.unity3d.com/6000.0/Documentation/Manual/AnimationOverview.html'),
        ('Animation nodes and an animation editor provide animation authoring.', 'https://docs.godotengine.org/en/stable/tutorials/animation/')],
     'Prove imported character animation through editor authoring and standalone export. Routes and a specialized crowd runtime are not equivalent to that pipeline.'),
    ('Gameplay authoring', ['Adventure gameplay foundation'], [
        ('Blueprints provides node-based gameplay scripting.', 'https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprints-visual-scripting-in-unreal-engine'),
        ('C# scripting provides programmable gameplay behavior.', 'https://docs.unity3d.com/6000.0/Documentation/Manual/scripting.html'),
        ('Supports GDScript, C# and native extension language options.', 'https://docs.godotengine.org/en/stable/getting_started/step_by_step/scripting_languages.html')],
     'Assess reusable authored rules beyond the bounded key/door/exit workflow; compare equivalent tasks, not feature names.'),
    ('Standalone builds', ['Windows adventure export'], [
        ('Packaging creates distributable applications for supported target platforms; platform prerequisites apply.', 'https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project'),
        ('Build profiles configure applications for different target platforms.', 'https://docs.unity3d.com/6000.0/Documentation/Manual/BuildSettings.html'),
        ('Export presets and templates produce platform-specific game builds.', 'https://docs.godotengine.org/en/stable/tutorials/export/exporting_projects.html')],
     'Verify Starlight on a clean machine and explicitly supported target platforms; a local package smoke test is narrower.'),
    ('Starting and learning', ['Discoverable editor help','Classic creator interface','Walk and build preview'], [
        ('Project templates provide starting configurations and sample gameplay.', 'https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-templates-reference'),
        ('Unity Hub offers project templates.', 'https://docs.unity.com/en-us/hub/templates'),
        ('Project Manager creates, imports and opens projects.', 'https://docs.godotengine.org/en/stable/tutorials/editor/project_manager.html')],
     'Test beginner task completion, discoverability and recovery with people. Documentation cannot establish which experience is easier or unique.'),
]

def native_generate(root,program,out,model,graph=None,today=None):
    with tempfile.TemporaryDirectory(prefix='starlight-comparison-') as temp:
        base=Path(temp)
        (base/'model.json').write_text(json.dumps(model),encoding='utf-8')
        (base/'graph.json').write_text(json.dumps(graph or {}),encoding='utf-8')
        command=[str(program),str(base/'model.json'),str(base/'graph.json'),
                 str(root/'tools/audit/comparison-rules.json'),str(base/'result')]
        if today: command.append(today.isoformat())
        result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace')
        if result.returncode: raise RuntimeError('Native comparison report failed: '+result.stderr.strip())
        for name in ('engine-comparison.json','ENGINE-COMPARISON.md'):
            with (out/name).open('xb') as target: target.write((base/'result'/name).read_bytes())
        return json.loads((base/'result/engine-comparison.json').read_text(encoding='utf-8'))


def generate(out, model, graph=None, today=None):
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    today=today or dt.datetime.now(dt.timezone.utc).date()
    age=(today-dt.date.fromisoformat(REVIEWED)).days
    freshness='review required' if age<0 or age>90 else 'within 90-day review window; not rechecked online this run'
    by_name={f['name']:f for f in model.get('features',[])}
    linked={f['name']:f for f in (graph or {}).get('features',[])}
    mapped=set();rows=[]
    for title,names,peers,next_step in ROWS:
        starlight=[]
        for name in names:
            mapped.add(name);f=by_name.get(name)
            starlight.append({'feature':name,'implementation':f.get('implementation_status','unknown') if f else 'unknown: missing source assessment',
                'sourceSupported':bool(f and f.get('source_supported')),
                'workflow':f.get('workflow','') if f else '', 'limits':f.get('limits','') if f else '',
                'sourceEvidence':f.get('evidence',[]) if f else [],
                'verification':linked.get(name,{}).get('verification','No linked feature verification supplied'),
                'runtimeEvidence':linked.get(name,{}).get('evidence',[]),
                'issues':linked.get(name,{}).get('issues',[])})
        rows.append({'area':title,'starlight':starlight,'competitors':[
            {'engine':engine,'claim':claim,'url':url,'reviewed':REVIEWED,'freshness':freshness,
             'evidenceType':'official documentation; not locally tested'} for engine,(claim,url) in zip(ENGINES,peers)],
             'nextValidation':next_step})
    report={'schema':'starlight.engine-comparison.v1','generatedUtc':dt.datetime.now(dt.timezone.utc).isoformat(),
        'starlightProvenance':model.get('provenance',{}), 'inputModelSha256':hashlib.sha256(json.dumps(model,sort_keys=True).encode()).hexdigest(),
        'starlightValidation':model.get('validation',{}),
        'catalogueReviewed':REVIEWED,'competitorFreshness':freshness,'referenceScope':'UE5 rolling documentation, Unity 6000.0/Hub, Godot stable 4.x; exact installed builds not tested',
        'rows':rows,'unmappedStarlightFeatures':sorted(set(by_name)-mapped),
        'performance':{'status':'not compared','reason':'No matched cross-engine benchmark dataset supplied. Starlight-only FPS and passing tests establish no ranking.',
          'required':['exact engine builds','same hardware and drivers','same scene and workload','matched native resolution and visual quality','same presentation and frame-cap policy','warmup and measurement duration','CPU/GPU frame times, percentiles and raw evidence']},
        'limits':'Selected capability areas, not exhaustive engine parity. Unknown is not absent. No percentages, winner or production-readiness score is inferred.'}
    (out/'engine-comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    lines=['# Starlight compared with Unreal Engine 5, Unity and Godot','',report['limits'],'',
        '**Competitor references reviewed:** '+REVIEWED+'. '+freshness+'.', '',report['referenceScope'],'',
        'Starlight descriptions come from the supplied audit snapshot. Competitor descriptions come from official documentation, not tests on this computer. Planned Starlight modes are not counted as delivered features.','']
    for row in rows:
        lines+=['## '+row['area'],'']
        for f in row['starlight']:
            lines+=['**Starlight — '+f['feature']+':** '+f['implementation'],f['workflow'],
                '**Limits:** '+f['limits'],'**Verification:** '+f['verification'],'']
            for e in f['runtimeEvidence']:
                lines.append('- Runtime evidence: '+str(e.get('path','unknown'))+'; '+str(e.get('outcome','unknown'))+'; '+str(e.get('freshness','unknown')))
            for issue in f['issues']:
                lines.append('- Known issue: '+str(issue.get('status','unknown'))+' — '+str(issue.get('summary','See linked evidence')))
        for peer in row['competitors']:
            lines+=['- **'+peer['engine']+':** '+peer['claim']+' [Official source]('+peer['url']+').']
        lines+=['','**Next validation:** '+row['nextValidation'],'']
    lines+=['## Performance comparison','','**Not compared.** '+report['performance']['reason'],'',
        'Required for a defensible comparison: '+', '.join(report['performance']['required'])+'.','',
        '## Coverage still to map','','The full Starlight feature reference remains authoritative for these unmapped areas. This comparison does not assume competitors lack them.','']
    lines+=['- '+name for name in report['unmappedStarlightFeatures']]
    (out/'ENGINE-COMPARISON.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--from-run',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():p.error('Use a new output folder; saved reports are not overwritten')
    model=json.loads((a.from_run/'engine-knowledge.json').read_text(encoding='utf-8'))
    graph_path=a.from_run/'feature-status.json'
    graph=json.loads(graph_path.read_text(encoding='utf-8')) if graph_path.exists() else {}
    generate(a.output,model,graph)
    for name in ('REVIEW-NOTES.md','USER-VISUAL-FINDING.md'):
        source=a.from_run/name
        if source.is_file():
            (a.output/name).write_bytes(source.read_bytes())
            with (a.output/'ENGINE-COMPARISON.md').open('a',encoding='utf-8') as report:
                report.write('\n[Preserved audit review: '+name+']('+name+')\n')
