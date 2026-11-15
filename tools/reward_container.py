"""Offline index upgrade; preserve every previously baked frame byte."""
import struct,zlib

def index_container(decoded):
 data=bytearray(decoded);h=list(struct.unpack_from('<16I',data))
 if h[0]!=0x45523148 or h[1] not in (4,5):raise ValueError('Unsupported reward container')
 if h[1]==5:return bytes(data)
 for i in range(h[5]):
  at=h[7]+i*16;kind,age,start,size=struct.unpack_from('<4I',data,at)
  raw=struct.unpack_from('<I',data,start)[0]
  struct.pack_into('<2I',data,at,raw,(kind<<16)|age)
 h[1]=5;h[10]=zlib.crc32(data[64:]);struct.pack_into('<16I',data,0,*h)
 return bytes(data)
