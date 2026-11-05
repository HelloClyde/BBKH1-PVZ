"""Keep reduced white font masks as exact A8, with bounded heap blocks."""
from pathlib import Path

def apply(root):
 root=Path(root)/'SexyAppFramework/graphics'
 path=root/'SWTri.h';s=path.read_text(encoding='utf8')
 assert 'PVZH1_REDUCED_A8' not in s
 needle='        const unsigned int * const *pages = nullptr;';assert s.count(needle)==1
 s=s.replace(needle,needle+'\n        const unsigned char* alpha8=nullptr; // PVZH1_REDUCED_A8\n        const unsigned char* const* alphaPages=nullptr;\n        const unsigned char* const* alphaTiles=nullptr;\n        unsigned tileColumns=0;\n        unsigned ReadTile(unsigned x,unsigned y)const{const auto* p=alphaTiles[(y>>4)*tileColumns+(x>>4)];return 0xffffffu|(unsigned(p?p[(y&15)*16+(x&15)]:0)<<24);}')
 needle='return pTexture[index];';assert s.count(needle)==2
 fetch='if(alphaTiles)return ReadTile(index%unsigned(pitch),index/unsigned(pitch));\n            if(pages)return pages[index>>10u][index&1023u];\n            return alphaPages?(0xffffffu|(unsigned(alphaPages[index>>10u][index&1023u])<<24)):(alpha8?(0xffffffu|(unsigned(alpha8[index])<<24)):pTexture[index]);'
 # The ordinary Read method is also available in non-H1 builds.
 s=s.replace(needle,'#ifdef H1_PVZ_PORTABLE\n            '+fetch+'\n#else\n            return pTexture[index];\n#endif')
 needle='return pTexture[(unsigned int)y*(unsigned int)pitch+(unsigned int)x];';assert s.count(needle)==2
 s=s.replace(needle,'const unsigned index=(unsigned)y*(unsigned)pitch+(unsigned)x;\n            '+fetch.replace('ReadTile(index%unsigned(pitch),index/unsigned(pitch))','ReadTile(unsigned(x),unsigned(y))'))
 path.write_text(s,encoding='utf8',newline='\n')
 path=root/'SWTri.cpp';s=path.read_text(encoding='utf8')
 needle='if(textured)storageTexture=Sexy::H1ImageCachePrepareStorageTexture(theImage,&textureInfo.pTexture,&textureWidth,&textureHeight);';assert s.count(needle)==1
 s=s.replace(needle,'float tileU[3],tileV[3];for(int i=0;i<3;++i){tileU[i]=aTriRef[i]->mU;tileV[i]=aTriRef[i]->mV;}\n            if(textured)storageTexture=Sexy::H1ImageCachePrepareStorageFontTiles(theImage,tileU,tileV,&textureInfo.alphaTiles,&textureInfo.tileColumns,&textureWidth,&textureHeight)||Sexy::H1ImageCachePrepareStorageRGBPages(theImage,&textureInfo.pages,&textureWidth,&textureHeight)||Sexy::H1ImageCachePrepareStorageAlphaPages(theImage,&textureInfo.alphaPages,&textureWidth,&textureHeight)||Sexy::H1ImageCachePrepareStorageAlpha(theImage,&textureInfo.alpha8,&textureWidth,&textureHeight)||Sexy::H1ImageCachePrepareStorageTexture(theImage,&textureInfo.pTexture,&textureWidth,&textureHeight);')
 path.write_text(s,encoding='utf8',newline='\n')
