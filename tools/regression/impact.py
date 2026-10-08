#!/usr/bin/env python3
"""Which check groups are worth running first for a change. A guide for fast feedback, never a verdict.

  python tools/regression/impact.py --repo <checkout> [--base <commit>] [--files a b ...] [--top 6]

A changed check file (tools/checks/<group>.inl) selects its own group. For any other changed source file the groups are ranked by shared identifiers: a name that
appears in a changed file and in only a few check files is strong evidence, a name used by most check files is ignored. The output is the shortest list that
covers the evidence plus the exact environment setting that runs only those groups. A change that matches nothing prints "run everything": no match is never
read as "safe". Only a full run can mark a fix VERIFIED. Reads files and runs `git diff --name-only`; changes nothing."""
import argparse, os, re, subprocess, sys

WORD = re.compile(r'[A-Za-z_][A-Za-z0-9_]{5,}')
IDENT = re.compile(r'_|[a-z][A-Z]')   # names written as code (snake_case or camelCase), not ordinary English words
SOURCE = ('.cpp', '.h', '.hpp', '.inl', '.cc', '.c')


def words(path):
    try:
        return {w for w in WORD.findall(open(path, encoding='utf-8', errors='replace').read()) if IDENT.search(w)}
    except OSError:
        return set()


def groups_of(core_checks):
    text = open(core_checks, encoding='utf-8', errors='replace').read()
    return re.findall(r'^\s*RUN_GROUP\((\w+),', text, re.M)   # a call, not the #define line


def changed_files(repo, base):
    out = subprocess.run(['git', '-C', repo, 'diff', '--name-only', base + '..HEAD'], capture_output=True, text=True,
                         creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0).stdout
    tracked = subprocess.run(['git', '-C', repo, 'diff', '--name-only'], capture_output=True, text=True,
                             creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0).stdout
    return sorted({l.strip() for l in (out + '\n' + tracked).splitlines() if l.strip()})


def body_words(checks_dir, group):
    """Words of a group's function when no <group>.inl exists because several groups share one file: from its definition to the next *_checks function."""
    start = re.compile(r'void\s+' + re.escape(group) + r'\s*\(')
    for name in sorted(os.listdir(checks_dir)):
        if not name.endswith('.inl'):
            continue
        text = open(os.path.join(checks_dir, name), encoding='utf-8', errors='replace').read()
        m = start.search(text)
        if m:
            end = re.compile(r'template\s*<[^>]*>\s*void\s+\w+_checks\s*\(').search(text, m.end())
            return {w for w in WORD.findall(text[m.start():end.start() if end else len(text)]) if IDENT.search(w)}
    return set()


def group_word_sets(repo, groups):
    checks_dir = os.path.join(repo, 'tools', 'checks')
    return {g: words(os.path.join(checks_dir, g + '.inl')) or body_words(checks_dir, g) for g in groups}


def unranked(repo, groups):
    """Groups no source could be found for, so a change can never select them."""
    return sorted(g for g, w in group_word_sets(repo, groups).items() if not w)


