"""Deterministic H1 platform patches; never modifies the pinned checkout.

Gameplay, menus, levels and save formats remain upstream code. Each patch is
bounded to the platform lifecycle and fails if its reviewed context changes.
"""
from pathlib import Path
import importlib.util

ROOT=Path(__file__).resolve().parents[1]
REPLACEMENTS={
 'SexyAppFramework/graphics/GLImage.cpp':'platform/graphics/GLImage.cpp',
 'SexyAppFramework/graphics/GLInterface.cpp':'platform/graphics/GLInterface.cpp',
 'SexyAppFramework/imagelib/ImageLib.cpp':'platform/resources/ImageLib.cpp',
 'SexyAppFramework/platform/default/Window.cpp':'platform/Window.cpp',
 'SexyAppFramework/platform/default/Input.cpp':'platform/Input.cpp',
 'SexyAppFramework/fcaseopen/fcaseopen.c':'platform/h1_fcase.cpp',
 'SexyAppFramework/sound/SDLSoundManager.cpp':'platform/h1_sound.cpp',
 'SexyAppFramework/sound/SDLSoundInstance.cpp':None,
}

def replace(path,old,new):
    source=path.read_text(encoding='utf8')
    if source.count(old)!=1:raise ValueError(f'H1 patch context changed: {path.name}: {old[:70]!r}')
    path.write_text(source.replace(old,new),encoding='utf8')

