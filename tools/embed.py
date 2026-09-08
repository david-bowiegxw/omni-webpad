from pathlib import Path
import gzip, sys
b=gzip.compress(Path(sys.argv[1]).read_bytes(),mtime=0)
Path(sys.argv[2]).write_text('// Generated from index.html\nstatic const unsigned char page_gz[] = {'+','.join(map(str,b))+'};\n')
print('Page gzip:',len(b),'bytes')
