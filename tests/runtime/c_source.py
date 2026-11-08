"""C source masking and balanced blocks shared by API registration fixtures."""
import re


def masked(s):
    pat = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(
        pat,
        lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0])
        if m[0].startswith(('/', '"', "'")) else m[0],
        s,
    )


def endbrace(s, start):
    depth = 0
    for i in range(start, len(s)):
        if s[i] == '{':
            depth += 1
        elif s[i] == '}':
            depth -= 1
            if not depth:
                return i + 1
    raise ValueError(start)
