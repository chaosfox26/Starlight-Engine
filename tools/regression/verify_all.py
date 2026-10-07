#!/usr/bin/env python3
"""One command that runs every verification Starlight has today and prints one honest summary.

  python tools/regression/verify_all.py --repo <checkout> --build-dir <folder with the built executables> --results <folder> [--python] [--gpu]

Steps (fixed commands, never taken from a packet or from the results):
  ledger   run_ledger.py: the core suite mapped onto the fix ledger (always run; the only step the ledger tracks)
  python   ctest -R py-: the registered Python audit-tool tests            (--python)
  denoise  Starlight.NativeDenoiseChecks.exe on the recorded packet        (--gpu; uses the GPU)
  fallback Starlight.NativeFallbackChecks.exe on the recorded packet       (--gpu; uses the GPU)
Everything runs windowless and, on Windows, at idle priority. GPU steps are off unless asked for, so a run while someone is gaming does not touch the GPU.
A step that did not run is reported NOT RUN, never PASS. Exit code: 0 every requested step passed and the ledger is all VERIFIED; 1 something failed or is not
verified; 2 a requested step could not run. The summary is written to <results>/verify-<time>.json (new file each time)."""
import argparse, json, os, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
FLAGS = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
SOURCE_SUFFIXES = ('.cpp', '.c', '.cc', '.h', '.hpp', '.inl', '.inc')
SOURCE_FOLDERS = ('editor/', 'deep/', 'tools/checks/')


def run_step(name, argv, cwd, timeout, env=None):
    """Run one fixed command. Returns a record with outcome PASS, FAIL, TIMEOUT or NOT RUN (the program is missing)."""
    start = time.monotonic()
    if not os.path.exists(argv[0]) and os.path.dirname(argv[0]):
        return {'step': name, 'outcome': 'NOT RUN', 'reason': 'program not found: %s' % argv[0], 'command': argv}
    try:
        proc = subprocess.Popen(argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace',
                                env=dict(os.environ, **(env or {})), creationflags=FLAGS)
        if os.name == 'nt':
            try:
                import ctypes
                handle = ctypes.windll.kernel32.OpenProcess(0x0200, False, proc.pid)   # PROCESS_SET_INFORMATION
                ctypes.windll.kernel32.SetPriorityClass(handle, 0x40)   # IDLE_PRIORITY_CLASS
                ctypes.windll.kernel32.CloseHandle(handle)
            except Exception:
                pass
        try:
            out, _ = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, _ = proc.communicate()
            return {'step': name, 'outcome': 'TIMEOUT', 'seconds': round(time.monotonic() - start, 1), 'tail': out[-400:], 'command': argv}
    except OSError as error:
        return {'step': name, 'outcome': 'NOT RUN', 'reason': str(error), 'command': argv}
    return {'step': name, 'outcome': 'PASS' if proc.returncode == 0 else 'FAIL', 'exitCode': proc.returncode,
            'seconds': round(time.monotonic() - start, 1), 'tail': out[-400:], 'command': argv}


def stale_reason(repo, build_dir):
    """Why the built checks cannot speak for the current sources, or None. A failed or forgotten build leaves an old executable that still passes."""
    exe = os.path.join(build_dir, 'Starlight.NativeCoreChecks.exe')
    if not os.path.exists(exe):
        return None
    built = os.path.getmtime(exe)
    try:
        listed = subprocess.run(['git', '-C', repo, 'ls-files', '-z'], capture_output=True, timeout=120, creationflags=FLAGS).stdout.decode('utf-8', 'replace').split(chr(0))
    except (OSError, subprocess.SubprocessError):
        return None
    newer = []
    for name in listed:
        if name.endswith(SOURCE_SUFFIXES) and name.startswith(SOURCE_FOLDERS):
            try:
                if os.path.getmtime(os.path.join(repo, name)) > built + 1:
                    newer.append(name)
            except OSError:
                pass
    if not newer:
        return None
    return '%d source files are newer than the built checks (for example %s): rebuild first' % (len(newer), newer[0])


