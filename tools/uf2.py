from pathlib import Path
import struct, sys
r=Path(__file__).resolve().parents[1]
b=Path(sys.argv[1]).read_bytes();count=(len(b)+255)//256
out=bytearray()
for i in range(count):
 p=b[i*256:(i+1)*256].ljust(256,b'\x00')
 out+=struct.pack('<8I',0x0a324655,0x9e5d5157,0x2000,0x10000000+i*256,256,i,count,0xe48bff56)+p+bytes(220)+struct.pack('<I',0x0ab16f30)
Path(sys.argv[2]).write_bytes(out)
print('UF2:',len(out),'bytes;',count,'blocks')
