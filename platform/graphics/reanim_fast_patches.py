"""Pinned, exact zero-skew matrices and DrawTrack hidden-before-frame returns."""
from pathlib import Path


def replace_one(text, old, new, label):
    if text.count(old) != 1:
        raise RuntimeError('Pinned fast reanimation context changed: ' + label)
    return text.replace(old, new, 1)


def apply(source_root):
    path = Path(source_root) / 'PvzpLib/Reanimator.cpp'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_EXACT_REANIM_FAST_DECLARATIONS' not in text:
        text = '''// PVZH1_EXACT_REANIM_FAST_DECLARATIONS
#include <bit>
#ifdef PVZH1_FAST3_TEST
extern "C" bool pvzh1_fast3_test_enabled;
#endif
''' + text
    if 'PVZH1_EXACT_ZERO_SKEW_MATRIX' not in text:
        old = 'void Reanimation::MatrixFromTransform(const ReanimatorTransform& theTransform, SexyMatrix3& theMatrix)\n{'
        new = old + '''
    // PVZH1_EXACT_ZERO_SKEW_MATRIX: preserve signed zero, finite scale and
    // translation bits. Every nonzero angle/nonfinite field uses the original.
    const uint32_t skewX=std::bit_cast<uint32_t>(theTransform.mSkewX);
    const uint32_t skewY=std::bit_cast<uint32_t>(theTransform.mSkewY);
    const uint32_t scaleX=std::bit_cast<uint32_t>(theTransform.mScaleX);
    const uint32_t scaleY=std::bit_cast<uint32_t>(theTransform.mScaleY);
    const uint32_t transX=std::bit_cast<uint32_t>(theTransform.mTransX);
    const uint32_t transY=std::bit_cast<uint32_t>(theTransform.mTransY);
    if(
#ifdef PVZH1_FAST3_TEST
       pvzh1_fast3_test_enabled&&
#endif
       !(skewX&0x7fffffffu)&&!(skewY&0x7fffffffu)&&
       (scaleX&0x7f800000u)!=0x7f800000u&&(scaleY&0x7f800000u)!=0x7f800000u&&
       (transX&0x7f800000u)!=0x7f800000u&&(transY&0x7f800000u)!=0x7f800000u){
        theMatrix.m00=theTransform.mScaleX;
        theMatrix.m10=std::bit_cast<float>((skewX^scaleX)&0x80000000u);
        theMatrix.m20=0.0f;
        theMatrix.m01=std::bit_cast<float>((skewY^scaleY^0x80000000u)&0x80000000u);
        theMatrix.m11=theTransform.mScaleY;theMatrix.m21=0.0f;
        theMatrix.m02=theTransform.mTransX;theMatrix.m12=theTransform.mTransY;
        theMatrix.m22=1.0f;return;
    }
'''
        text = replace_one(text, old, new, 'zero-skew matrix')
    if 'PVZH1_EXACT_HIDDEN_DRAW_TRACK' not in text:
        old = 'bool Reanimation::DrawTrack(Graphics* g, int theTrackIndex, [[maybe_unused]] int theRenderGroup, PvzpTriangleGroup* theTriangleGroup, ReanimatorFrameTime* theFrameTime)\n{'
        new = old + '''
    // PVZH1_EXACT_HIDDEN_DRAW_TRACK: this is only the same false return which
    // followed GetCurrentTransform. DrawRenderGroup still handles attachments.
    // Direct/null-frame-time calls and malformed indices keep original checks.
    if(
#ifdef PVZH1_FAST3_TEST
       pvzh1_fast3_test_enabled&&
#endif
       theFrameTime&&mDefinition&&mDefinition->mTracks.tracks&&
       theTrackIndex>=0&&theTrackIndex<mDefinition->mTracks.count){
        const auto& frames=mDefinition->mTracks.tracks[theTrackIndex].mTransforms;
        if(frames.mTransforms&&frames.count==mDefinition->mTracks.tracks[0].mTransforms.count&&
           theFrameTime->mAnimFrameBeforeInt>=0&&theFrameTime->mAnimFrameBeforeInt<frames.count&&
           theFrameTime->mAnimFrameAfterInt>=0&&theFrameTime->mAnimFrameAfterInt<frames.count&&
           std::bit_cast<uint32_t>(frames.mTransforms[theFrameTime->mAnimFrameBeforeInt].mFrame)==0xbf800000u)
            return false;
    }
'''
        text = replace_one(text, old, new, 'hidden DrawTrack')
    path.write_text(text, encoding='utf8', newline='\n')
