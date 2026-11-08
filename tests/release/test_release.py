import sys
import importlib.util
import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
spec = importlib.util.spec_from_file_location('check_release', ROOT / 'tools/check_release.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ReleaseTests(unittest.TestCase):
    def test_current_notes_match_version(self):
        version = (ROOT / 'version.txt').read_text().strip()
        self.assertEqual(module.validate(f'v{version}', f'{version}+fixture.1234', ROOT).name, f'v{version}.md')

    def test_version_identity_has_no_clock_and_changes_with_source(self):
        from unittest.mock import patch
        import gen_version
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); (root / 'version.txt').write_text('0.6.5\n')
            (root / 'main').mkdir(); source = root / 'main/main.c'; source.write_text('int value = 1;\n')
            with patch.object(gen_version, 'get_git_hash', return_value=''):
                first = gen_version.generate_version(root)
                self.assertRegex(first, r'^0\.6\.5\+s[0-9a-f]{8}$')
                self.assertEqual(first, gen_version.generate_version(root))
                source.write_text('int value = 2;\n')
                self.assertNotEqual(first, gen_version.generate_version(root))
            with patch.object(gen_version, 'get_git_hash', return_value='abcdef12'), patch.object(gen_version, 'is_git_dirty', return_value=False):
                self.assertEqual(gen_version.generate_version(root), '0.6.5+abcdef12')
            with patch.object(gen_version, 'get_git_hash', return_value='abcdef12'), patch.object(gen_version, 'is_git_dirty', return_value=True):
                self.assertRegex(gen_version.generate_version(root), r'^0\.6\.5\+abcdef12d\.[0-9a-f]{8}$')

    def test_mismatch_and_missing_notes_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'version.txt').write_text('0.5.2\n')
            for tag, version in [('v0.5.1', '0.5.2'), ('v0.5.2', '0.5.1'), ('../../x', '0.5.2')]:
                with self.subTest(tag=tag, version=version), self.assertRaises(ValueError):
                    module.validate(tag, version, root)
            with self.assertRaises(FileNotFoundError):
                module.validate('v0.5.2', '0.5.2', root)
            notes = root / 'docs/releases/v0.5.2.md'
            notes.parent.mkdir(parents=True)
            notes.write_text('English-only summary')
            with self.assertRaises(ValueError):
                module.validate('v0.5.2', '0.5.2', root)

    def test_actual_workflow_release_condition(self):
        policy_spec = importlib.util.spec_from_file_location('check_profile', ROOT / 'tools/ci/check_profile.py')
        policy = importlib.util.module_from_spec(policy_spec)
        policy_spec.loader.exec_module(policy)
        cases = [
            ('RMinte/TianshanOS', 'push', 'refs/heads/main', {}, True),
            ('massif-01/TianshanOS', 'push', 'refs/heads/main', {}, False),
            ('RMinte/TianshanOS', 'pull_request', 'refs/pull/41/merge', {}, False),
            ('RMinte/TianshanOS', 'push', 'refs/heads/develop', {}, False),
            ('RMinte/TianshanOS', 'push', 'refs/tags/v0.5.2', {}, True),
            ('RMinte/TianshanOS', 'release', 'refs/tags/v0.5.2', {'action': 'published'}, True),
            ('RMinte/TianshanOS', 'workflow_dispatch', 'refs/heads/main', {}, False),
        ]
        for repo, event, ref, payload, expected in cases:
            with self.subTest(repo=repo, event=event, ref=ref):
                self.assertEqual(policy.classify(repo, event, ref, payload, ['README.md'])['publish_release'], expected)
        workflow = (ROOT / '.github/workflows/build.yml').read_text()
        jobs = workflow.split('\njobs:\n', 1)[1]
        def job(name):
            return re.search(r'^  ' + re.escape(name) + r':\n(.*?)(?=^  [\w-]+:|\Z)', jobs, re.M | re.S).group(1)
        release = job('release')
        dependencies = re.search(r'^    needs: \[(.*?)\]$', release, re.M).group(1)
        self.assertEqual(set(x.strip() for x in dependencies.split(',')), {'changes', 'build', 'ci-gate'})
        expression = re.search(r'^    if: (.+)$', release, re.M).group(1)
        self.assertEqual(re.sub(r'\s+', '', expression), "${{!cancelled()&&needs.changes.outputs.publish_release=='true'&&needs.build.result=='success'&&needs['ci-gate'].result=='success'}}")
        self.assertIn('target_commitish: ${{ needs.build.outputs.commit }}', release)
        self.assertIn('body_path: docs/releases/${{ steps.preflight.outputs.tag }}.md', release)
        self.assertIn('generate_release_notes: false', release)
        self.assertIn('make_latest: "true"', release)
        self.assertIn('overwrite_files: "true"', release)
        self.assertNotIn('skip_release', release)
        self.assertNotIn('Add build identity to the Chinese release notes', release)
        self.assertNotIn('notes.open(', release)
        self.assertNotIn('body:', release)
        self.assertIn('firmware/**/*.zip', release)
        self.assertIn('firmware/**/*.bin', release)
        self.assertIn('name: TianshanOS ${{ steps.preflight.outputs.tag }}', release)
        self.assertIn('firmware/**/flasher_args.json', release)
        self.assertIn('python3 tools/serial_upgrade.py package --build-dir build', job('build'))
        self.assertNotIn("steps.signed_package", job('build'))
        self.assertIn('python3 tools/ci/check_profile.py', job('changes'))
        gate = job('ci-gate')
        selected = re.search(r'^    needs: \[(.*?)\]$', gate, re.M).group(1)
        self.assertEqual(set(x.strip() for x in selected.split(',')), {'changes', 'web-tests', 'runtime-tests', 'build', 'web-artifacts'})
        self.assertIn('if: always()', gate)
        self.assertIn('if: ${{ always() && !cancelled() }}', gate)
        self.assertIn('if: ${{ always() && cancelled() }}', gate)
        self.assertIn('CI Gate failed: workflow cancelled at the final decision', gate)
        self.assertNotIn('paths-ignore:', workflow)

    def test_public_asset_name_and_flash_mapping_match(self):
        import json, subprocess
        workflow = (ROOT / '.github/workflows/build.yml').read_text()
        step = workflow.split('      - name: Normalize public firmware filename\n', 1)[1].split('      - name:', 1)[0]
        body = step.split("python3 - <<'PYTHON'\n", 1)[1].split('          PYTHON', 1)[0]
        code = '\n'.join(line[10:] for line in body.splitlines())
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'firmware'; root.mkdir()
            original = b'synthetic firmware bytes'
            (root / 'TianShanOS.bin').write_bytes(original)
            (root / 'www.bin').write_bytes(b'synthetic web')
            args = {'app': {'file': 'TianShanOS.bin'}, 'flash_files': {'0x20000': 'TianShanOS.bin', '0x6a0000': 'www.bin'}}
            (root / 'flasher_args.json').write_text(json.dumps(args))
            subprocess.run([sys.executable, '-c', code], cwd=folder, check=True)
            self.assertNotIn('TianShanOS.bin', {p.name for p in root.iterdir()})
            self.assertIn('TianshanOS.bin', {p.name for p in root.iterdir()})
            self.assertEqual((root / 'TianshanOS.bin').read_bytes(), original)
            actual = json.loads((root / 'flasher_args.json').read_text())
            self.assertEqual(actual['app']['file'], 'TianshanOS.bin')
            self.assertEqual(actual['flash_files'], {'0x20000': 'TianshanOS.bin', '0x6a0000': 'www.bin'})


if __name__ == '__main__':
    unittest.main()
