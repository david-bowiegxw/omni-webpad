"""Gzip the controller page into a C array: embed.py web/index.html out.c"""
from pathlib import Path
import gzip
import sys

data = gzip.compress(Path(sys.argv[1]).read_bytes(), compresslevel=9, mtime=0)
rows = [','.join(map(str, data[i:i + 24])) for i in range(0, len(data), 24)]
Path(sys.argv[2]).write_text(
    '// Generated from web/index.html by tools/embed.py. Do not edit.\n'
    'const unsigned char page_gz[] = {\n' + ',\n'.join(rows) + '\n};\n'
    f'const unsigned page_gz_len = {len(data)};\n')
print(f'Page: {len(data)} bytes gzip')
