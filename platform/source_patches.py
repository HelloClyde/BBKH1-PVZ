"""Deterministic H1 platform patches; never modifies the pinned checkout.

Gameplay, menus, levels and save formats remain upstream code. Each patch is
bounded to the platform lifecycle and fails if its reviewed context changes.
"""
from pathlib import Path
import importlib.util
import re

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
    app=source_root/'LawnApp.cpp'
    app.write_text('#include "OptionsFeedback.h"\n'+app.read_text(encoding='utf8'),encoding='utf8')
    replace(app,'void LawnApp::DoNewOptions(bool theFromGameSelector)\n{',
            'void LawnApp::DoNewOptions(bool theFromGameSelector)\n{\n    H1OptionsBegin();\n    struct Done {~Done(){H1OptionsConstructed();}} done;')
    options=source_root/'Lawn/Widget/NewOptionsDialog.cpp'
    options.write_text('#include "OptionsFeedback.h"\n'+options.read_text(encoding='utf8'),encoding='utf8')
    replace(options,'void NewOptionsDialog::Draw(Sexy::Graphics* g)\n{',
            'void NewOptionsDialog::Draw(Sexy::Graphics* g)\n{\n    struct Done {~Done(){H1OptionsDrawn();}} done;')
    # The bundled Mac strings use RESTART_LEVEL for a confirmation question;
    # only these two button labels need the original short button key.
    replace(source_root/'Lawn/Widget/ContinueDialog.cpp',
            'mNewGameButton = MakeButton(ContinueDialog::ContinueDialog_NewGame, this, "[RESTART_LEVEL]");',
            'mNewGameButton = MakeButton(ContinueDialog::ContinueDialog_NewGame, this, "[RESTART_LEVEL_BUTTON]");')
    replace(source_root/'Lawn/Widget/NewOptionsDialog.cpp',
            'mRestartButton = MakeButton(NewOptionsDialog::NewOptionsDialog_Restart, this, "[RESTART_LEVEL]");',
            'mRestartButton = MakeButton(NewOptionsDialog::NewOptionsDialog_Restart, this, "[RESTART_LEVEL_BUTTON]");')
    base=source_root/'SexyAppFramework/SexyAppBase.cpp'
    base.write_text('#include "SaveFile.h"\n'+base.read_text(encoding='utf8'),encoding='utf8')
    replace(base,'\tstd::ofstream aFile(PathFromU8(theFileName), std::ios::out | std::ios::binary | std::ios::trunc);\n\tif (!aFile)',
                 '\tconst bool saved=H1WriteSaveFile(theFileName,theData,theDataLen);\n\tif (!saved)')
    replace(base,'\taFile.write(reinterpret_cast<const char*>(theData), static_cast<std::streamsize>(theDataLen));\n\tif (!aFile)\n\t\treturn false;\n','')
    replace(base,'bool SexyAppBase::ReadBufferFromFile(const std::string& theFileName, Buffer* theBuffer, bool dontWriteToDemo)\n{',
                 'bool SexyAppBase::ReadBufferFromFile(const std::string& theFileName, Buffer* theBuffer, bool dontWriteToDemo)\n{\n    if(theFileName.find("userdata/")!=std::string::npos)H1RecoverSaveFile(theFileName);')
    replace(base,'#ifdef __EMSCRIPTEN__\n\t\tLoadingThreadProcStub(this);',
                 '#if defined(__EMSCRIPTEN__) || defined(H1_PVZ_PORTABLE)\n\t\tLoadingThreadProcStub(this);')
    replace(base,'#elif (defined(__ANDROID__) && !defined(__TERMUX__)) || defined(__IPHONEOS__)\n\tShutdown();\n#else\n\texit(theCode);',
                 '#elif (defined(__ANDROID__) && !defined(__TERMUX__)) || defined(__IPHONEOS__) || defined(H1_PVZ_PORTABLE)\n\tShutdown();\n#else\n\texit(theCode);')
    base.write_text('#include "Startup.h"\n'+base.read_text(encoding='utf8'),encoding='utf8')
    replace(base,'\t\t\tmMusicInterface->StopAllMusic();\n\n\t\tRestoreScreenResolution();',
                 '\t\t\tmMusicInterface->StopAllMusic();\n        if(mSoundManager)mSoundManager->StopAllSounds();\n        pvzp_audio_loading_begin(); // Drain output before the app destroys resources.\n\n\t\tRestoreScreenResolution();')
    replace(source_root/'main.cpp','int main(int argc, char** argv)','int pvzp_upstream_main(int argc, char** argv)')
    lawn=source_root/'LawnApp.cpp'
    text=lawn.read_text(encoding='utf8')
    text='#include "Startup.h"\n#include "actors/ActorPose.h"\n#include "graphics/EarlyLawn.h"\n#include "graphics/RewardEffects.h"\nclass LawnApp; extern void H1PreloadCurrentBoard(LawnApp*);\nextern void H1PreloadCurrentBoardForPlay(LawnApp*);\nextern void H1RetireBoardReplay();\n'+text
    # These are the confirmed return-to-menu and app-quit saves. Replays are
    # no longer drawn, while all original simulation data is still available.
    if text.count('mBoard->TryToSaveGame();')!=2:raise ValueError('Confirmed board-exit save boundaries changed')
    text=text.replace('mBoard->TryToSaveGame();','H1RetireBoardReplay();mBoard->TryToSaveGame();')
    anchor='void LawnApp::ShowGameSelector()\n{\n\tKillBoard();'
    assert text.count(anchor)==1
    text=text.replace(anchor,anchor+'\n    pvzh1_actor_pose_shutdown();pvzh1_early_lawn_shutdown();pvzh1_reward_effect_shutdown();\n    {extern void H1ReanimationDefinitionsTrimForMenu();H1ReanimationDefinitionsTrimForMenu();}',1)
    anchor='void LawnApp::ShowAwardScreen(AwardType theAwardType, bool theShowAchievements)\n{'
    assert text.count(anchor)==1
    text=text.replace(anchor,anchor+'\n    pvzh1_reward_effect_shutdown();',1)
    anchor='void LawnApp::CheckForGameEnd()\n{\n\tif (mBoard == nullptr || !mBoard->mLevelComplete)\n\t\treturn;'
    assert text.count(anchor)==1
    text=text.replace(anchor,anchor+'\n    H1AudioSaveScope audioLoadingScope;',1)
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
    // Gameplay dependencies are warmed from the actual board and chosen cards.
