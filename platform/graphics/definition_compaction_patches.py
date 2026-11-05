"""Trim XML growth arrays before any reanimation instance can borrow them."""
from pathlib import Path

def apply(source_root):
    path=Path(source_root)/'PvzpLib/Reanimator.cpp'
    text=path.read_text(encoding='utf8')
    marker='PVZH1_COMPACT_DEFINITION_ARRAYS'
    if marker in text:return
    original='''	if (!DefinitionLoadXML(theFileName, &gReanimatorDefMap, theDefinition))
		return false;

	for (int aTrackIndex = 0; aTrackIndex < theDefinition->mTracks.count; aTrackIndex++)'''
    replacement='''	if (!DefinitionLoadXML(theFileName, &gReanimatorDefMap, theDefinition))
		return false;
#ifdef H1_PVZ_PORTABLE
    // PVZH1_COMPACT_DEFINITION_ARRAYS: first successful XML load, no borrowers.
    extern bool H1CompactReanimationDefinition(ReanimatorDefinition&);
    (void)H1CompactReanimationDefinition(*theDefinition);
#endif

	for (int aTrackIndex = 0; aTrackIndex < theDefinition->mTracks.count; aTrackIndex++)'''
    if text.count(original)!=1:raise RuntimeError('Pinned ReanimationLoadDefinition context changed')
    text=text.replace(original,replacement,1)
    anchor='\tfor (int aTrackIndex = 0; aTrackIndex < theDefinition->mTracks.count; aTrackIndex++)'
    text=text.replace(anchor,'    extern bool H1DefinitionNumericResolved(const void*);\n    const bool h1NumericResolved=H1DefinitionNumericResolved(theDefinition);\n'+anchor,1)
    first='\t\t\tReanimationFillInMissingData(aPrevTransX, aTransform.mTransX);'
    last='\t\t\tReanimationFillInMissingData(aPrevAlpha, aTransform.mAlpha);'
    assert text.count(first)==text.count(last)==1
    text=text.replace(first,'            if(!h1NumericResolved){\n'+first,1).replace(last,last+'\n            }',1)
    path.write_text(text,encoding='utf8',newline='\n')
