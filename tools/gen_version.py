#!/usr/bin/env python3
"""
TianShanOS 版本号生成脚本

生成不包含日期/时间的版本标识：干净检出使用 Git 提交号；
未提交修改或无 Git 的源码副本使用源码指纹区分内容。
输出示例：0.6.5+7fdcca4d 或 0.6.5+s1a2b3c4d。
"""

import subprocess
import sys
import hashlib
from pathlib import Path


def get_git_hash(repo_path: Path) -> str:
    """获取 git commit 短 hash"""
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--short', 'HEAD'],
            cwd=repo_path,
            capture_output=True,
            text=True,
            timeout=5
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except Exception:
        pass
    return ''


def is_git_dirty(repo_path: Path) -> bool:
    """检查是否有未提交的修改"""
    try:
        result = subprocess.run(
            ['git', 'status', '--porcelain'],
            cwd=repo_path,
            capture_output=True,
            text=True,
            timeout=5
        )
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                path = line[3:]
                if path.startswith('build/'):
                    continue
                return True
            return False
    except Exception:
        pass
    return False


def read_version_file(version_file: Path) -> tuple:
    """读取 version.txt 并解析版本号"""
    if not version_file.exists():
        return '0', '0', '0', ''
    
    content = version_file.read_text().strip()
    
    # 解析 MAJOR.MINOR.PATCH
    import re
    match = re.match(r'^(\d+)\.(\d+)\.(\d+)', content)
    if not match:
        return '0', '0', '0', ''
    
    major, minor, patch = match.groups()
    
    # 解析预发布标识 (如 -alpha, -rc1)
    prerelease = ''
    pre_match = re.search(r'-([a-zA-Z0-9.]+)', content)
    if pre_match:
        prerelease = pre_match.group(1)
    
    return major, minor, patch, prerelease


def source_fingerprint(repo_path: Path) -> str:
    """Hash relative source paths/content, excluding clocks and local outputs."""
    files = [repo_path / name for name in ('version.txt', 'CMakeLists.txt', 'Kconfig.projbuild',
             'sdkconfig', 'sdkconfig.defaults', 'partitions.csv', 'dependencies.lock')]
    for directory in ('components', 'main', 'cmake', 'tools'):
        for path in (repo_path / directory).rglob('*'):
            parts = path.relative_to(repo_path / directory).parts
            if not path.is_file() or any(p.startswith('.') or p in ('build', '__pycache__', 'node_modules') for p in parts):
                continue
            if directory != 'tools' or path.suffix in ('.py', '.sh'):
                files.append(path)
    digest = hashlib.sha256()
    for path in sorted(p for p in files if p.is_file()):
        content = path.read_bytes()
        if path.name == 'sdkconfig':
            content = '\n'.join(sorted(line for line in content.decode().splitlines()
                       if line.startswith('CONFIG_') or line.startswith('# CONFIG_'))).encode()
        digest.update(path.relative_to(repo_path).as_posix().encode() + b'\0')
        digest.update(hashlib.sha256(content).digest())
    return digest.hexdigest()[:8]


def generate_version(repo_path: Path) -> str:
    """生成完整版本字符串"""
    version_file = repo_path / 'version.txt'
    major, minor, patch, prerelease = read_version_file(version_file)
    
    # 基础版本
    version = f"{major}.{minor}.{patch}"
    if prerelease:
        version += f"-{prerelease}"
    
    git_hash = get_git_hash(repo_path)
    if git_hash and not is_git_dirty(repo_path):
        build_meta = git_hash
    elif git_hash:
        build_meta = f"{git_hash}d.{source_fingerprint(repo_path)}"
    else:
        build_meta = 's' + source_fingerprint(repo_path)

    return f"{version}+{build_meta}"


def main():
    # 获取仓库路径（脚本所在目录的上两级）
    script_path = Path(__file__).resolve()
    repo_path = script_path.parent.parent  # tools/gen_version.py -> 项目根目录
    
    # 也可以通过参数指定
    if len(sys.argv) > 1:
        repo_path = Path(sys.argv[1])
    
    version = generate_version(repo_path)
    print(version)


if __name__ == '__main__':
    main()
