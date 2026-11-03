"""Capture only Selector-owned XML strings and retire only its unused slot."""
from pathlib import Path

def apply(source_root):
    root=Path(source_root);path=root/'PvzpLib/Reanimator.cpp'
    text=path.read_text(encoding='utf8')
    if 'PVZH1_SELECTOR_DEFINITION_OWNERS' not in text:
        anchor='bool ReanimationLoadDefinition(const std::string& theFileName, ReanimatorDefinition* theDefinition)'
        if text.count(anchor)!=1:raise RuntimeError('Pinned reanimation loader changed')
        text=text.replace(anchor,'''#ifdef H1_PVZ_PORTABLE
// PVZH1_SELECTOR_DEFINITION_OWNERS: never infer ownership after inheritance.
void H1SelectorLoadBegin(ReanimatorDefinition*);
void H1SelectorParsed(ReanimatorDefinition*);
void H1SelectorLoadReady(ReanimatorDefinition*);
void H1SelectorLoadFailed(ReanimatorDefinition*);
void H1SelectorFreed(ReanimatorDefinition*);
#endif
'''+anchor,1)
        old='''{
\tif (!DefinitionLoadXML(theFileName, &gReanimatorDefMap, theDefinition))
\t\treturn false;'''
        new='''{
#ifdef H1_PVZ_PORTABLE
    H1SelectorLoadBegin(theDefinition);
#endif
\tif (!DefinitionLoadXML(theFileName, &gReanimatorDefMap, theDefinition)) {
#ifdef H1_PVZ_PORTABLE
        H1SelectorLoadFailed(theDefinition);
#endif
\t\treturn false;
    }
#ifdef H1_PVZ_PORTABLE
    H1SelectorParsed(theDefinition);
#endif'''
        if text.count(old)!=1:raise RuntimeError('Pinned successful XML context changed')
        text=text.replace(old,new,1)
        old='''\treturn true;
}

void ReanimationFreeDefinition(ReanimatorDefinition* theDefinition)'''
        new='''#ifdef H1_PVZ_PORTABLE
    H1SelectorLoadReady(theDefinition);
#endif
\treturn true;
}

void ReanimationFreeDefinition(ReanimatorDefinition* theDefinition)'''
        if text.count(old)!=1:raise RuntimeError('Pinned load completion context changed')
        text=text.replace(old,new,1)
        old='\tDefinitionFreeMap(&gReanimatorDefMap, theDefinition);'
        if text.count(old)!=1:raise RuntimeError('Pinned reanimation definition free changed')
        text=text.replace(old,old+'\n#ifdef H1_PVZ_PORTABLE\n    H1SelectorFreed(theDefinition);\n#endif',1)
        path.write_text(text,encoding='utf8',newline='\n')
    path=root/'SexyAppFramework/widget/WidgetManager.cpp';text=path.read_text(encoding='utf8')
    if 'PVZH1_ROOT_SELECTOR_DEFINITION_TRIM' in text:return
    old='extern "C" void pvzh1_global_allocator_trim_empty(void*);'
    if text.count(old)!=1:raise RuntimeError('Pinned root allocator declaration changed')
    text=text.replace(old,old+'\nextern "C" void pvzh1_selector_definition_trim(void*);',1)
    old='        pvzh1_global_allocator_trim_empty(nullptr);'
    if text.count(old)!=1:raise RuntimeError('Pinned root allocator call changed')
    text=text.replace(old,old+'\n        // PVZH1_ROOT_SELECTOR_DEFINITION_TRIM: stable actual-screen boundary.\n        pvzh1_selector_definition_trim(nullptr);',1)
    path.write_text(text,encoding='utf8',newline='\n')