def suite_failure(results):
    """The ledger can report every tracked fix VERIFIED while the suite itself failed on another check; that is still a failure."""
    try:
        suite = json.load(open(os.path.join(results, 'latest.json'), encoding='utf-8')).get('suite', {})
    except (OSError, ValueError):
        return 'the ledger wrote no readable latest.json'
    if suite.get('success') is True:
        return None
    return 'the core suite did not pass: %s' % (suite.get('error') or 'a check failed')[:300]


def plan(args, stamp):
    # The core suite also contains the viewport device-failure checks (they use the GPU); without --gpu they are switched off and the ledger reports CC-07/CC-02 as not covered.
    steps = [('ledger', [sys.executable, os.path.join(HERE, 'run_ledger.py'), '--repo', args.repo, '--build-dir', args.build_dir, '--results', args.results], args.repo, 3000,
              {} if args.gpu else {'STARLIGHT_SKIP_GPU_CHECKS': '1'})]
    if args.python:
        steps.append(('python', ['ctest', '-C', 'Release', '--test-dir', os.path.dirname(args.build_dir.rstrip('\\/')), '-R', 'py-', '--timeout', '600'], args.repo, 1200, {}))
    if args.gpu:
        packet = os.path.join(args.repo, 'editor', 'checks', 'denoise', 'regression', 'chaos-rt.packet')
        gpu = os.path.join(args.results, 'verify-' + stamp)
        steps.append(('denoise', [os.path.join(args.build_dir, 'Starlight.NativeDenoiseChecks.exe'), '--packet', packet, '--output', os.path.join(gpu, 'denoise'),
                                  '--build-dir', os.path.dirname(args.build_dir.rstrip('\\/')), '--config', 'Release'], args.build_dir, 1800, {}))
        steps.append(('fallback', [os.path.join(args.build_dir, 'Starlight.NativeFallbackChecks.exe'), '--build-dir', os.path.dirname(args.build_dir.rstrip('\\/')),
                                   '--packet', packet, '--output', os.path.join(gpu, 'fallback'), '--config', 'Release'], args.build_dir, 1800, {}))
    return steps


def summarize(records):
    code = 0
    for r in records:
        if r['outcome'] == 'NOT RUN':
            code = max(code, 2)
        elif r['outcome'] != 'PASS':
            code = max(code, 1)
        elif r['step'] == 'ledger' and r.get('exitCode') != 0:
            code = max(code, 1)
    return code


def stale_reason_for_test():
    import tempfile
    with tempfile.TemporaryDirectory() as root:
        subprocess.run(['git', '-C', root, 'init', '-q'], creationflags=FLAGS)
        os.makedirs(os.path.join(root, 'editor'))
        build = os.path.join(root, 'build')
        os.makedirs(build)
        exe = os.path.join(build, 'Starlight.NativeCoreChecks.exe')
        open(exe, 'w').write('x')
        os.utime(exe, (time.time() - 100, time.time() - 100))
        source = os.path.join(root, 'editor', 'a.cpp')
        open(source, 'w').write('int a;')
        subprocess.run(['git', '-C', root, 'add', '-A'], creationflags=FLAGS)
        return stale_reason(root, build)


