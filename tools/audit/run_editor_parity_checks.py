"""Run editor regression checks with an external deadline and explicit result."""
import atexit
import datetime
import feature_evidence
from audit_engine import execute, source_paths
import argparse
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('editor', type=Path)
parser.add_argument('report', type=Path)
parser.add_argument('--timeout', type=float, default=240)
args = parser.parse_args()
if args.timeout <= 0:
    parser.error('timeout must be positive')
report = args.report.resolve()
report.parent.mkdir(parents=True, exist_ok=True)
if report.exists():
    parser.error('choose a new report path to preserve earlier evidence')
# Record the candidate identity before execution, never retrofit historic results.
source_root = Path(__file__).resolve().parents[2]
observed_sources = source_paths(source_root)
record = feature_evidence.capture_record(args.editor.resolve(), source_root, observed_sources,
    ['classic-creator-interface'], 'unknown', 'Native editor --ui-check; assertion/resource checks, not visual acceptance')
record['startedUtc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
execution = {'exitCode': None}
def save_feature_evidence():
    record['finishedUtc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    record['resultPath'] = str(report)
    record['processExitCode'] = execution['exitCode']
    try:
        result = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
        record['outcome'] = 'pass' if execution['exitCode'] == 0 and result.get('pass') is True else 'fail'
        current = feature_evidence.capture_record(args.editor.resolve(), source_root, observed_sources,
            record['features'], 'unknown', record['workload'])
        if current['sourceHashes'] != record['sourceHashes'] or current['build'] != record['build']:
            record['outcome'] = 'invalid: inputs changed during test'
        record['sourceBuildRelationship'] = 'Source observed during execution; rebuild provenance not established'
        report.with_suffix('.feature-evidence.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    except Exception as error:
        record['outcome'] = 'unknown'; record['recordingError'] = str(error)
        report.with_suffix('.feature-evidence.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
atexit.register(save_feature_evidence)
print(f'Editor checks started; deadline {args.timeout:g}s. Log: {report.with_suffix(".worker.log")}', flush=True)
result_execution = execute([str(args.editor.resolve()), '--ui-check', str(report)], source_root,
    report.with_suffix('.worker.log'), args.timeout, lambda age: None)
code = result_execution.get('exitCode', 124 if result_execution['outcome']=='timeout' else 1)
execution['exitCode'] = code
if result_execution['outcome']=='timeout':
    report.with_suffix('.timeout.json').write_text(json.dumps(result_execution, indent=2), encoding='utf-8')
    print('FAILED: deadline exceeded; owned process tree stopped and evidence preserved.', flush=True)
    sys.exit(124)
if not report.exists():
    print(f'FAILED: editor exited {code} without a completion report.', flush=True)
    sys.exit(code or 1)
result = json.loads(report.read_text(encoding='utf-8'))
failures = [item for item in result.get('checks', []) if not item['pass']]
print(json.dumps({'pass': code == 0 and result.get('pass') is True, 'exit_code': code,
                  'checks': len(result.get('checks', [])), 'failures': failures}), flush=True)
sys.exit(0 if code == 0 and result.get('pass') is True else 1)