def rank(repo, files, groups):
    group_words = {g: w for g, w in group_word_sets(repo, groups).items() if w}
    spread = {}
    for w in set().union(*group_words.values()) if group_words else ():
        spread[w] = sum(1 for gw in group_words.values() if w in gw)
    limit = min(3, max(1, len(group_words) // 4))
    score, why = {}, {}
    for f in files:
        norm = f.replace('\\', '/')
        stem = os.path.splitext(os.path.basename(norm))[0]
        if norm.startswith('tools/checks/') and stem in group_words:
            score[stem] = score.get(stem, 0) + 1000
            why.setdefault(stem, []).append(norm + ' (the check itself)')
            continue
        if not norm.endswith(SOURCE):
            continue
        mine = words(os.path.join(repo, f))
        for g, gw in group_words.items():
            shared = [w for w in mine & gw if spread[w] <= limit]
            if shared:
                score[g] = score.get(g, 0) + sum(1.0 / spread[w] for w in shared)
                why.setdefault(g, []).append('%s shares %s' % (norm, ', '.join(sorted(shared)[:3])))
    return sorted(score.items(), key=lambda kv: -kv[1]), why


def self_test():
    import tempfile
    ok = True
    with tempfile.TemporaryDirectory() as root:
        checks = os.path.join(root, 'tools', 'checks')
        os.makedirs(checks)
        open(os.path.join(checks, 'core_checks.cpp'), 'w').write('#define RUN_GROUP(name, ...) do { } while (0)' + chr(10) + '    RUN_GROUP(alpha_checks, a());' + chr(10) + 'RUN_GROUP(beta_checks, b());' + chr(10)
                                                                 + 'RUN_GROUP(gamma_checks, c());' + chr(10) + 'RUN_GROUP(delta_checks, d());' + chr(10) + 'RUN_GROUP(ghost_checks, g());' + chr(10))
        open(os.path.join(checks, 'alpha_checks.inl'), 'w').write('void alpha_checks(){ spawn_widget_pool(); widget_count(); }')
        open(os.path.join(checks, 'beta_checks.inl'), 'w').write('void beta_checks(){ read_save_file(); widget_count(); }')
        open(os.path.join(checks, 'shared_checks.inl'), 'w').write('template<class R> void gamma_checks(R r){ load_gamma_tree(); widget_count(); }' + chr(10)
                                                                   + 'template<class R> void delta_checks(R r){ build_delta_rope(); widget_count(); }')
        open(os.path.join(root, 'engine.cpp'), 'w').write('int spawn_widget_pool(){ return widget_count(); }')
        open(os.path.join(root, 'shared.cpp'), 'w').write('int widget_count(){ return 1; }')
        open(os.path.join(root, 'unrelated.cpp'), 'w').write('int something_else(){ return 0; }')
        open(os.path.join(root, 'gamma.cpp'), 'w').write('int load_gamma_tree(){ return 1; }')
        open(os.path.join(root, 'delta.cpp'), 'w').write('int build_delta_rope(){ return 1; }')
        groups = groups_of(os.path.join(checks, 'core_checks.cpp'))
        for label, good in (('the RUN_GROUP macro definition is not a group', 'name' not in groups and len(groups) == 5),
                            ('a group with no source file is reported as unranked', unranked(root, groups) == ['ghost_checks'])):
            ok = ok and good
            print('  %s  %s -> %s' % ('ok  ' if good else 'FAIL', label, groups))
        for label, files, want in (('a distinctive name picks its group', ['engine.cpp'], ['alpha_checks']),
                                   ('a name used by every check file picks nothing', ['shared.cpp'], []),
                                   ('an unrelated file picks nothing', ['unrelated.cpp'], []),
                                   ('a group whose function sits in a shared file is ranked by its own body', ['gamma.cpp'], ['gamma_checks']),
                                   ('the next group in the same shared file is kept apart', ['delta.cpp'], ['delta_checks']),
                                   ('a changed check file selects its own group', ['tools/checks/beta_checks.inl'], ['beta_checks'])):
            got = [g for g, _ in rank(root, files, groups)[0]]
            good = got[:len(want)] == want and (want or not got)
            ok = ok and good
            print('  %s  %s -> %s' % ('ok  ' if good else 'FAIL', label, got))
    print('SELF-TEST PASSED' if ok else 'SELF-TEST FAILED')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo')
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('--base', default='ea4adf8')
    ap.add_argument('--files', nargs='*')
    ap.add_argument('--top', type=int, default=6)
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.repo:
        ap.error('--repo is required')
    groups = groups_of(os.path.join(a.repo, 'tools', 'checks', 'core_checks.cpp'))
    if not groups:
        print('run everything: no check groups found in core_checks.cpp')
        return 1
    files = a.files if a.files else changed_files(a.repo, a.base)
    ranked, why = rank(a.repo, files, groups)
    print('%d changed files, %d check groups' % (len(files), len(groups)))
    missing = unranked(a.repo, groups)
    if missing:
        print('not ranked (no check source found, so never suggested): ' + ', '.join(missing))
    if not ranked:
        print('run everything: no changed file shares a distinctive name with any check group (not a sign that the change is safe)')
        return 1
    pick = [g for g, _ in ranked[:a.top]]
    for g, s in ranked[:a.top]:
        print('  %-34s score %7.1f  %s' % (g, s, '; '.join(why[g][:2])[:150]))
    print('fast run:  STARLIGHT_CHECK_GROUPS=%s  (a filtered run exits 3 and can never mark a fix VERIFIED; finish with the full run)' % ','.join(pick))
    return 0


if __name__ == '__main__':
    sys.exit(main())
