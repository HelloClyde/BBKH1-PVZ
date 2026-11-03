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
    path.write_text(text.replace(original,replacement,1),encoding='utf8',newline='\n')
