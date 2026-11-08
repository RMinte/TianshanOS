#!/usr/bin/env python3
"""Reject a release whose tag, built firmware and checked-in notes disagree."""
import re
import hashlib
import json
import struct
import sys
from pathlib import Path


def validate(tag, build_version, root=Path('.')):
    if not re.fullmatch(r'v\d+\.\d+\.\d+(?:-[A-Za-z0-9.]+)?', tag):
        raise ValueError('Invalid release tag')
    expected = (root / 'version.txt').read_text().strip()
    if tag != f'v{expected}' or build_version.split('+', 1)[0] != expected:
        raise ValueError('Tag, version.txt and compiled firmware version must match')
    notes = root / 'docs' / 'releases' / f'{tag}.md'
    text = notes.read_text()
    if len(re.findall(r'[\u4e00-\u9fff]', text)) < 100:
        raise ValueError('Release notes must include a detailed Chinese description')
    return notes


def firmware_identity(binary):
    """Read the IDF app descriptor in the first ESP image data segment.

    This verifies build identity, not publisher signatures or device state.
    """
    data = binary.read_bytes()
    if len(data) < 112 or data[0] != 0xE9 or struct.unpack_from('<I', data, 32)[0] != 0xABCD5432:
        raise ValueError('Missing ESP application descriptor')
    def field(offset):
        value = data[offset:offset+32].split(b'\0', 1)[0]
        return value.decode('ascii')
    return {'project_name': field(80), 'version': field(48),
            'sha256': hashlib.sha256(data).hexdigest()}


def seal_security_artifact(build_dir):
    """Bind CMake's non-secret build policy to the actual binary before upload."""
    build_dir = Path(build_dir)
    profile = json.loads((build_dir/'security-build.json').read_text())
    expected = {'schema', 'development', 'recovery', 'webui_enabled', 'auth_required', 'security_enabled', 'webui_tls'}
    if set(profile) != expected or type(profile['schema']) is not int or profile['schema'] != 2:
        raise ValueError('Missing or unsupported build policy')
    if any(type(profile[key]) is not bool for key in expected-{'schema'}):
        raise ValueError('Build policy booleans must be explicit')
    name = 'TianShanOS_Dev' if profile['development'] else 'TianShanOS'
    binary_name = name+'.bin'
    identity = firmware_identity(build_dir/binary_name)
    if identity['project_name'] != name:
        raise ValueError('Actual firmware project differs from build policy')
    artifact = {'schema': 1, 'binary': binary_name, 'profile': profile, **identity}
    (build_dir/'security-artifact.json').write_text(json.dumps(artifact, indent=2)+'\n')
    return artifact


def validate_security_artifacts(build_dir, *, recovery=False):
    build_dir = Path(build_dir)
    artifact = json.loads((build_dir/'security-artifact.json').read_text())
    if type(artifact.get('schema')) is not int or artifact.get('schema') != 1 or artifact.get('binary') != 'TianShanOS.bin':
        raise ValueError('Only standard firmware is eligible for release')
    profile = artifact.get('profile', {})
    wanted = {'schema': 2, 'development': False, 'recovery': False,
              'webui_enabled': True, 'auth_required': True, 'security_enabled': True, 'webui_tls': False}
    if recovery:
        wanted.update(recovery=True, webui_enabled=False, auth_required=False, security_enabled=False, webui_tls=False)
    if set(profile) != set(wanted) or any(type(profile[k]) is not type(v) or profile[k] != v for k, v in wanted.items()):
        raise ValueError('Release requires standard WebUI authentication/security with the supported HTTP transport; dedicated recovery policy is separate')
    identity = firmware_identity(build_dir/'TianShanOS.bin')
    if identity['project_name'] != 'TianShanOS' or any(artifact.get(k) != v for k, v in identity.items()):
        raise ValueError('Release security evidence does not match the actual firmware')
    return identity



if __name__ == '__main__':
    if sys.argv[1] == '--seal-build':
        print(json.dumps(seal_security_artifact(sys.argv[2]), indent=2))
    elif sys.argv[1] == '--check-security':
        print(json.dumps(validate_security_artifacts(sys.argv[2]), indent=2))
    else:
        print(validate(sys.argv[1], sys.argv[2]))
        validate_security_artifacts(Path(sys.argv[3]) if len(sys.argv) > 3 else Path('build'))
