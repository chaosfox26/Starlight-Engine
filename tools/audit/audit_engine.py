"""User-operated audit coordinator. Each invocation owns a fresh evidence folder."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import shutil
import ctypes
import feature_evidence
from deep_analysis import write_progress


def write(path, value):
    path.write_text(json.dumps(value, indent=2), encoding='utf-8')


def native_progress(runtime, age):
    message = f'Render/lifecycle/visible audit — {age}s; keep visible tests foreground.'
    try:
        candidates = [runtime/name/'lifecycle-progress.json'
                      for name in ('lifecycle-traced', 'lifecycle-idle-control')]
        recent = [p for p in candidates if p.is_file() and time.time()-p.stat().st_mtime < 10]
        if recent:
            latest = max(recent, key=lambda p: p.stat().st_mtime)
            child = json.loads(latest.read_text(encoding='utf-8'))
            if not isinstance(child, dict):
                return message
            stage = child.get('stage', 'lifecycle')
            elapsed = round(child.get('elapsedMs', 0)/1000)
            cycles = (f"; cycles {child['completed']}/{child['total']}"
                      if 'completed' in child and 'total' in child else '')
            message += f' {latest.parent.name}: {stage} — {elapsed}s{cycles}.'
    except (OSError, ValueError, TypeError):
        pass  # A transient progress read must never abort the actual checks.
    return message


def execute(command, directory, log, timeout, progress):
    start = time.monotonic()
    with log.open('w', encoding='utf-8') as stream:
        try:
            process = subprocess.Popen(command, cwd=directory, stdout=stream, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        except OSError as error:
            return {'outcome': 'unavailable', 'error': str(error), 'command': command}
        while process.poll() is None:
            progress(round(time.monotonic()-start))
            if time.monotonic()-start > timeout:
                # Kill only this coordinator-owned process tree.
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
                process.wait(timeout=15)
                return {'outcome': 'timeout', 'command': command, 'timeoutSeconds': timeout}
            time.sleep(.5)
    return {'outcome': 'pass' if process.returncode == 0 else 'fail', 'exitCode': process.returncode,
            'command': command, 'seconds': time.monotonic()-start}


def source_paths(root):
    return sorted({p.relative_to(root).as_posix()
        for folder in ('editor/cpp', 'editor/native', 'deep/engine', 'deep/Physics', 'deep/src', 'tools')
        for p in (root/folder).rglob('*')
        if p.is_file() and p.suffix in ('.cpp','.hpp','.h','.inl','.hlsl','.py')}
        | {p for p in ('CMakeLists.txt','CMakePresets.json','Build-Native.py') if (root/p).exists()})


def source_hashes(root, sources):
    return {p: hashlib.sha256((root/p).read_bytes()).hexdigest() for p in sources}


def test_command(test, stage):
    """Redirect known native test artifacts; leave source/reference inputs alone."""
    command = list(test['command'])
    name = test['name']
    if name in ('native-core','native-diagnostics','native-textures','native-session'):
        command[1] = str(stage)
    elif name == 'native-classic-ui':
        command[-1] = str(stage/'result.json')
    elif name == 'native-no-managed':
        command += ['--output', str(stage/'result.json')]
    elif name == 'native-adventure-export':
        command[command.index('--output')+1] = str(stage)
    return command


FEATURE_BINDINGS = {
    'audit-contract': ['diagnostics-and-existing-audits'],
    'native-core': ['scene-data-and-compatibility','scene-packet-abi','local-automation','persistence-and-editing-safety','adventure-gameplay-foundation','object-geometry-and-sculpting'],
    'native-diagnostics': ['diagnostics-and-existing-audits'],
    'native-textures': ['texture-foundation'],
    'native-session': ['render-ownership-and-updates','routes-and-simulation-controls','rigid-body-simulation','water-and-lava-flow'],
    'native-classic-ui': ['classic-creator-interface'],
    'native-adventure-export': ['adventure-gameplay-foundation','windows-adventure-export'],
    'native-no-managed': ['native-application-and-build'],
    'native-no-managed-policy': ['native-application-and-build'],
}


def collect_findings(directory):
    """Extract explicit failures only; do not infer success from absent findings."""
    findings = []
    for path in sorted(directory.rglob('*.json')):
        if path.name not in ('result.json','session-results.json','audit-verdict.json','render-matrix.json','native-pe.json'):
            continue
        try:
            if path.stat().st_size > 2*1024*1024: continue
            data = json.loads(path.read_text(encoding='utf-8'))
            rows = data if isinstance(data,list) else [data]
            for row in rows:
                if not isinstance(row,dict): continue
                if row.get('error'): findings.append({'file':str(path),'detail':str(row['error'])})
                checks = row.get('checks',[])
                for check in checks if isinstance(checks,list) else []:
                    if isinstance(check,dict) and (check.get('pass') is False or check.get('outcome')=='failed'):
                        findings.append({'file':str(path),'detail':str(check.get('name','Unnamed check'))})
                violations = row.get('violations',[])
                for violation in violations if isinstance(violations,list) else []:
                    findings.append({'file':str(path),'detail':str(violation)})
                if row.get('passed') is False:
                    findings.append({'file':str(path),'detail':str(row.get('case',row.get('path','Failed result')))})
        except (OSError,ValueError) as error:
            findings.append({'file':str(path),'detail':'Unreadable result: '+str(error)})
    return findings


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--audit-mode', choices=['features', 'errors', 'both'], default='features')
    parser.add_argument('--reuse-runtime', type=Path, help='Reference saved runtime evidence without rerunning it; freshness remains explicit')
    args = parser.parse_args()
    root, out = args.root.resolve(), args.output.resolve()
    if out == root or root in out.parents:
        raise SystemExit('Audit output must be outside the engine repository')
    out.mkdir(parents=True, exist_ok=True)
    if (out/'audit-summary.json').exists():
        raise SystemExit('Existing audit output cannot be overwritten')
    def progress(percent, message):
        write_progress(out,percent,message)
    records, evidence = [], []
    # Feature-only refreshes retain the latest runtime observations for this
    # repository instead of silently reverting to older in-repository evidence.
    if args.audit_mode == 'features' and not args.reuse_runtime:
        candidates = []
        for candidate in out.parent.glob('*/audit-summary.json'):
            if candidate.parent == out: continue
            try:
                previous_summary = json.loads(candidate.read_text(encoding='utf-8'))
                identity = previous_summary.get('root')
                if not identity and (candidate.parent/'run-state.json').is_file():
                    identity = json.loads((candidate.parent/'run-state.json').read_text(encoding='utf-8')).get('root')
                if identity and Path(identity).resolve() == root and previous_summary.get('runtimeExecuted') and (candidate.parent/'runtime-evidence.json').is_file():
                    candidates.append(candidate)
            except (OSError, ValueError, TypeError):
                continue
        if candidates: args.reuse_runtime = max(candidates,key=lambda p:p.stat().st_mtime_ns).parent
    if args.reuse_runtime:
        if args.audit_mode != 'features': parser.error('--reuse-runtime requires features mode')
        previous = args.reuse_runtime.resolve()
        saved_summary = json.loads((previous/'audit-summary.json').read_text(encoding='utf-8'))
        records = saved_summary['stages']
        evidence = json.loads((previous/'runtime-evidence.json').read_text(encoding='utf-8'))
        write(out/'reused-runtime-provenance.json', {'source':str(previous),
            'sha256':{name:hashlib.sha256((previous/name).read_bytes()).hexdigest() for name in ('audit-summary.json','runtime-evidence.json')},
            'scope':'Saved observations, not new test executions or proof of current-source equivalence'})
    build = root/'out/audit-engine'
    # Serialize access to the reusable build tree across desktop windows.
    mutex = None
    if os.name == 'nt':
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p]
        kernel.CreateMutexW.restype = ctypes.c_void_p
        mutex = kernel.CreateMutexW(None, False, 'Local\\StarlightAuditEngine-'+hashlib.sha256(str(root).encode()).hexdigest()[:16])
        if not mutex: raise OSError('Cannot create audit lock')
        if ctypes.get_last_error() == 183:
            progress(-1, 'Another Starlight audit is already running. This run did not start.')
            raise SystemExit(2)
    sources = source_paths(root)
    initial_hashes = source_hashes(root, sources)
    write(out/'execution-source-hashes.json', initial_hashes)
    if args.audit_mode != 'features':
        progress(1, 'Building isolated audit binaries; the normal editor build is preserved.')
        result = execute([sys.executable, str(root/'Build-Native.py'), '--build-dir', str(build)], root,
                         out/'build.log', 1200, lambda age: progress(2, f'Building audit binaries — {age}s; details in build.log'))
        result['name'] = 'build'; records.append(result)
        if source_hashes(root, sources) != initial_hashes:
            result['outcome'] = 'invalid: source changed during build'
        if result['outcome'] == 'pass':
            try:
                discovery = subprocess.run(['ctest', '--test-dir', str(build), '-C', 'Release', '--show-only=json-v1'],
                                           capture_output=True, text=True, timeout=30)
                if discovery.returncode: raise ValueError(discovery.stderr)
                tests = json.loads(discovery.stdout)['tests']
                if not tests: raise ValueError('No registered checks discovered')
            except (OSError, subprocess.TimeoutExpired, ValueError, KeyError) as error:
                tests = []; records.append({'name': 'test-discovery', 'outcome': 'fail', 'error': str(error)})
            write(out/'discovered-tests.json', tests)
            for i, test in enumerate(tests):
                name = test['name']; log = out/f'test-{i}.log'
                if name == 'native-session':
                    records.append({'name':name,'outcome':'delegated','reason':'Executed by Gate 0 lifecycle-traced with diagnostic controls; not run twice'})
                    continue
                stage = out/f'test-{i}'; stage.mkdir()
                command = test_command(test, stage)
                executable = Path(test.get('command', [''])[0])
                if name == 'native-adventure-export':
                    executable = Path(command[command.index('--runtime')+1])/'Starlight.NativePlayer.exe'
                before = feature_evidence.capture_record(executable, root, sources, FEATURE_BINDINGS.get(name, ['diagnostics-and-existing-audits']),
                    'unknown', name) if executable.is_file() else None
                result = execute(command, root, log, 270,
                    lambda age, n=name, pos=i: progress(5+int(45*pos/max(1,len(tests))), f'Error check: {n} — {age}s (deadline 270s)'))
                result['name'] = name; records.append(result)
                if name not in FEATURE_BINDINGS:
                    result['coverageWarning'] = 'New test discovered; feature binding and artifact redirection require review'
                write(out/'stages.partial.json', records)
                if before:
                    after = feature_evidence.capture_record(executable, root, sources, before['features'], 'unknown', name)
                    before['outcome'] = result['outcome'] if before['sourceHashes']==after['sourceHashes'] and before['build']==after['build'] else 'invalid: inputs changed'
                    result['outcome'] = before['outcome']
                    before['sourceBuildRelationship'] = 'Built before checks; full transitive/reproducible provenance not established'
                    evidence.append({'path': str(out/f'test-{i}.feature-evidence.json'), 'data': before})
                    write(out/f'test-{i}.feature-evidence.json', before)
            native = build/'Release/audit/starlight_audit_tool.exe'
            runtime = out/'native-diagnostics'
            native_record = feature_evidence.capture_record(native, root, sources,
                ['native-rendering-contract','render-ownership-and-updates','diagnostics-and-existing-audits'],
                'unknown', 'Gate 0 bounded lifecycle, debug tiers, measurements and visible stages')
            result = execute([str(native), str(root), str(runtime), '--gate0', '--batch'], root,
                out/'native-diagnostics.log', 1200,
                lambda age: progress(55, native_progress(runtime, age)))
            result['name'] = 'native-render-lifecycle-visible'; records.append(result)
            try:
                verdict=json.loads((runtime/'audit-verdict.json').read_text(encoding='utf-8'))
                traced=next((c for c in verdict.get('checks',[]) if c.get('name')=='lifecycle-traced'),{})
                for record in records:
                    if record.get('outcome')=='delegated':
                        record['outcome']='pass' if traced.get('outcome')=='bounded-check-passed' else 'fail'
                        record['evidence']=str(runtime/'lifecycle-traced')
            except (OSError,ValueError):
                for record in records:
                    if record.get('outcome')=='delegated': record['outcome']='not-run'
            write(out/'stages.partial.json',records)
            native_record['outcome'] = result['outcome']
            native_after = feature_evidence.capture_record(native, root, sources, native_record['features'], 'unknown', native_record['workload'])
            if native_after['sourceHashes'] != native_record['sourceHashes'] or native_after['build'] != native_record['build']:
                native_record['outcome'] = 'invalid: inputs changed during execution'
                result['outcome'] = native_record['outcome']
            native_record['sourceBuildRelationship'] = 'Build invoked by this run; external/transitive reproducibility not certified'
            write(out/'native.feature-evidence.json', native_record)
            evidence.append({'path': str(out/'native.feature-evidence.json'), 'data': native_record})
            for path in sorted(runtime.rglob('*.json')):
                try:
                    if path.stat().st_size > 2*1024*1024: raise ValueError('Too large to interpret')
                    raw = path.read_bytes()
                    evidence.append({'path': str(path), 'sha256': hashlib.sha256(raw).hexdigest(), 'data': json.loads(raw)})
                except (OSError, ValueError) as error:
                    evidence.append({'path': str(path), 'error': str(error)})
            # Distinct image-quality/fallback coverage; not another FPS benchmark.
            for name, script, packet, deadline in (
                ('renderer-fallbacks', 'validate_native.py', root/'audit-runs/run-20260917-192850/comparison.bin', 800),
                ('denoiser-quality', 'check_denoise.py', root/'editor/checks/denoise/regression/chaos-rt.packet', 600)):
                destination = out/name
                if not packet.is_file():
                    records.append({'name':name, 'outcome':'not-run', 'reason':'Required diagnostic scene packet is missing', 'packet':str(packet)})
                    continue
                packet_hash = hashlib.sha256(packet.read_bytes()).hexdigest()
                binary = build/'Release/Starlight.Viewport.dll'
                linked = feature_evidence.capture_record(binary, root, sources,
                    ['native-rendering-contract'], 'unknown', name)
                stage_result = execute([sys.executable, str(root/'tools/render'/script),
                    '--build-dir', str(build), '--packet', str(packet), '--output', str(destination)],
                    root, out/(name+'.log'), deadline,
                    lambda age, n=name: progress(60, f'{n} — {age}s; diagnostic workload, not a performance comparison'))
                stage_result.update(name=name, packet=str(packet), packetSha256=packet_hash)
                after = feature_evidence.capture_record(binary, root, sources, linked['features'], 'unknown', name)
                if linked['build'] != after['build'] or linked['sourceHashes'] != after['sourceHashes'] or hashlib.sha256(packet.read_bytes()).hexdigest() != packet_hash:
                    stage_result['outcome'] = 'invalid: inputs changed'
                linked.update(outcome=stage_result['outcome'], workload={'name':name,'packet':str(packet),'sha256':packet_hash})
                records.append(stage_result)
                write(out/(name+'.feature-evidence.json'), linked)
                evidence.append({'path':str(out/(name+'.feature-evidence.json')), 'data':linked})
                write(out/'stages.partial.json', records)
        else:
            records.append({'name': 'runtime-checks', 'outcome': 'not-run', 'reason': 'Build did not succeed'})
    summary = {'root':str(root), 'mode': args.audit_mode, 'stages': records, 'runtimeExecuted': args.audit_mode != 'features' and any(r['name'] not in ('build','runtime-checks','test-discovery') for r in records),
               'reusedRuntime':str(args.reuse_runtime.resolve()) if args.reuse_runtime else None,
               'allRequestedStagesPassed': bool(records) and all(r['outcome']=='pass' for r in records),
               'limits': 'Bounded checks only. Not all features, visual states, hardware or long-soak conditions are tested. Gate 0 certification remains separate.'}
    write(out/'audit-summary.json', summary)
    write(out/'runtime-evidence.json', evidence)
    findings = collect_findings(args.reuse_runtime.resolve() if args.reuse_runtime else out)
    write(out/'audit-findings.json', findings)
    if args.audit_mode != 'errors':
        progress(65, 'Generating source and feature report with this run’s runtime evidence.')
        command = [sys.executable, str(Path(__file__).with_name('deep_analysis.py')), '--root', str(root), '--output', str(out),
                   '--extra-evidence', str(out/'runtime-evidence.json')]
        result = subprocess.run(command, cwd=root)
        if result.returncode:
            records.append({'name':'feature-report-generation','outcome':'fail','error':'See failure.txt; runtime evidence is preserved'})
            summary['allRequestedStagesPassed'] = False
            write(out/'audit-summary.json', summary)
    lines = ['# Starlight audit — shareable result', '', 'Mode: '+args.audit_mode, '', summary['limits'], '',
             'Feature inventory alone does not run tests. Failures and not-run checks are not passes.', '']
    for row in records:
        lines += ['- '+row['name']+': **'+row['outcome']+'** — '+str(row.get('error',row.get('reason','')))]
        lines += ['  Evidence: '+str(out/(('build.log') if row['name']=='build' else 'stages.partial.json'))]
    if not records: lines += ['No runtime checks requested.']
    if args.reuse_runtime: lines += ['', 'Runtime results below are preserved from '+str(args.reuse_runtime.resolve())+'; tests were not rerun in this report pass.']
    lines += ['', '## Explicit findings', '']
    for finding in findings: lines += ['- '+finding['detail']+' — '+finding['file']]
    if not findings: lines += ['No explicit findings extracted; inspect stage failures and coverage. This is not an error-free certification.']
    lines += ['', '## Coverage still outside this standard pass', '',
              '- Specialized denoiser and fallback checks run only in error/combined mode; missing fixture inputs are reported as not-run.',
              '- Legacy-versus-native comparative profiling is not part of this standard Gate 0 pass.',
              '- Human GUI appearance acceptance, all nested menus/DPI, long-soak leak freedom and all future features are not certified.']
    for name in ('GENERATED-FEATURE-BRIEFING.md','ENGINE-COMPARISON.md'):
        if (out/name).exists(): lines += ['', (out/name).read_text(encoding='utf-8')]
    if args.audit_mode != 'errors':
        for name in ('STARLIGHT-PRODUCT-DIRECTION.md','STARLIGHT-SCULPTING-FEASIBILITY-2026-09-18.md'):
            if (root/name).exists(): lines += ['', '# Planned scope — not implementation proof', '', (root/name).read_text(encoding='utf-8')]
    (out/'AUDIT-SHAREABLE-REPORT.md').write_text('\n'.join(lines), encoding='utf-8')
    if args.audit_mode == 'errors':
        (out/'STARLIGHT-START-HERE.md').write_text('# Error-check audit\n\nSee [shareable report](AUDIT-SHAREABLE-REPORT.md).\n', encoding='utf-8')
    # Preserve the analyzer seal and separately seal the final combined deliverables.
    write(out/'audit-deliverable-hashes.json', {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
        for p in out.iterdir() if p.is_file() and p.name in ('AUDIT-SHAREABLE-REPORT.md','audit-summary.json','runtime-evidence.json')})
    progress(100, 'Completed — report saved. Review failed, unavailable and untested areas; completion is not certification.')
    return 0 if not records or summary['allRequestedStagesPassed'] else 4


if __name__ == '__main__':
    raise SystemExit(main())
