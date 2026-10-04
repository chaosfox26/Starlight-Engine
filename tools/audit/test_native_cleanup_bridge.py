"""Run established cleanup acceptance cases against the native implementation."""
import functools
import itertools
from pathlib import Path
import unittest
from unittest.mock import patch
import artifact_cleanup
import test_artifact_cleanup

PROGRAM = Path(__file__).resolve().parents[2]/'out/classic/Release/Starlight.NativeCleanup.exe'


class NativeCleanupTests(test_artifact_cleanup.CleanupTests):
    def setUp(self):
        self.assertTrue(PROGRAM.is_file(), 'Build Starlight.NativeCleanup before these tests')
        replacements = {name: functools.partial(getattr(artifact_cleanup, 'native_'+name), PROGRAM)
                        for name in ('compact_build', 'compact_report', 'retain_captures', 'final_status')}
        self.override = patch.multiple(test_artifact_cleanup, **replacements)
        self.override.start()
        self.addCleanup(self.override.stop)

    def test_summary_matches_baseline(self):
        findings = [{'detail': 'stage deadline; Fullscreen live telemetry; dynamic transform produces object motion vectors'}]
        cases = [{'mode': 'features', 'runtimeExecuted': False}, {'runtimeExecuted': True, 'stages': []}]
        for outcome, name in itertools.product(('pass', 'fail', 'timeout', 'invalid: changed', 'delegated', 'not run'),
                                               ('build', 'native-classic-ui', 'native-render-lifecycle-visible', 'denoiser-quality')):
            cases.append({'runtimeExecuted': True, 'stages': [{'name': name, 'outcome': outcome}]})
        for summary in cases:
            with self.subTest(summary=summary):
                self.assertEqual(artifact_cleanup.native_final_status(PROGRAM, summary, findings),
                                 artifact_cleanup.final_status(summary, findings))


if __name__ == '__main__':
    unittest.main()