def apply(source_root:Path):
    # The bundled Mac strings use RESTART_LEVEL for a confirmation question;
    # only these two button labels need the original short button key.
    replace(source_root/'Lawn/Widget/ContinueDialog.cpp',
            'mNewGameButton = MakeButton(ContinueDialog::ContinueDialog_NewGame, this, "[RESTART_LEVEL]");',
            'mNewGameButton = MakeButton(ContinueDialog::ContinueDialog_NewGame, this, "[RESTART_LEVEL_BUTTON]");')
    replace(source_root/'Lawn/Widget/NewOptionsDialog.cpp',
            'mRestartButton = MakeButton(NewOptionsDialog::NewOptionsDialog_Restart, this, "[RESTART_LEVEL]");',
            'mRestartButton = MakeButton(NewOptionsDialog::NewOptionsDialog_Restart, this, "[RESTART_LEVEL_BUTTON]");')
    base=source_root/'SexyAppFramework/SexyAppBase.cpp'
    replace(base,'#ifdef __EMSCRIPTEN__\n\t\tLoadingThreadProcStub(this);',
                 '#if defined(__EMSCRIPTEN__) || defined(H1_PVZ_PORTABLE)\n\t\tLoadingThreadProcStub(this);')
    replace(base,'#elif (defined(__ANDROID__) && !defined(__TERMUX__)) || defined(__IPHONEOS__)\n\tShutdown();\n#else\n\texit(theCode);',
                 '#elif (defined(__ANDROID__) && !defined(__TERMUX__)) || defined(__IPHONEOS__) || defined(H1_PVZ_PORTABLE)\n\tShutdown();\n#else\n\texit(theCode);')
    replace(source_root/'main.cpp','int main(int argc, char** argv)','int pvzp_upstream_main(int argc, char** argv)')
    # H1 uses a streaming tracker/PCM backend. Retaining the original filename
    # avoids copying a complete MO3 into RAM before loading the unpacked XM.
    music=source_root/'Lawn/System/Music.cpp'
    text=music.read_text(encoding='utf8')
    begin=text.index('bool Music::PvzpLoadMusic(')
    end=text.index('void Music::SetupVolumeForTune',begin)
    text=text[:begin]+'''bool Music::PvzpLoadMusic(MusicFile theMusicFile, std::string_view theFileName)
{
    auto *backend = static_cast<SDLMusicInterface*>(mApp->mMusicInterface.get());
    std::string filename(theFileName);
    Mix_Music *handle = Mix_LoadMUS(filename.c_str());
    if (!handle) return false;
    SDLMusicInfo info;
    info.mHMusic = handle;
    std::scoped_lock lock(backend->mMusicMapMutex);
    backend->mMusicMap.insert(SDLMusicMap::value_type(theMusicFile, info));
    return true;
}

'''+text[end:]
    music.write_text(text,encoding='utf8')
    replace(source_root/'PvzpLib/Definition.cpp',
            'std::filesystem::last_write_time(Sexy::PathFromU8(theFilePath), ec)',
            'std::filesystem::last_write_time(Sexy::PathFromU8(Sexy::GetResourcePath(theFilePath)), ec)')
    replace(source_root/'PvzpLib/Definition.cpp',
            '\treturn DefinitionWriteCompiledFile(theCompiledFilePath, theDefMap, theDefinition);',
            '''#ifdef H1_PVZ_PORTABLE
    // DefinitionLoadMap has already populated the complete live definition.
    // A serialized copy plus compressBound/zlib workspace would duplicate it
    // only to create an optional disk cache. Keep the parsed data on H1.
    return true;
#else
    return DefinitionWriteCompiledFile(theCompiledFilePath, theDefMap, theDefinition);
#endif''')
    replace(source_root/'PvzpLib/Definition.cpp',
            '\tconst bool aShouldTryCompiled = !aRequireCompiledUpToDate || DefinitionIsCompiled(theXMLFilePath);',
            '''#ifdef H1_PVZ_PORTABLE
    const bool aShouldTryCompiled = false; // Parse XML once; avoid full inflate + live-array duplication.
#else
    const bool aShouldTryCompiled = !aRequireCompiledUpToDate || DefinitionIsCompiled(theXMLFilePath);
#endif''')
    replace(source_root/'PvzpLib/PvzpStringFile.cpp',
            'thread_local std::string aMissing;',
            'static std::string aMissing; // H1 runs the engine and loading on one thread; no native TLS.')
    # The upstream thread starts before the title leaves its first black frame.
    # H1 loads synchronously: establish the same title state before entering
    # that work, then draw/update its original widget at safe resource boundaries.
    title=source_root/'Lawn/Widget/TitleScreen.cpp'
    replace(title, '\t\tmApp->StartLoadingThread();',
        '#ifndef PVZ_NATIVE_LOADING_PROGRESS\n\t\tmApp->StartLoadingThread();\n#endif')
    replace(title, '\t\tmTitleStateCounter = mTitleStateDuration;\n\t}',
        '\t\tmTitleStateCounter = mTitleStateDuration;\n#ifdef PVZ_NATIVE_LOADING_PROGRESS\n\t\tmApp->StartLoadingThread();\n#endif\n\t}')
    common=source_root/'PvzpLib/PvzpCommon.cpp'
    replace(common,
        'bool PvzpLoadNextResource()\n{\n\treturn static_cast<PvzpResourceManager*>(gSexyAppBase->mResourceManager.get())->PvzpLoadNextResource();\n}',
        '''#ifdef PVZ_NATIVE_LOADING_PROGRESS
extern "C" void pvzh1_native_loading_progress();
#endif
bool PvzpLoadNextResource()
{
    bool loaded=static_cast<PvzpResourceManager*>(gSexyAppBase->mResourceManager.get())->PvzpLoadNextResource();
#ifdef PVZ_NATIVE_LOADING_PROGRESS
    pvzh1_native_loading_progress();
#endif
    return loaded;
}''')
    # GL type declarations are supplied by the software backend, not GLAD.
    gl=ROOT/'platform/graphics/GLPlatform.h'
    (source_root/'SexyAppFramework/graphics/GLPlatform.h').write_bytes(gl.read_bytes())
    optional=ROOT/'platform/graphics/memory_patches.py'
    if optional.exists():
        spec=importlib.util.spec_from_file_location('pvzp_h1_memory_patches',optional)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        module.apply(source_root)
    optional=ROOT/'platform/graphics/reanim_fast_patches.py'
    if optional.exists():
        spec=importlib.util.spec_from_file_location('pvzp_h1_reanim_fast_patches',optional)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        module.apply(source_root)
    optional=ROOT/'platform/profiling_patches.py'
    if optional.exists():
        spec=importlib.util.spec_from_file_location('pvzp_h1_optional_profiling',optional)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        module.apply(source_root)
    optional=ROOT/'platform/graphics/raster_codegen_patches.py'
    if optional.exists():
        spec=importlib.util.spec_from_file_location('pvzp_h1_raster_codegen_patches',optional)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        module.apply(source_root)
    optional=ROOT/'platform/selector/selector_offline_patches.py'
    if optional.exists():
        spec=importlib.util.spec_from_file_location('pvzp_h1_offline_selector',optional)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        module.apply(source_root)

    optional=ROOT/"platform/widescreen_patches.py"
    spec=importlib.util.spec_from_file_location("pvzh1_widescreen",optional)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(source_root)
