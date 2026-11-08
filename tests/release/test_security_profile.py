"""Effective CMake auth policy and actual ESP app descriptor artifact identity."""
import hashlib
import os
from unittest.mock import patch
import sys
import importlib.util
import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
spec = importlib.util.spec_from_file_location('release_security', ROOT/'tools/check_release.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SecurityProfileTests(unittest.TestCase):
    def profile(self, development=False, recovery=False, auth=True, security=True, webui=True, tls=False):
        return dict(schema=2, development=development, recovery=recovery,
                    webui_enabled=webui, auth_required=auth, security_enabled=security, webui_tls=tls)

    def image(self, path, project='TianShanOS', version='0.6.3'):
        # Image/descriptor fixture only, no claim of valid flashable firmware.
        data=bytearray(512);data[0]=0xE9;struct.pack_into('<I',data,32,0xABCD5432)
        data[48:48+len(version)]=version.encode();data[80:80+len(project)]=project.encode()
        path.write_bytes(data)

    def prepare(self, root, profile=None):
        (root/'security-build.json').write_text(json.dumps(profile or self.profile()))
        self.image(root/'TianShanOS.bin')

    def test_effective_cmake_matrix(self):
        import itertools
        for dev,recovery,auth,security in itertools.product((False,True),repeat=4):
            with self.subTest(dev=dev,recovery=recovery,auth=auth,security=security),tempfile.TemporaryDirectory() as folder:
                root=Path(folder)
                values=dict(TS_DEVELOPMENT_BUILD=dev,ROOT_PASSWORD_RESET=recovery,CONFIG_TS_WEBUI_ENABLE=True,
                            CONFIG_TS_WEBUI_AUTH_REQUIRED=auth,CONFIG_TS_SECURITY_ENABLE=security)
                commands='\n'.join('set('+name+' '+('ON' if value else 'OFF')+')' for name,value in values.items())
                script=root/'run.cmake';script.write_text(commands+'\ninclude("'+str(ROOT/'cmake/security_profile.cmake')+'")\n')
                result=subprocess.run(['cmake','-P',str(script)],cwd=root,capture_output=True,text=True)
                allowed=dev or recovery or (auth and security)
                self.assertEqual(result.returncode==0,allowed,result.stdout+result.stderr)
                if allowed:self.assertEqual(json.loads((root/'security-build.json').read_text()),self.profile(dev,recovery,auth,security))


    def test_standard_artifact_and_byte_binding(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.prepare(root);artifact=module.seal_security_artifact(root)
            self.assertEqual(module.validate_security_artifacts(root)['sha256'],hashlib.sha256((root/'TianShanOS.bin').read_bytes()).hexdigest())
            data=bytearray((root/'TianShanOS.bin').read_bytes());data[240]^=1;(root/'TianShanOS.bin').write_bytes(data)
            with self.assertRaises(ValueError):module.validate_security_artifacts(root)

    def test_unsafe_profiles_rejected_even_with_matching_bytes(self):
        for key in ('development','recovery','webui_enabled','auth_required','security_enabled','webui_tls'):
            with self.subTest(key=key),tempfile.TemporaryDirectory() as folder:
                root=Path(folder);profile=self.profile();profile[key]=not profile[key];self.prepare(root,profile)
                if key=='development':self.image(root/'TianShanOS_Dev.bin','TianShanOS_Dev')
                module.seal_security_artifact(root)
                with self.assertRaises(ValueError):module.validate_security_artifacts(root)

    def test_renamed_development_binary_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.prepare(root);self.image(root/'TianShanOS.bin','TianShanOS_Dev')
            with self.assertRaises(ValueError):module.seal_security_artifact(root)
            self.prepare(root);artifact=module.seal_security_artifact(root)
            self.image(root/'TianShanOS.bin','TianShanOS_Dev');artifact.update(module.firmware_identity(root/'TianShanOS.bin'))
            (root/'security-artifact.json').write_text(json.dumps(artifact))
            with self.assertRaises(ValueError):module.validate_security_artifacts(root)

    def test_missing_bad_or_ambiguous_evidence_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            with self.assertRaises(FileNotFoundError):module.validate_security_artifacts(root)
            self.prepare(root);profile=self.profile();profile['auth_required']=1
            (root/'security-build.json').write_text(json.dumps(profile))
            with self.assertRaises(ValueError):module.seal_security_artifact(root)
            self.prepare(root);artifact=module.seal_security_artifact(root);artifact['profile']['development']='false'
            (root/'security-artifact.json').write_text(json.dumps(artifact))
            with self.assertRaises(ValueError):module.validate_security_artifacts(root)
            (root/'TianShanOS.bin').write_bytes(b'not an ESP image')
            with self.assertRaises(ValueError):module.firmware_identity(root/'TianShanOS.bin')

    def test_actual_release_main_rejects_before_any_remote_operation(self):
        entry_spec=importlib.util.spec_from_file_location('ci_release_security',ROOT/'tools/ci/check_release_state.py')
        entry=importlib.util.module_from_spec(entry_spec);entry_spec.loader.exec_module(entry)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);self.prepare(root,self.profile(development=True,auth=False))
            self.image(root/'TianShanOS_Dev.bin','TianShanOS_Dev');module.seal_security_artifact(root)
            env=dict(GITHUB_EVENT_NAME='push',GITHUB_REF='refs/heads/main',BUILD_VERSION='0.6.3',RELEASE_ARTIFACT_DIR=str(root))
            with patch.dict(os.environ,env),patch.object(entry,'prepare_release') as remote,patch.object(entry,'advance_tag') as write:
                with self.assertRaisesRegex(ValueError,'standard firmware'):
                    entry.main()
                remote.assert_not_called();write.assert_not_called()

    def test_old_profile_evidence_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);profile=self.profile();profile['schema']=1;del profile['webui_tls']
            self.prepare(root,profile)
            with self.assertRaises(ValueError):module.seal_security_artifact(root)

    def test_actual_release_entry_enforces_before_tag_write(self):
        source=(ROOT/'tools/ci/check_release_state.py').read_text()
        main=source[source.index('def main():'):]
        self.assertLess(main.index('validate_security_artifacts('),main.index('prepare_release('))
        self.assertLess(main.index('validate_security_artifacts('),main.index('advance_tag('))
        workflow=(ROOT/'.github/workflows/build.yml').read_text()
        self.assertIn('python3 tools/check_release.py --seal-build build',workflow)
        self.assertIn('/security-artifact.json',workflow)
        self.assertIn('path: firmware',workflow)
        cmake=(ROOT/'CMakeLists.txt').read_text()
        self.assertIn('project(TianShanOS_Dev VERSION',cmake)
        self.assertIn('include(cmake/security_profile.cmake)',cmake)


if __name__ == '__main__':
    unittest.main()
