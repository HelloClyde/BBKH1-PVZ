"""Keep reduced white font masks as A8 in the original triangle sampler."""
from pathlib import Path
def apply(root):
 root=Path(root)/'SexyAppFramework/graphics'
 path=root/'SWTri.h';s=path.read_text(encoding='utf8')
 assert 'PVZH1_REDUCED_A8' not in s
 needle='        const unsigned int * const *pages = nullptr;';assert s.count(needle)==1
 s=s.replace(needle,needle+'\n        const unsigned char* alpha8=nullptr; // PVZH1_REDUCED_A8')
 needle='return pTexture[index];';assert s.count(needle)==2
 s=s.replace(needle,'return alpha8?(0xffffffu|(unsigned(alpha8[index])<<24)):pTexture[index];')
 needle='return pTexture[(unsigned int)y*(unsigned int)pitch+(unsigned int)x];';assert s.count(needle)==2
 s=s.replace(needle,'const unsigned index=(unsigned)y*(unsigned)pitch+(unsigned)x;\n            return alpha8?(0xffffffu|(unsigned(alpha8[index])<<24)):pTexture[index];')
 path.write_text(s,encoding='utf8',newline='\n')
 path=root/'SWTri.cpp';s=path.read_text(encoding='utf8')
 needle='if(textured)storageTexture=Sexy::H1ImageCachePrepareStorageTexture(theImage,&textureInfo.pTexture,&textureWidth,&textureHeight);';assert s.count(needle)==1
 s=s.replace(needle,'if(textured)storageTexture=Sexy::H1ImageCachePrepareStorageAlpha(theImage,&textureInfo.alpha8,&textureWidth,&textureHeight)||Sexy::H1ImageCachePrepareStorageTexture(theImage,&textureInfo.pTexture,&textureWidth,&textureHeight);')
 path.write_text(s,encoding='utf8',newline='\n')