#else
    PreloadForUser();
#endif
    pvzh1_startup_mark(6);
\tif (mLoadingFailed''',1)
    anchor='void LawnApp::PreNewGame(GameMode theGameMode, bool theLookForSavedGame)\n{'
    if text.count(anchor)!=1:raise ValueError('First-board preload boundary changed')
    text=text.replace(anchor,anchor+'''
    H1AudioLoadingScope audioLoadingScope;
    H1LevelLoadingScope levelLoadingScope;
    // No profile-wide resource preload at this boundary.
''',1)
    text=text.replace('\tmBoard->InitLevel();','\tmBoard->InitLevel();\n    H1PreloadCurrentBoard(this);',1)
    text=text.replace('\tmBoard->StartLevel();','''    if(mBoard->ChooseSeedsOnCurrentLevel()){
        H1LevelLoadingScope selectedLoadingScope; H1PreloadCurrentBoardForPlay(this);
        H1RewardEffectsPrepare();
    }else{H1PreloadCurrentBoardForPlay(this);H1RewardEffectsPrepare();}
    H1ActorPoseFinishSod();
\tmBoard->StartLevel();''',1)
    assert text.count('\t\t\tDoContinueDialog();')==2
    # A restored board has already passed its intro/selection. Use the same
    # dependency lifetime and music warmup as StartPlaying, before Continue.
    text=text.replace('\t\t\tDoContinueDialog();','            H1PreloadCurrentBoardForPlay(this);\n            H1RewardEffectsPrepare();\n\t\t\tDoContinueDialog();')
    # A restarted board briefly overlaps its replacement and camera panorama.
    # Reward frames are needed only once the intro has completed (or a saved
    # board is ready); release them before building either replacement.
    anchor='void LawnApp::MakeNewBoard()\n{\n\tKillBoard();'
    assert text.count(anchor)==1
    text=text.replace(anchor,anchor+'\n    pvzh1_actor_pose_shutdown();\n    pvzh1_reward_effect_shutdown();',1)
    lawn.write_text(text,encoding='utf8')
    # Save completes before DoBackToMain tears down the board. Its PCM tail
    # must drain before config writes / object destruction, too, rather than
    # waiting until the next selector constructor starts its loading scope.
    replace(lawn,'\tmSoundSystem->CancelPausedFoley();\n\tWriteCurrentUserConfig();',
                 '\tmSoundSystem->CancelPausedFoley();\n    pvzp_audio_loading_begin();\n\tWriteCurrentUserConfig();')
    save=source_root/'Lawn/System/SaveGame.cpp'
    text=save.read_text(encoding='utf8');text='#include "Startup.h"\nextern "C" void pvzp_audio_service();\n'+text
    begin=text.index('class PortableSaveContext');end=text.index('static void SyncColorPortable',begin)
    segment,count=re.subn(r'(\tvoid Sync\w+\([^)]*\)\n\t\{)',r'\1\n        pvzp_audio_service();',text[begin:end])
    if count!=9:raise ValueError('Portable save service boundaries changed: '+str(count))
    text=text[:begin]+segment+text[end:]
    anchor='bool LawnSaveGame(Board* theBoard, const std::string& theFilePath)\n{'
    if text.count(anchor)!=1:raise ValueError('Portable save entry changed')
    save.write_text(text.replace(anchor,anchor+'\n    H1AudioSaveScope audioSaveScope;',1),encoding='utf8')
    resources=source_root/'Resources.cpp'
    text=resources.read_text(encoding='utf8');text='#include <algorithm>\n#include <cstdint>\nextern "C" void pvzp_audio_service();\n'+text
    old='''	static std::map<void*, int> aMap;

	if (gNeedRecalcVariableToIdMap)
	{
		gNeedRecalcVariableToIdMap = false;
		aMap.clear();
		for (int i = 0; i < static_cast<int>(ResourceId::RESOURCE_ID_MAX); i++)
		{
			void* value;
			std::memcpy(&value, gResources[i], sizeof(void*));
			aMap[value] = i;
		}
	}

	auto anIter = aMap.find(theVariable);
	return anIter == aMap.end() ? ResourceId::RESOURCE_ID_MAX : (ResourceId)anIter->second;'''
    new='''    // One compact allocation instead of thousands of individually allocated
    // tree nodes. Sorting by (address, ID) keeps the original last-ID alias rule.
    static std::vector<std::pair<uintptr_t,int>> ids;
    if(gNeedRecalcVariableToIdMap){
        gNeedRecalcVariableToIdMap=false;ids.clear();
        ids.reserve(static_cast<int>(ResourceId::RESOURCE_ID_MAX));
        for(int i=0;i<static_cast<int>(ResourceId::RESOURCE_ID_MAX);i++){
            void* value;std::memcpy(&value,gResources[i],sizeof(void*));
            ids.emplace_back(reinterpret_cast<uintptr_t>(value),i);pvzp_audio_service();
        }
        std::sort(ids.begin(),ids.end());
    }
    const auto key=reinterpret_cast<uintptr_t>(theVariable);
    auto it=std::upper_bound(ids.begin(),ids.end(),std::make_pair(key,static_cast<int>(ResourceId::RESOURCE_ID_MAX)));
    if(it==ids.begin())return ResourceId::RESOURCE_ID_MAX;
    --it;return it->first==key?static_cast<ResourceId>(it->second):ResourceId::RESOURCE_ID_MAX;'''
    if text.count(old)!=1:raise ValueError('Resource reverse-ID lookup context changed')
    resources.write_text(text.replace(old,new,1),encoding='utf8')
    selector=source_root/'Lawn/Widget/GameSelector.cpp'
    text=selector.read_text(encoding='utf8')
    text='#include "Startup.h"\n'+text
    anchor='GameSelector::GameSelector(LawnApp* theApp)\n{'
    if text.count(anchor)!=1:raise ValueError('Selector constructor boundary changed')
    text=text.replace(anchor,anchor+'\n    H1StartupSelectorScope startupScope;',1)
    # The hand must already own its definition/atlas before the touch starts
    # the animation. Keep all 450 original updates and original effects.
    anchor='\tmApp->mMusic->StopAllMusic();\n\tmApp->PlaySample(Sexy::SOUND_LOSEMUSIC);\n\tmStartingGame = true;'
    if text.count(anchor)!=1:raise ValueError('Adventure start context changed')
    text=text.replace(anchor,'    pvzh1_adventure_mark(0,0);\n'+anchor,1)
    anchor='\t\tmStartingGameCounter++;'
    if text.count(anchor)!=1:raise ValueError('Adventure update context changed')
    text=text.replace(anchor,anchor+'\n        pvzh1_adventure_mark(2,mStartingGameCounter);',1)
    anchor='\t\t\tmApp->KillGameSelector();\n\n\t\t\tif (mApp->IsIceDemo())'
    if text.count(anchor)!=1:raise ValueError('Adventure end context changed')
    text=text.replace(anchor,'            H1LevelLoadingScope levelLoadingScope;\n            pvzh1_adventure_mark(4,mStartingGameCounter);\n            H1SelectorOfflineRelease(this); // Release menu/hand pixels before board warmup.\n'+anchor,1)
    anchor='\tmHandReanimID = mApp->ReanimationGetID(aHandReanim);'
    if text.count(anchor)!=1:raise ValueError('Adventure hand context changed')
    text=text.replace(anchor,anchor+'\n    pvzh1_adventure_mark(1,0);',1)
    anchor='\t\taHandReanim->Draw(g);'
    if text.count(anchor)!=1:raise ValueError('Adventure hand draw context changed')
    text=text.replace(anchor,anchor+'\n        pvzh1_adventure_mark(3,mStartingGameCounter);',1)
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
    reanim=source_root/'PvzpLib/Reanimator.cpp'
    text=reanim.read_text(encoding='utf8')
    text='#include "h1_host.h"\nextern "C" void pvzp_audio_service();\n'+text
    text=text.replace('for (int aTrackIndex = 0; aTrackIndex < theDefinition->mTracks.count; aTrackIndex++)\n\t{',
        'for (int aTrackIndex = 0; aTrackIndex < theDefinition->mTracks.count; aTrackIndex++)\n\t{\n        pvzp_audio_service();')
    anchor='\t\tif (DefinitionIsCompiled(aReanimationParams->mReanimFileName))'
    if text.count(anchor)!=1:raise ValueError('Legacy animation cache scan changed')
    text=text.replace(anchor,'#ifndef H1_PVZ_PORTABLE\n'+anchor+'\n#endif\n#ifdef H1_PVZ_PORTABLE\n        if(false) // H1D is loaded on demand; do not stat 292 absent legacy cache paths.\n#endif',1)
    reanim.write_text(text,encoding='utf8')
    raster=source_root/'SexyAppFramework/graphics/SWTri.cpp'
    text=raster.read_text(encoding='utf8')
    entry='bool blend, bool vertexColor)\n{'
    if text.count(entry)!=1:raise ValueError('Software draw entry changed')
    text='extern "C" void pvzp_audio_service();\n'+text
    text=text.replace(entry,entry+'\n    pvzp_audio_service();',1)
    raster.write_text(text,encoding='utf8')
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
    replace(title,'\t\tmApp->mMusic->MusicTitleScreenInit();',
        '#ifndef PVZ_NATIVE_LOADING_PROGRESS\n\t\tmApp->mMusic->MusicTitleScreenInit();\n#endif')
    replace(title, '\t\tmApp->StartLoadingThread();',
        '#ifndef PVZ_NATIVE_LOADING_PROGRESS\n\t\tmApp->StartLoadingThread();\n#endif')
    replace(title, '\t\tmTitleStateCounter = mTitleStateDuration;\n\t}',
        '\t\tmTitleStateCounter = mTitleStateDuration;\n#ifdef PVZ_NATIVE_LOADING_PROGRESS\n        mTitleStateCounter-=50; // First visible frame is the opaque original PopCap logo.\n        MarkDirty();mApp->mWidgetManager->MarkAllDirty();mApp->DrawDirtyStuff();\n        mApp->mMusic->MusicTitleScreenInit();\n\t\tmApp->StartLoadingThread();\n#endif\n\t}')
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

    # Board pixels are immutable while a normal adventure dialog pauses the
    # simulation. Reuse that layer; original dialog/child widgets still draw.
    board=source_root/'Lawn/Board.cpp'
    text=board.read_text(encoding='utf8')
    # H1 policy selected by the user: persist at return-to-menu, shutdown and
    # level completion. Pausing a modal dialog does not perform SD writes.
    pause_save='\tif (thePause && NeedSaveGame())\n\t{\n\t\tTryToSaveGame();\n\t}'
    assert text.count(pause_save)==1
    text=text.replace(pause_save,'\t// H1: pausing changes memory state only; lifecycle boundaries save the game.')
    text='#include "graphics/PausedBoardCache.h"\n#include "graphics/RewardEffects.h"\n#include "misc/ResourceManager.h"\n'+text
    needle='void Board::ZombiesWon(Zombie* theZombie)\n{\n\tif (mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON)\n\t\treturn;'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    pvzh1_reward_effect_shutdown();')
    # KillBoard defers Widget destruction, while NewGame loads the next board
    # immediately. Release this board's tracked groups before their next load,
    # so its later Widget destructor cannot unload the replacement's images.
    needle='\tmApp->mEffectSystem->EffectSystemFreeAll();\n}'
    assert text.count(needle)==1
    text=text.replace(needle,'\tmApp->mEffectSystem->EffectSystemFreeAll();\n'
        '    mApp->mResourceManager->ReleaseTrackedResources(mLoadedResourceNames);\n}')
    needle='\tDrawGameObjects(g);\n}'
    assert text.count(needle)==1
    text=text.replace(needle,'\tif(!H1PausedBoardReplay(this,g)){DrawGameObjects(g);H1SodOfflineDraw(this,g);H1PausedBoardCapture(this,g);}\n}')
    needle='Board::~Board() = default;';assert text.count(needle)==1
    text=text.replace(needle,'Board::~Board(){H1IntroMovieRelease(this);H1PausedBoardForget(this);}')
    text='#include "video/IntroMovie.h"\n'+text
    needle='void Board::Draw(Graphics* g)\n{';assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    if(H1IntroMovieDraw(this,g))return;')
    needle='\tWidget::Update();\n\tMarkDirty();\n\n\tmBoardUpdateCounter++;\n\tmCutScene->Update();'
    if text.count(needle)!=1:raise RuntimeError('Pinned board update changed')
    text=text.replace(needle,'\tWidget::Update();\n\tMarkDirty();\n'
        '    if(H1IntroMovieActive(this)){++mBoardUpdateCounter;mCutScene->Update();return;}\n'
        '\n\tmBoardUpdateCounter++;\n\tmCutScene->Update();')
    board.write_text(text,encoding='utf8',newline='\n')

    text=board.read_text(encoding='utf8')
    start=text.index('void Board::DrawBackdrop(Graphics* g)')
    first=text.index('\tif (mLevel == 1 && mApp->IsFirstTimeAdventureMode())',start)
    last=text.index('\n\tif (mApp->mGameScene == GameScenes::SCENE_ZOMBIES_WON)',first)
    text='#include "actors/ActorPose.h"\n#include "graphics/EarlyLawn.h"\n'+text[:first]+'    if(!H1EarlyLawnDraw(this,g)){\n'+text[first:last]+'\n    }\n'+text[last:]
    board.write_text(text,encoding='utf8',newline='\n')

    credits=source_root/'Lawn/Widget/CreditScreen.cpp';text=credits.read_text(encoding='utf8')
    text='extern void H1ReanimationDefinitionsTrimForMenu();\n'+text
    boundary='mApp->mEffectSystem->EffectSystemFreeAll();\n\tmApp->mMusic->StopAllMusic();'
    assert text.count(boundary)==1
    text=text.replace(boundary,'mApp->mEffectSystem->EffectSystemFreeAll();\n\tH1ReanimationDefinitionsTrimForMenu();\n\tmApp->mMusic->StopAllMusic();',1)
    needle='mApp->mMusic->PlayFromOffset(MusicFile::MUSIC_FILE_CREDITS_ZOMBIES_ON_YOUR_LAWN, aMusicOffset - 900, 1.0f);'
    assert text.count(needle)==1
    text=text.replace(needle,'mApp->mMusic->MakeSureMusicIsPlaying(MusicTune::MUSIC_TUNE_CREDITS_ZOMBIES_ON_YOUR_LAWN);\n\tpvzh1_music_seek_pcm_ms(mApp->mMusic->GetMusicHandle(MusicFile::MUSIC_FILE_CREDITS_ZOMBIES_ON_YOUR_LAWN), aJumpMilliseconds);')
    credits.write_text(text,encoding='utf8',newline='\n')

    reanim=source_root/'PvzpLib/Reanimator.cpp';text=reanim.read_text(encoding='utf8')
    needle='void ReanimatorEnsureDefinitionLoaded(ReanimationType theReanimType, bool theIsPreloading)\n{'
    assert text.count(needle)==1
    text='#include "actors/ActorPose.h"\n#include <exception>\nextern void H1PrewarmDefinitionImages(int,bool);\n'+text.replace(needle,needle+'\n    struct H1WarmScope {int type;bool preloading;~H1WarmScope() noexcept(false){if(!std::uncaught_exceptions())H1PrewarmDefinitionImages(type,preloading);}} warmScope{int(theReanimType),theIsPreloading};')
    needle='void Reanimation::DrawRenderGroup(Graphics* g, int theRenderGroup)\n{'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    if(H1ActorPoseDraw(this,g,theRenderGroup))return;\n#ifdef PVZH1_ACTOR_DETAIL_PROFILE\n    H1ActorLiveDrawScope h1LiveDraw(this);\n#endif')
    needle='void Reanimation::StartBlend(int theBlendTime)\n{';assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    H1ActorPoseNoteBlend(this,theBlendTime);')
    needle='void Reanimation::ReanimationDelete()\n{';assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    H1ActorPoseForget(this);')
    needle='void ReanimationFreeDefinition(ReanimatorDefinition* theDefinition)\n{';assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    H1ActorPoseForgetDefinition(theDefinition);')
    reanim.write_text(text,encoding='utf8',newline='\n')

    zombie=source_root/'Lawn/Zombie.cpp';text=zombie.read_text(encoding='utf8')
    needle='\t\tfor (size_t i = 0; i < LENGTH(gBossZombieList); i++)'
    assert text.count(needle)==1
    # The current-board pass already enumerates its required enemy types.
    # Keep the upstream broad preload for callers outside this targeted scope.
    text='extern bool H1TargetedBoardPreloadActive();\n'+text.replace(
        needle,'        if(!H1TargetedBoardPreloadActive())\n'+needle,1)
    # A zombie walk rate is derived from its selected clip and gameplay state.
    # Refresh it at the first actual update, after initialization/preloading.
    # This does not advance animation time or change movement speed/RNG.
    needle='\tmZombieAge++;\n\tbool doUpdate = false;'
    assert text.count(needle)==1
    text=text.replace(needle,'''    if(mZombieAge==0){ // PVZH1_ZOMBIE_INITIAL_MOTION
        auto* body=mApp->ReanimationTryToGet(mBodyReanimID);
        if(body&&body->mAnimRate==0.0f)UpdateAnimSpeed();
    }
'''+needle,1)
    zombie.write_text(text,encoding='utf8',newline='\n')

    save=source_root/'Lawn/System/SaveGame.cpp';text=save.read_text(encoding='utf8')
    needle='\t\t\taZombie->mApp = theBoard->mApp;\n\t\t\taZombie->mBoard = theBoard;'
    assert text.count(needle)==1
    # FixBoardAfterLoad runs after all entity/effect chunks have been restored.
    # Reconcile the derived rate once here rather than trusting a saved zero.
    # Original UpdateAnimSpeed preserves ice/butter, eating and death phases.
    text=text.replace(needle,needle+'''
            if(!aZombie->mDead){ // PVZH1_ZOMBIE_RESTORE_MOTION
                auto* body=theBoard->mApp->ReanimationTryToGet(aZombie->mBodyReanimID);
                if(body&&body->mAnimRate==0.0f)aZombie->UpdateAnimSpeed();
            }''',1)
    save.write_text(text,encoding='utf8',newline='\n')

    cut=source_root/'Lawn/CutScene.cpp';text=cut.read_text(encoding='utf8')
    start=text.index('// Sod rolling on early adventure levels')
    needle='\t\t\tif (mBoard->mLevel == 1)';at=text.index(needle,start)
    text='#include "actors/ActorPose.h"\n'+text[:at]+text[at:].replace(needle,'            if(H1SodOfflineBegin(mBoard)){}\n            else if (mBoard->mLevel == 1)',1)
    cut.write_text(text,encoding='utf8',newline='\n')

    # The first-player movie is a full viewport scene, not an 800px board.
    # Its black/fade layers and centered captions must cover the H1 widescreen.
    text=cut.read_text(encoding='utf8')
    start=text.index('void CutScene::DrawIntro(Graphics* g)')
    end=text.index('bool CutScene::ShouldRunUpsellBoard()',start)
    text='#include "graphics/ScreenLayout.h"\n'+text[:start]+text[start:end].replace('BOARD_WIDTH','Sexy::H1LogicalWidth')+text[end:]
    needle='void CutScene::LoadIntroBoard()\n{\n\tClearUpsellBoard();\n\tmApp->mMuteSoundsForCutscene = true;'
    assert text.count(needle)==1
    text='#include "video/IntroMovie.h"\n'+text.replace(needle,needle+'\n    if(H1IntroMovieActive(mBoard))return;')
    needle='void CutScene::PreloadResources()\n{'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    if(mApp->mGameMode==GAMEMODE_INTRO&&H1IntroMovieRequired()){mPreloaded=true;mPlacedZombies=true;mLoadedResourceNames.clear();return;}')
    needle='void CutScene::PlaceStreetZombies()\n{'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    if(mApp->mGameMode==GAMEMODE_INTRO&&H1IntroMovieRequired()){mPlacedZombies=true;return;}')
    needle='void CutScene::PlaceLawnItems()\n{'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'\n    if(mApp->mGameMode==GAMEMODE_INTRO&&H1IntroMovieRequired()){mPlacedLawnItems=true;return;}')
    needle='\tif (mCutsceneTime == TimeIntro_End)\n\t{'
    assert text.count(needle)==1
    text=text.replace(needle,needle+'\n        H1IntroMovieCompleted(mBoard);')
    needle='mApp->AddPvzpParticle(400, 300, aRenderPosition, ParticleEffect::PARTICLE_SCREEN_FLASH);'
    if text.count(needle)!=1:raise RuntimeError('Pinned opening flash changed')
    text=text.replace(needle,'if(!H1IntroMovieRequired())'+needle)
    cut.write_text(text,encoding='utf8',newline='\n')

    # Seed selection draws static packets, not every unlocked plant's full
    # animation. Current board/wave dependencies are warmed in PreNewGame;
    # actual selected and imitated plants are warmed again before StartLevel.
    # Keep all original wave, award, dialogue and scripted challenge warmups.
    unlocked='''\tfor (SeedType aSeedType = SeedType::SEED_PEASHOOTER; aSeedType < SeedType::NUM_SEED_TYPES; aSeedType = static_cast<SeedType>(static_cast<int>(aSeedType) + 1))
\t{
\t\tif (mApp->HasSeedType(aSeedType))
\t\t{
\t\t\tPlant::PreloadPlantResources(aSeedType);
\t\t}
\t}'''
    replace(cut,unlocked,'    // H1: current/selected plant dependencies are already warmed at their lifecycle boundaries.')

    particles=source_root/'PvzpLib/PvzpParticle.cpp'
    text=particles.read_text(encoding='utf8')
    text='#include "graphics/RewardEffects.h"\n#include "graphics/CombatSprites.h"\n'+text
    start=text.index('void PvzpParticleSystem::PvzpParticleInitializeFromDef(')
    end=text.index('void PvzpParticleEmitter::PvzpEmitterInitialize(',start)
    close=text.rfind('\n}',start,end)
    text=text[:close]+'\n    H1RewardEffectsBegin(this); H1CombatHitBegin(this);'+text[close:]
    for signature,statement in (('void PvzpParticleSystem::Update()','H1CombatHitUpdate(this); if(H1RewardEffectsUpdate(this))return;'),('void PvzpParticleSystem::Draw(Graphics* g)','if(H1CombatHitDraw(this,g))return; if(H1RewardEffectsDraw(this,g))return;'),('void PvzpParticleSystem::ParticleSystemDie()','H1CombatHitForget(this); H1RewardEffectsForget(this);')):
        anchor=signature+'\n{'
        assert text.count(anchor)==1
        text=text.replace(anchor,anchor+'\n    '+statement)
    particles.write_text(text,encoding='utf8',newline='\n')
    coin=source_root/'Lawn/Coin.cpp'
    text=coin.read_text(encoding='utf8')
    anchor='mApp->AddPvzpParticle(mPosX + 30.0f, mPosY + 30.0f, mRenderOrder + 1, ParticleEffect::PARTICLE_STARBURST);'
    assert text.count(anchor)==1
    text='#include "graphics/RewardEffects.h"\n#include "graphics/CombatSprites.h"\n'+text.replace(anchor,'H1RewardStarburst(mApp,mPosX + 30.0f,mPosY + 30.0f,mRenderOrder + 1);')
    coin.write_text(text,encoding='utf8',newline='\n')
    board=source_root/'Lawn/Board.cpp';text='#include "BoardLookup.h"\n'+board.read_text(encoding='utf8')
    for signature,statement in (('Board::Board(LawnApp* theApp)','H1BoardSupportChanged();'),('Plant* Board::GetFlowerPotAt(int theGridX, int theGridY)','if(!H1BoardMayHaveSupport(this,SEED_FLOWERPOT))return nullptr;'),('Plant* Board::GetPumpkinAt(int theGridX, int theGridY)','if(!H1BoardMayHaveSupport(this,SEED_PUMPKINSHELL))return nullptr;')):
        anchor=signature+'\n{';assert text.count(anchor)==1;text=text.replace(anchor,anchor+'\n    '+statement)
    board.write_text(text,encoding='utf8',newline='\n')
    plant=source_root/'Lawn/Plant.cpp';text='#include "BoardLookup.h"\n'+plant.read_text(encoding='utf8')
    for anchor in ('\tmSeedType = theSeedType;','void Plant::Die()\n{'):
        assert text.count(anchor)==1;text=text.replace(anchor,anchor+'\n    H1BoardSupportChanged();')
    plant.write_text(text,encoding='utf8',newline='\n')
    save=source_root/'Lawn/System/SaveGame.cpp';text='#include "BoardLookup.h"\n'+save.read_text(encoding='utf8')
    anchor='bool LawnLoadGame(Board* theBoard, const std::string& theFilePath)\n{';assert text.count(anchor)==1
    text=text.replace(anchor,anchor+'\n    struct H1SupportLoadScope {H1SupportLoadScope(){H1BoardSupportChanged();}~H1SupportLoadScope(){H1BoardSupportChanged();}} supportLoadScope;')
    save.write_text(text,encoding='utf8',newline='\n')

    cached=source_root/'Lawn/System/ReanimationLawn.cpp';text=cached.read_text(encoding='utf8')
    text='#include "actors/CachedFrames.h"\nextern "C" volatile unsigned pvzh1_cached_frame_diagnostic[8];\n'+text
    for entry,kind,identifier,variation in (
        ('std::unique_ptr<MemoryImage> ReanimatorCache::MakeCachedPlantFrame(SeedType theSeedType, DrawVariation theDrawVariation)',0,'int(theSeedType)','int(theDrawVariation)'),
        ('std::unique_ptr<MemoryImage> ReanimatorCache::MakeCachedMowerFrame(LawnMowerType theMowerType)',1,'int(theMowerType)','0'),
        ('std::unique_ptr<MemoryImage> ReanimatorCache::MakeCachedZombieFrame(ZombieType theZombieType)',2,'int(theZombieType)','0')):
        start=text.index(entry+'\n{');end=text.index('\n}\n',start);body=text[start+len(entry)+2:end]
        load=f'H1CachedFrameLoad({kind},{identifier},{variation})'
        replacement=entry+'\n{\n#if defined(H1_PVZ_PORTABLE) && !defined(PVZ_HOST_RUNNER)\n    return '+load+';\n#else\n#ifdef PVZ_HOST_RUNNER\n    if(H1CachedFramesTestEnabled())return '+load+';\n#endif\n'+body+'\n#endif'
        text=text[:start]+replacement+text[end:]
    entry='std::unique_ptr<MemoryImage> ReanimatorCache::MakeBlankMemoryImage(int theWidth, int theHeight)\n{'
    assert text.count(entry)==1;text=text.replace(entry,entry+'\n    ++pvzh1_cached_frame_diagnostic[4];')
    cached.write_text(text,encoding='utf8',newline='\n')
    lawn=source_root/'LawnApp.cpp';text='#include "actors/CachedFrames.h"\n'+lawn.read_text(encoding='utf8')
    anchor='void LawnApp::MakeNewBoard()\n{\n\tKillBoard();';assert text.count(anchor)==1;text=text.replace(anchor,anchor+'\n    H1CachedFramesRelease();')
    anchor='void LawnApp::ShowGameSelector()\n{\n\tKillBoard();';assert text.count(anchor)==1;text=text.replace(anchor,anchor+'\n    H1CachedFramesRelease();')
    lawn.write_text(text,encoding='utf8',newline='\n')

    # Apply last, after the original lifecycle boundaries have been installed.
    spec=importlib.util.spec_from_file_location('pvzh1_ui_flow',ROOT/'platform/ui_flow_patches.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(source_root)