def self_test():
    ok = True

    def check(label, condition):
        nonlocal ok
        ok = ok and condition
        print('  %s  %s' % ('ok  ' if condition else 'FAIL', label))
    py = sys.executable
    check('a passing command is PASS', run_step('t', [py, '-c', 'pass'], None, 30)['outcome'] == 'PASS')
    check('a failing command is FAIL with its exit code', (lambda r: r['outcome'] == 'FAIL' and r['exitCode'] == 3)(run_step('t', [py, '-c', 'raise SystemExit(3)'], None, 30)))
    check('a hanging command is TIMEOUT', run_step('t', [py, '-c', 'import time;time.sleep(60)'], None, 1)['outcome'] == 'TIMEOUT')
    check('a missing program is NOT RUN, never PASS', run_step('t', [os.path.join(tempfile.gettempdir(), 'no-such-program.exe')], None, 5)['outcome'] == 'NOT RUN')
    check('output tail is captured', 'hello' in run_step('t', [py, '-c', 'print("hello")'], None, 30)['tail'])
    check('all PASS with ledger exit 0 gives 0', summarize([{'step': 'ledger', 'outcome': 'PASS', 'exitCode': 0}, {'step': 'python', 'outcome': 'PASS'}]) == 0)
    check('a failed step gives 1', summarize([{'step': 'ledger', 'outcome': 'PASS', 'exitCode': 0}, {'step': 'python', 'outcome': 'FAIL'}]) == 1)
    check('a ledger run that is not all VERIFIED gives 1 even though the process finished', summarize([{'step': 'ledger', 'outcome': 'FAIL', 'exitCode': 2}]) == 1)
    check('a stale build is reported as NOT RUN and is never PASS', (lambda r: r is not None and 'rebuild' in r)(stale_reason_for_test()))
    check('a step that could not run gives 2', summarize([{'step': 'ledger', 'outcome': 'PASS', 'exitCode': 0}, {'step': 'denoise', 'outcome': 'NOT RUN'}]) == 2)

    class A:
        repo, build_dir, results, python, gpu = 'R', 'B\\Release', 'S', False, False
    check('GPU steps are not planned unless asked', [s[0] for s in plan(A, 't')] == ['ledger'])
    A.gpu = A.python = True
    A.gpu = A.python = False
    check('without --gpu the ledger step switches the GPU device checks off', plan(A, 't')[0][4] == {'STARLIGHT_SKIP_GPU_CHECKS': '1'})
    A.gpu = A.python = True
    check('with --gpu the ledger step leaves them on', plan(A, 't')[0][4] == {})
    check('GPU and Python steps are planned when asked', [s[0] for s in plan(A, 't')] == ['ledger', 'python', 'denoise', 'fallback'])
    print('SELF-TEST PASSED' if ok else 'SELF-TEST FAILED')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo')
    ap.add_argument('--build-dir')
    ap.add_argument('--results', default=r'D:\projects\_recovery\regression-results')
    ap.add_argument('--python', action='store_true')
    ap.add_argument('--gpu', action='store_true')
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not (args.repo and args.build_dir):
        ap.error('--repo and --build-dir are required')
    os.makedirs(args.results, exist_ok=True)
    stamp = time.strftime('%Y%m%d-%H%M%S')
    records = []
    stale = stale_reason(args.repo, args.build_dir)
    if stale:
        print('NOT RUN: ' + stale)
        records.append({'step': 'build', 'outcome': 'NOT RUN', 'reason': stale})
    for name, argv, cwd, timeout, env in ([] if stale else plan(args, stamp)):
        print('running %s ...' % name, flush=True)
        record = run_step(name, argv, cwd, timeout, env)
        if name == 'ledger' and record['outcome'] == 'PASS':
            failure = suite_failure(args.results)
            if failure:
                record['outcome'] = 'FAIL'
                record['reason'] = failure
        print('  %-9s %s%s' % (name, record['outcome'], ('  (%s s)' % record['seconds'] if 'seconds' in record else '') + ('  ' + record['reason'] if record.get('reason') else '')), flush=True)
        records.append(record)
    code = summarize(records)
    summary = {'schema': 'starlight-verify-all-1', 'createdUtc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'repo': args.repo, 'buildDir': args.build_dir,
               'requested': {'python': args.python, 'gpu': args.gpu}, 'exitCode': code, 'steps': records,
               'notTracked': [] if args.python and args.gpu else [n for n, on in (('python tests', args.python), ('denoise and fallback GPU checks', args.gpu)) if not on]}
    path = os.path.join(args.results, 'verify-%s.json' % stamp)
    with open(path, 'x', encoding='utf-8') as f:
        json.dump(summary, f, indent=1)
    print('SUMMARY: %s' % ('all requested steps passed' if code == 0 else 'NEEDS ATTENTION (exit %d)' % code))
    if summary['notTracked']:
        print('NOT RUN this time (not asked for): ' + ', '.join(summary['notTracked']))
    print('written: ' + path)
    return code


if __name__ == '__main__':
    sys.exit(main())
