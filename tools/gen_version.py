#!/usr/bin/env python3
"""
TianShanOS 版本号生成脚本

在每次构建时生成包含 git hash 和时间戳的版本字符串。
由 CMake 在构建阶段调用，确保每次构建都有唯一的版本号。

输出格式: MAJOR.MINOR.PATCH+HASH.TIMESTAMP
例如: 0.3.1+7fdcca4d.01311234
"""

import subprocess
import sys
from datetime import datetime
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
            return bool(result.stdout.strip())
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


def generate_version(repo_path: Path) -> str:
    """生成完整版本字符串"""
    version_file = repo_path / 'version.txt'
    major, minor, patch, prerelease = read_version_file(version_file)
    
    # 基础版本
    version = f"{major}.{minor}.{patch}"
    if prerelease:
        version += f"-{prerelease}"
    
    # 构建元数据: HASH[d].TIMESTAMP
    git_hash = get_git_hash(repo_path)
    dirty_flag = 'd' if is_git_dirty(repo_path) else ''
    timestamp = datetime.now().strftime('%m%d%H%M')
    
    if git_hash:
        build_meta = f"{git_hash}{dirty_flag}.{timestamp}"
    else:
        build_meta = timestamp
    
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
