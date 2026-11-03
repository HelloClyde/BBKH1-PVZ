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
    lawn=source_root/'LawnApp.cpp'
    text=lawn.read_text(encoding='utf8')
    text='#include "Startup.h"\nstatic bool h1_game_preload_pending=false;\n'+text
    for phase,anchor in ((1,'aHesitationResources.EndBracket();'),(2,'mMusic->MusicInit();'),
                         (3,'PvzpFoleyInitialize(gLawnFoleyParamArray, LENGTH(gLawnFoleyParamArray));'),
                         (4,'TrailLoadDefinitions(gLawnTrailArray, LENGTH(gLawnTrailArray));'),
                         (5,'PvzpParticleLoadDefinitions(gLawnParticleArray, LENGTH(gLawnParticleArray));'),
                         (7,'LoadGroup("LoadingSounds", 54);')):
        if text.count(anchor)!=1:raise ValueError('Startup phase anchor changed: '+anchor)
        text=text.replace(anchor,anchor+f'\n    pvzh1_startup_mark({phase});',1)
    anchor='\tPreloadForUser();\n\tif (mLoadingFailed'
    if text.count(anchor)!=1:raise ValueError('Startup preload boundary changed')
    text=text.replace(anchor,'''#ifdef H1_PVZ_PORTABLE
    // The main menu needs only its selector animation. The original gameplay
    // warmup still runs once, before creating/loading the first board.
    ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_SELECTOR_SCREEN, true);
    mCompletedLoadingThreadTasks += GetNumPreloadingTasks();
    h1_game_preload_pending=true;
#else
    PreloadForUser();
#endif
    pvzh1_startup_mark(6);
\tif (mLoadingFailed''',1)
    anchor='void LawnApp::PreNewGame(GameMode theGameMode, bool theLookForSavedGame)\n{'
    if text.count(anchor)!=1:raise ValueError('First-board preload boundary changed')
    text=text.replace(anchor,anchor+'''
    if(h1_game_preload_pending){h1_game_preload_pending=false;PreloadForUser();}
''',1)
    lawn.write_text(text,encoding='utf8')
    selector=source_root/'Lawn/Widget/GameSelector.cpp'
    text=selector.read_text(encoding='utf8')
    text='#include "Startup.h"\n'+text
    anchor='GameSelector::GameSelector(LawnApp* theApp)\n{'
    if text.count(anchor)!=1:raise ValueError('Selector constructor boundary changed')
    text=text.replace(anchor,anchor+'\n    H1StartupSelectorScope startupScope;',1)
    # The decorative open/sign transitions cannot use the offline final-pose
    # pixels. Finish them before drawing while retaining upstream Update's
    # button enablement, new-user dialog and profile/achievement checks.
    for animation,count in (('anim_open',1),('anim_sign',2)):
        anchor=f'aSelectorReanim->PlayReanim("{animation}", ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD, 0, 30.0f);'
        if text.count(anchor)!=count:raise ValueError('Selector transition context changed: '+animation)
        text=text.replace(anchor,anchor+'\n#ifdef H1_PVZ_PORTABLE\n    aSelectorReanim->mAnimTime=1.0f;\n    aSelectorReanim->mLoopCount=1;\n#endif')
    # Static sky/grass on H1: moving decorative transforms invalidate reusable
    # raster plans even when the visible motion is only a fraction of a pixel.
    anchor='\t\telse\n\t\t\taCloudReanim->mAnimRate = 0.0f;'
    if text.count(anchor)!=1:raise ValueError('Cloud initialization changed')
    text=text.replace(anchor,anchor+'\n#ifdef H1_PVZ_PORTABLE\n        aCloudReanim->mAnimRate=0.0f;\n#endif',1)
    cloud_motion='''	for (int i = 0; i < 6; i++)
	{
		Reanimation* aCloudReanim = mApp->ReanimationGet(mCloudReanimID[i]);
		if (mCloudCounter[i] > 0)
		{
			if (--mCloudCounter[i] == 0)
			{
				aCloudReanim->mLoopCount = 0;
				aCloudReanim->mAnimTime = 0.0f;
				aCloudReanim->mAnimRate = 0.5f;
			}
		}
		else if (aCloudReanim->mLoopCount > 0)
			mCloudCounter[i] = RandRangeInt(2000, 4000);
	}'''
    grass_motion='''	if (--mLeafCounter == 0)
	{
		float aRate = RandRangeFloat(3.0f, 12.0f);
		aLeafReanim->PlayReanim("anim_grass", ReanimLoopType::REANIM_LOOP, 20, aRate);
		mLeafCounter = RandRangeInt(200, 400);
	}'''
    for anchor in (cloud_motion,grass_motion):
        if text.count(anchor)!=1:raise ValueError('Menu decoration update context changed')
        text=text.replace(anchor,'#ifndef H1_PVZ_PORTABLE\n'+anchor+'\n#endif',1)
    selector.write_text(text,encoding='utf8')
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
            'bool DefinitionCompileAndLoad(const std::string& theXMLFilePath, const DefMap* theDefMap, void* theDefinition)\n{',
            '''extern bool H1DefinitionLoad(const std::string&, const DefMap*, void*);
bool DefinitionCompileAndLoad(const std::string& theXMLFilePath, const DefMap* theDefMap, void* theDefinition)
{
    // H1D1 is produced on the PC using the original parser. It has no ABI pointers.
    if (H1DefinitionLoad(theXMLFilePath, theDefMap, theDefinition)) return true;''')
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

    optional=ROOT/"platform/localization_patches.py"
    spec=importlib.util.spec_from_file_location("pvzh1_localization",optional)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(source_root)
