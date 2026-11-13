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
    if 'PVZH1_CONSTANT_TRANSFORM' not in text:
        text='#include "graphics/ConstantTransform.h"\n'+text
        first='\ttheTransform->mTransX = FloatLerp(aTransBefore.mTransX, aTransAfter.mTransX, theFrameTime->mFraction);'
        last='\ttheTransform->mAlpha = FloatLerp(aTransBefore.mAlpha, aTransAfter.mAlpha, theFrameTime->mFraction);'
        assert text.count(first)==text.count(last)==1
        text=text.replace(first,'''    // PVZH1_CONSTANT_TRANSFORM: equal finite endpoints have the same exact
    // result, including canonical positive zero, without seven soft FP lerps.
    if(!(
#ifdef PVZH1_FAST3_TEST
       pvzh1_fast3_test_enabled&&
#endif
       H1ConstantTransform(aTransBefore,aTransAfter,theFrameTime->mFraction,*theTransform))){
'''+first).replace(last,last+'\n    }')
    if 'PVZH1_BASE_INVERSE_MEMO' not in text:
        start=text.index('void Reanimation::GetAttachmentOverlayMatrix(');end=text.index('\nvoid Reanimation::GetFramesForLayer',start)
        section=text[start:end]
        first='\tSexyTransform2D aBasePoseMatrix;'
        last='\tSexyMatrix3Inverse(aBasePoseMatrix, aBasePoseMatrixInv);'
        a=section.index(first);b=section.index(last,a)+len(last)
        section=section[:a]+"""    // PVZH1_BASE_INVERSE_MEMO: definitions and base-frame numbers are immutable
    // inputs; all current-frame and overlay calculations remain original.
    SexyTransform2D aBasePoseMatrixInv;
    if(!H1ActorBaseInverse(this,theTrackIndex,aBasePoseMatrixInv)){
        SexyTransform2D aBasePoseMatrix;GetTrackBasePoseMatrix(theTrackIndex,aBasePoseMatrix);
        SexyMatrix3Inverse(aBasePoseMatrix,aBasePoseMatrixInv);
        H1ActorBaseInverse(this,theTrackIndex,aBasePoseMatrixInv,true);
    }"""+section[b:]
        text=text[:start]+section+text[end:]
    if 'PVZH1_ANIM_INCREMENT_MEMO' not in text:
        text=replace_one(text,
            'mAnimTime += SECONDS_PER_UPDATE * mAnimRate / mFrameCount;',
            'mAnimTime += H1ActorAnimIncrement(this); // PVZH1_ANIM_INCREMENT_MEMO: same double increment until rate/count changes',
            'animation double increment')
    if 'PVZH1_PRESENT_RENDER_GROUPS' not in text:
        old='void Reanimation::DrawAllRenderGroups(Graphics* g)\n{'
        text=replace_one(text,old,old+'''
    // PVZH1_PRESENT_RENDER_GROUPS: absent groups draw neither tracks nor
    // attachments. Collect current groups once, preserving ascending order.
    if(mDefinition&&mTrackInstances){
        if(mDead)return;
        uint32_t groups=0;
        for(int i=0;i<mDefinition->mTracks.count;++i){
            const int group=mTrackInstances[i].mRenderGroup;
            if(group>=RENDER_GROUP_NORMAL&&group<=RENDER_GROUP_MAX)groups|=1u<<group;
        }
        for(int group=RENDER_GROUP_NORMAL;group<=RENDER_GROUP_MAX;++group)
            if(groups&(1u<<group))DrawRenderGroup(g,group);
        return;
    }
''','present render groups')
    if 'PVZH1_FIXED_ATTACHER_TAG' not in text:
        text='#include "graphics/ReanimTrack.h"\n'+text
        text=replace_one(text,
            'strncasecmp(mDefinition->mTracks.tracks[aTrackIndex].mName, "attacher__", 10) == 0',
            'H1IsAttacherTrack(mDefinition->mTracks.tracks[aTrackIndex].mName) /* PVZH1_FIXED_ATTACHER_TAG */',
            'fixed attacher tag')
    if 'PVZH1_EXACT_TRIG_MEMO' not in text:
        text='#include "graphics/ReanimTrig.h"\n'+text
        old='''\ttheMatrix.m00 = cos(aSkewX) * theTransform.mScaleX;
\ttheMatrix.m10 = -sin(aSkewX) * theTransform.mScaleX;
\ttheMatrix.m20 = 0.0f;
\ttheMatrix.m01 = sin(aSkewY) * theTransform.mScaleY;
\ttheMatrix.m11 = cos(aSkewY) * theTransform.mScaleY;'''
        new='''    // PVZH1_EXACT_TRIG_MEMO: cache only exact angle bits/libm results.
    // Scales, translations, interpolation and multiplication order stay original.
    float cosineX,sineX,cosineY,sineY;
    H1ReanimTrig(aSkewX,cosineX,sineX);H1ReanimTrig(aSkewY,cosineY,sineY);
\ttheMatrix.m00 = cosineX * theTransform.mScaleX;
\ttheMatrix.m10 = -sineX * theTransform.mScaleX;
\ttheMatrix.m20 = 0.0f;
\ttheMatrix.m01 = sineY * theTransform.mScaleY;
\ttheMatrix.m11 = cosineY * theTransform.mScaleY;'''
        text=replace_one(text,old,new,'exact repeated-angle libm results')
    path.write_text(text, encoding='utf8', newline='\n')
    target=Path(source_root)/'PvzpLib/Reanimator.cpp'
    source=target.read_text(encoding='utf8')
    if 'PVZH1_EXACT_FRAME_TIME_MEMO' not in source:
        entry='void Reanimation::GetFrameTime(ReanimatorFrameTime* theFrameTime)\n{'
        source=replace_one(source,entry,entry+'\n    // PVZH1_EXACT_FRAME_TIME_MEMO: animation clock and events still update normally.\n    if(H1ActorFrameTime(this,*theFrameTime))return;','frame time lookup')
        tail='\tPVZP_ASSERT(theFrameTime->mAnimFrameBeforeInt >= 0 && theFrameTime->mAnimFrameAfterInt < mDefinition->mTracks.tracks[0].mTransforms.count);'
        source=replace_one(source,tail,tail+'\n    H1ActorFrameTime(this,*theFrameTime,true);','frame time publication')
        target.write_text(source,encoding='utf8',newline='\n')
    target=Path(source_root)/'PvzpLib/Reanimator.cpp'
    source=target.read_text(encoding='utf8')
    if 'PVZH1_ACTIVE_UPDATE_TRACKS' not in source:
        start=source.index('void Reanimation::Update()\n{');end=source.index('\n}\n',start)+3
        body=source[start:end]
        loop='for (int aTrackIndex = 0; aTrackIndex < mDefinition->mTracks.count; aTrackIndex++)'
        body=replace_one(body,loop,'for (int aTrackIndex=H1ActorNextActiveTrack(this,-1);aTrackIndex>=0;aTrackIndex=H1ActorNextActiveTrack(this,aTrackIndex)) /* PVZH1_ACTIVE_UPDATE_TRACKS */','active update tracks')
        source=source[:start]+body+source[end:]
        start=source.index('void Reanimation::PropogateColorToAttachments()\n{');end=source.index('\n}\n',start)+3
        body=source[start:end]
        body=replace_one(body,'for (int i = 0; i < mDefinition->mTracks.count; i++)','for (int i=H1ActorNextActiveTrack(this,-1);i>=0;i=H1ActorNextActiveTrack(this,i))\n        if(mTrackInstances[i].mAttachmentID!=AttachmentID::ATTACHMENTID_NULL)','active color propagation tracks')
        source=source[:start]+body+source[end:]
        for entry in ['void Reanimation::StartBlend(int theBlendTime)\n{','void Reanimation::SetShakeOverride(const char* theTrackName, float theShakeAmount)\n{']:
            source=replace_one(source,entry,entry+'\n    H1ActorTrackStateChanged(this);','active track mutation')
        target.write_text(source,encoding='utf8',newline='\n')
        target=Path(source_root)/'PvzpLib/Attachment.cpp';source=target.read_text(encoding='utf8')
        entry='AttachEffect* CreateEffectAttachment(AttachmentID& theAttachmentID, EffectType theEffectType, unsigned int theDataID, float theOffsetX, float theOffsetY)\n{'
        source='#include "actors/ActorPose.h"\n'+replace_one(source,entry,entry+'\n    H1ActorAttachmentsChanged(&theAttachmentID); // Only this referenced track can become active.','active attachment mutation')
        target.write_text(source,encoding='utf8',newline='\n')
    for relative,entry,body in (
        ('PvzpLib/PvzpCommon.cpp',
         'void SexyMatrix3Multiply(SexyMatrix3& m, const SexyMatrix3& l, const SexyMatrix3& r)\n{',
         '    if(H1MultiplyAffine(m,l,r))return;'),
        ('SexyAppFramework/misc/SexyMatrix.cpp',
         'SexyMatrix3 SexyMatrix3::operator*(const SexyMatrix3 &theMat) const\n{',
         '    SexyMatrix3 affine;if(H1MultiplyAffine(affine,*this,theMat,true))return affine;'),
    ):
        target=Path(source_root)/relative
        source=target.read_text(encoding='utf8')
        if 'PVZH1_EXACT_AFFINE_PRODUCT' not in source:
            source='#include "graphics/AffineMatrix.h"\n'+replace_one(source,entry,entry+'\n    // PVZH1_EXACT_AFFINE_PRODUCT: original non-affine/nonfinite path below.\n'+body,'affine matrix product')
            target.write_text(source,encoding='utf8',newline='\n')
    target=Path(source_root)/'PvzpLib/PvzpCommon.h'
    source=target.read_text(encoding='utf8')
    if 'PVZH1_EQUAL_LERP_ENDPOINTS' not in source:
        source='#include <bit>\n'+source
        old='inline float\t\t\tFloatLerp(float theStart, float theEnd, float theFactor)\t{ return theStart + theFactor * (theEnd - theStart); }'
        new='''inline float FloatLerp(float theStart,float theEnd,float theFactor){
    // PVZH1_EQUAL_LERP_ENDPOINTS: retain the exact original positive-zero
    // result for equal finite endpoints and a finite fraction in [0,1].
    const unsigned a=std::bit_cast<unsigned>(theStart),b=std::bit_cast<unsigned>(theEnd);
    if(a==b&&(a&0x7f800000u)!=0x7f800000u&&std::bit_cast<unsigned>(theFactor)<=0x3f800000u)
        return (a&0x7fffffffu)?theStart:0.0f;
    return theStart+theFactor*(theEnd-theStart);
}'''
        source=replace_one(source,old,new,'equal FloatLerp endpoints')
        target.write_text(source,encoding='utf8',newline='\n')

    target=Path(source_root)/'PvzpLib/Reanimator.cpp'
    source=target.read_text(encoding='utf8')
    if 'PVZH1_DEFERRED_OVERLAYS' not in source:
        source='#include "actors/DeferredOverlay.h"\n// PVZH1_DEFERRED_OVERLAYS\n'+source
        old='\t\t\tGetAttachmentOverlayMatrix(aTrackIndex, aOverlayMatrix);\n\t\t\tAttachmentUpdateAndSetMatrix(aTrack->mAttachmentID, aOverlayMatrix);'
        new='\t\t\tif(!H1DeferredAttachmentUpdate(this,aTrackIndex)){\n'+old+'\n\t\t\t}'
        source=replace_one(source,old,new,'deferred leaf attachment update')
        for entry in [
            'void Reanimation::GetTrackMatrix(int theTrackIndex, SexyTransform2D& theMatrix)\n{',
            'void Reanimation::GetAttachmentOverlayMatrix(int theTrackIndex, SexyTransform2D& theOverlayMatrix)\n{',
            'bool Reanimation::DrawTrack(Graphics* g, int theTrackIndex, [[maybe_unused]] int theRenderGroup, PvzpTriangleGroup* theTriangleGroup, ReanimatorFrameTime* theFrameTime)\n{',
            'void Reanimation::SetPosition(float theX, float theY)\n{',
            'void Reanimation::OverrideScale(float theScaleX, float theScaleY)\n{',
        ]:
            source=replace_one(source,entry,entry+'\n    H1ResolveActorOverlay(this);','materialize observed overlay')
        target.write_text(source,encoding='utf8',newline='\n')
        target=Path(source_root)/'PvzpLib/Attachment.cpp';source=target.read_text(encoding='utf8')
        source='#include "actors/DeferredOverlay.h"\n'+replace_one(source,'\t\t\t\taReanimation->mOverlayMatrix = aPosition;','\t\t\t\tH1DropActorOverlay(aReanimation);\n\t\t\t\taReanimation->mOverlayMatrix = aPosition;','replace complete attachment matrix')
        target.write_text(source,encoding='utf8',newline='\n')
        target=Path(source_root)/'Lawn/System/SaveGame.cpp';source=target.read_text(encoding='utf8')
        entry='static void SyncReanimationPortable(Board* theBoard, Reanimation* theReanimation, PortableSaveContext& theContext)\n{'
        source='#include "actors/ActorPose.h"\n#include "actors/DeferredOverlay.h"\n'+replace_one(source,entry,entry+'\n    if(theContext.mReading)H1ActorTrackStateChanged(theReanimation);else H1ResolveActorOverlay(theReanimation);','materialize portable save matrix')
        target.write_text(source,encoding='utf8',newline='\n')

    target=Path(source_root)/'PvzpLib/Reanimator.cpp';source=target.read_text(encoding='utf8')
    if 'PVZH1_EMPTY_ATTACHMENT_PROPAGATION' not in source:
        entry='void Reanimation::PropogateColorToAttachments()\n{'
        source=replace_one(source,entry,entry+'\n    if(!H1ActorHasAttachments(this))return; // PVZH1_EMPTY_ATTACHMENT_PROPAGATION','skip empty attachment propagation')
    if 'PVZH1_UNCHANGED_ATTACHMENT_COLOR' not in source:
        entry='    if(!H1ActorHasAttachments(this))return; // PVZH1_EMPTY_ATTACHMENT_PROPAGATION'
        source=replace_one(source,entry,entry+'\n    if(H1ActorColorPropagationUnchanged(this))return; // PVZH1_UNCHANGED_ATTACHMENT_COLOR','skip unchanged attachment colors')
        target.write_text(source,encoding='utf8',newline='\n')
        target=Path(source_root)/'PvzpLib/Attachment.cpp';source=target.read_text(encoding='utf8')
        entry='void AttachmentDie(AttachmentID& theAttachmentID)\n{'
        source=replace_one(source,entry,entry+'\n    if(theAttachmentID!=ATTACHMENTID_NULL)H1ActorAttachmentsChanged(&theAttachmentID);','invalidate attachment presence on removal')
        target.write_text(source,encoding='utf8',newline='\n')

    target=Path(source_root)/'PvzpLib/PvzpCommon.cpp';source=target.read_text(encoding='utf8')
    if 'PVZH1_DIRECT_PLANT_SHADOW' not in source:
        entry='void PvzpDrawImageCelCenterScaledF(Graphics* g, Image* theImageStrip, float thePosX, float thePosY, int theCelCol, float theScaleX, float theScaleY)\n{'
        source=replace_one(source,entry,entry+'\n    if(H1PlantShadowDraw(g,theImageStrip,thePosX,thePosY,theCelCol,theScaleX,theScaleY))return; // PVZH1_DIRECT_PLANT_SHADOW','known shadow sprite without matrix setup')
        if '#include "actors/ActorPose.h"' not in source:source='#include "actors/ActorPose.h"\n'+source
        target.write_text(source,encoding='utf8',newline='\n')
