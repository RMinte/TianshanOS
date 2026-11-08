"""Mutable host-test outputs: shared inside one run, isolated between runs.

SDK/library paths supplied by the caller remain separate read-only inputs.
Standalone Python suites get a private directory retained for diagnosis.
"""
import os
from pathlib import Path
import tempfile


def build_dir(suite):
    root = os.environ.get("TS_TEST_BUILD_ROOT")
    path = Path(root) / suite if root else Path(tempfile.mkdtemp(prefix="ts-" + suite + "-"))
    path.mkdir(parents=True, exist_ok=True)
    return path


def sdk_library_dir(idf):
    """Build the actual SDK crypto libraries once in this run's private root."""
    import shutil
    import subprocess
    native = build_dir("sdk-mbedtls")
    if not all((native / "library" / name).exists() for name in ("libmbedcrypto.a", "libmbedx509.a")):
        cmake = shutil.which("cmake") or "/Users/massif/.espressif/tools/cmake/3.30.2/CMake.app/Contents/bin/cmake"
        env = {**os.environ, "DEVELOPER_DIR": "/Library/Developer/CommandLineTools"}
        subprocess.run([cmake, "-S", str(idf / "components/mbedtls/mbedtls"), "-B", str(native),
                        "-DENABLE_PROGRAMS=OFF", "-DENABLE_TESTING=OFF"], check=True, env=env)
        subprocess.run([cmake, "--build", str(native), "--target", "mbedcrypto", "mbedx509", "-j", "4"], check=True, env=env)
    return native
