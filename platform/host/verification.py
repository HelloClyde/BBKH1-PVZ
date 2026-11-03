"""Summarize recorded host-only integration evidence without changing the game."""
from pathlib import Path
import hashlib
import json
import argparse
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/host'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def relative(path):
    return path.relative_to(ROOT).as_posix()

def read_run(name):
    path=BUILD/name
    states=[json.loads(line) for line in (path/'states.jsonl').read_text().splitlines()]
    metadata=json.loads((path/'run-metadata.json').read_text())
    assert metadata['exit_code']==0
    assert metadata['prepared_manifest_sha256']==digest(path/'prepared.json')
    assert metadata['compile_report_sha256']==digest(path/'compile-report.json')
    assert metadata['script_sha256']==digest(path/'input.script')
    assert max(state['image_load_failures'] for state in states)==0
    report=json.loads((path/'compile-report.json').read_text())
    assert report['compiled']==report['source_count']>=156 and report['linked'] and not report['failed']
    return path,states,metadata

def image(source,destination=None):
    target=destination or source.with_suffix('.png')
    target.parent.mkdir(parents=True,exist_ok=True)
    Image.open(source).save(target)
    return {'path':relative(target),'sha256':digest(target),'size':[800,600],
            'source_frame':relative(source),'source_sha256':digest(source)}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--first-run',default='run64-paged-font-first')
    parser.add_argument('--restore-run',default='run65-paged-font-restore')
    args=parser.parse_args()
    first,states,metadata=read_run(args.first_run)
    restore,restored,restore_meta=read_run(args.restore_run)
    assert all(s['source_cache_budget']==7*1024*1024 for s in states+restored)
    assert metadata.get('compiled_cache_files_before',0)==metadata.get('compiled_cache_files_after',0)==0
    assert restore_meta.get('compiled_cache_files_before',0)==restore_meta.get('compiled_cache_files_after',0)==0
    assert any(s['scene']==1 for s in states)
    assert any(s['scene']==2 and s['level']==0 and s.get('game_mode')==72 for s in states)
    events={state['event']:state for state in states if state['event'] not in ('scene','frame')}
    planted,shot,hit=(events[name] for name in ('first-plant','first-projectile','first-hit'))
    assert planted['scene']==shot['scene']==hit['scene']==3
    assert planted['level']==shot['level']==hit['level']==1
    assert shot['projectiles']>0 and any(z['hp']==z['max_hp']==270 for z in shot['zombies'])
    assert any(z['hp']==250 and z['max_hp']==270 for z in hit['zombies'])
    held=events['pause-held']
    paused=next(state for state in states if state.get('paused'))
    assert held['frame']>paused['frame'] and held['paused']
    assert paused['board_counter']==held['board_counter']
    assert paused['zombies']==held['zombies'] and paused['plants']==held['plants']
    resumed=events['resumed']
    assert not resumed['paused'] and resumed['board_counter']>held['board_counter']
    award=events['level-award-input']
    assert award['wave']==award['total_waves']==4 and not award['zombies']
    assert 2<=len(award['plants'])<=6
    assert all(p['type']==0 and p['row']==2 and 0<=p['col']<6 and p['hp']==300 for p in award['plants'])
    assert len({p['col'] for p in award['plants']})==len(award['plants'])
    unlocked=next(state for state in states if state['scene']==3 and state['level']==2 and state['event']=='scene')
    assert unlocked['profile_level']==2 and unlocked['total_waves']==6
    assert 'Loaded save game (v4)' in (restore/'pvz.log').read_text()
    assert any(state['event']=='resume-save-input' for state in restored)
    restored_play=next(state for state in restored if state['scene']==3 and state['event']=='scene' and not state['dialogs'])
    assert restored_play['level']==restored_play['profile_level']==2
    assert restored_play['board_counter']>=states[-1]['board_counter']
    assert restored_play['sun']==50 and restored_play['total_waves']==6
    if metadata.get('trim_unused_atlases'):
        assert max(s.get('atlas_active_checks',0) for s in states)>0
        assert max(s.get('atlas_handle_checks',0) for s in states)>0
        assert max(s.get('atlas_rebuild_checks',0) for s in restored)>0
        assert restored[-1]['scene']==1
    # Original engine name is game<profile-id>_<mode>.v4, not game<level>.v4.
    saves=json.loads((first/'save-manifest-after-exit.json').read_text())
    assert saves['game1_0.v4']['bytes']>0
    docs=ROOT/'docs'
    menu=sorted(first.glob('frame-*-scene-1.ppm'),key=lambda p:int(p.name.split('-')[1]))[-1]
    reward=sorted(first.glob('frame-*-scene-5.ppm'),key=lambda p:int(p.name.split('-')[1]))[-1]
    screenshots={
        'menu':image(menu,docs/'host-menu.png'),
        'real_projectile_and_damage':image(first/'first-real-hit.ppm',docs/'host-combat.png'),
        'sunflower_reward':image(reward,docs/'host-reward.png'),
        'pause':image(first/'original-pause-menu.ppm'),
        'level2':image(first/f"frame-{unlocked['frame']}-scene-3.ppm"),
        'restored_level2':image(restore/f"frame-{restored_play['frame']}-scene-3.ppm"),
    }
    run_records=[]
    for path,recorded,meta in ((first,states,metadata),(restore,restored,restore_meta)):
        run_records.append({'directory':relative(path),'metadata':meta,
            'states_sha256':digest(path/'states.jsonl'),'log_sha256':digest(path/'pvz.log'),
            'actual_presented_frames':recorded[-1]['frame'],
            'image_load_failures':max(s['image_load_failures'] for s in recorded),
            'sampled_host_heap_peak_bytes':max(s['heap_sampled_peak'] for s in recorded),
            'host_process_commit_peak_bytes':max(s['process_commit_peak'] for s in recorded),
            'source_cache_peak_bytes':max(s['source_cache_peak'] for s in recorded),
            'max_image_buffer_bytes':max(s.get('image_buffer_bytes',0) for s in recorded),
            'max_dynamic_image_bytes':max(s.get('dynamic_image_bytes',0) for s in recorded),
            'atlas_active_preservation_checks':max(s.get('atlas_active_checks',0) for s in recorded),
            'atlas_encoded_restore_checks':max(s.get('atlas_handle_checks',0) for s in recorded),
            'atlas_pixel_identical_rebuild_checks':max(s.get('atlas_rebuild_checks',0) for s in recorded),
            'atlas_pixel_bytes_released':max(s.get('atlas_pixel_bytes_released',0) for s in recorded),
            'alpha_palette_source_loads':max(s['alpha_palette'][2] for s in recorded),
            'alpha_palette_payload_bytes_read':max(s['alpha_palette'][3] for s in recorded),
            'alpha_palette_failed_loads':max(s['alpha_palette'][4] for s in recorded),
            'a8_generic_argb_source_loads':max(s['alpha_palette'][6] for s in recorded),
            'rgba_font_palette_source_loads':max(s['rgba_font_palette'][2] for s in recorded),
            'rgba_font_palette_payload_bytes_read':max(s['rgba_font_palette'][3] for s in recorded),
            'rgba_font_palette_failed_loads':max(s['rgba_font_palette'][4] for s in recorded),
            'rgba_font_palette_over256_rejections':max(s['rgba_font_palette'][6] for s in recorded),
            'paged_font_loads':max(s['rgba_font_pages'][2] for s in recorded),
            'paged_font_pixel_bytes_read':max(s['rgba_font_pages'][3] for s in recorded),
            'paged_font_failed_preparations':max(s['rgba_font_pages'][4] for s in recorded),
            'generic_rgba_font_argb_loads':max(s['rgba_font_pages'][6] for s in recorded),
            'pcm_samples':recorded[-1]['pcm_samples'],'pcm_nonzero_samples':recorded[-1]['pcm_nonzero']})
    source_files=[ROOT/name for name in ('platform/host/host_io.cpp','platform/host/font_probe.cpp','platform/host/widget_probe.cpp','platform/host/triangle_probe.cpp','platform/host/selector_probe.cpp','platform/host/atlas_entry_probe.cpp','platform/host/allocator_probe.cpp','tools/build_host.py','platform/graphics/memory_patches.py','platform/graphics/data_array_patches.py','platform/graphics/font_parser_patches.py','platform/graphics/widget_patches.py','platform/graphics/AllocatorCache.cpp','platform/graphics/allocator_patches.py','platform/graphics/pool_epoch_patches.py',
        'platform/graphics/ReanimAtlasCache.cpp','platform/graphics/MemoryImageCache.cpp','platform/graphics/MemoryImageCache.h','platform/graphics/GLInterface.cpp','platform/graphics/DefinitionCompaction.cpp','platform/graphics/definition_compaction_patches.py','platform/graphics/SelectorDefinitionCache.cpp','platform/graphics/selector_definition_patches.py','platform/resources/h1_resources.c','platform/resources/h1_resources.h','platform/Main.cpp','platform/Loading.cpp','platform/h1_music.cpp','platform/xmp_shared.c','runtime/pressure_retry.c','runtime/syscalls.c')]
    for tested in (first,restore):
        tested_sources={r['source']:r['sha256'] for r in json.loads((tested/'compile-report.json').read_text())['records']}
        for path in source_files:
            if relative(path) in tested_sources:assert digest(path)==tested_sources[relative(path)]
    verification={
        'scope':'Host-only complete original PvZ-Portable integration; not H1 firmware or physical-device validation.',
        'upstream_commit':metadata['upstream_commit'],
        'prepared_manifest':relative(first/'prepared.json'),
        'prepared_manifest_sha256':metadata['prepared_manifest_sha256'],
        'source_cache_budget_bytes':7*1024*1024,
        'input_only':True,
        'input_only_statement':'Scripts and the optional controller send ordinary mouse/key events through platform/Input.cpp and original widgets. The controller only reads existing board, packet, coin and dialog state; it does not write resources, cooldowns, entity state, timers, profile progression or saves. Saves and unlocks are performed by original game code.',
        'graphics_audit_note':'The platform collects unused atlases before a root draw to the real screen and after completed frames. When enabled, a separate host frame-complete audit verifies active atlas pointers and restored image handles and rebuilds an unused atlas through the original builder. The first run also contains a separately identified root-entry fixture using real definitions/holder/widgets; it checks active and dead holder protection, restored handles, original atlas and complete-screen pixel equality, and nested/offscreen exclusion. These cache fixtures do not manufacture gameplay state.',
        'definition_policy':'The H1 branch uses the original XML definition parser, bypassing optional compiled-cache reads and serialization. Immediately after successful reanimation XML parsing and before original missing-field inheritance, it copies every counted track/frame into exact-sized raw arrays and releases unused growth capacity. All values and original image/font/name/text pointers remain unchanged. These fresh-save runs use no .compiled files.',
        'pool_policy':'Original logical capacities and ID/index fields are retained with stable, value-initialized 32-slot pages. Portable v4 saves are tested through original game code. Legacy raw C++ object layouts are not certified compatible.',
        'clock_note':'These test runs scale the host elapsed clock by 3. This is accelerated integration testing, not a real-time H1 performance measurement.',
        'memory_note':'64-bit host heap/process peaks include host ABI and runtime. The source-image budget covers immutable file-backed pixels only and an active frame can exceed it; these results do not establish the native heap requirement. Per-run prepared/compiled source identities record the tested cache implementation.',
        'verified':{'complete_game_sources_compiled':json.loads((first/'compile-report.json').read_text())['source_count'],'normal_intro_and_menu':True,
            'first_level_waves_observed':sorted({s['wave'] for s in states if s['scene']==3 and s['level']==1}),
            'first_plant':planted,'first_projectile':shot,'first_damage':hit,
            'pause_begin':paused,'pause_held':held,'resume':resumed,
            'four_wave_clear_reward_input':award,'unlocked_level2':unlocked,
            'restored_level2':restored_play,'save_files_after_first_run':saves},
        'runs':run_records,'screenshots':screenshots,
        'implementation_sha256':{relative(path):digest(path) for path in source_files},
        'known_source_asset_warnings':['Optional properties/partner_logo publisher slot uses an explicit transparent fallback.',
            'Original resource repository has no Properties/ZombatarTOS.txt; no replacement terms were invented.'],
        'unit_tests':'platform/graphics/tests/run.py passed: original MemoryImage/SWTri drawing, alpha/tint/additive/mirror/stretch/rotate/matrix/triangles/repeat/coverage/presentation, zero-alpha modulation, metadata-only alpha/grid loading, frame-safe eviction/reload, sanding and mutation preservation.',
    }
    pool_test=ROOT/'build/data-array-tests/verification.json'
    if pool_test.exists():verification['paged_pool_differential']=json.loads(pool_test.read_text())
    cache_test=BUILD/'cache-benchmark/comparison.json'
    if cache_test.exists():verification['source_cache_io_comparison']={
        'path':relative(cache_test),'sha256':digest(cache_test),
        'pixel_payload_reduction_percent':json.loads(cache_test.read_text())['payload_reduction_percent'],
        'note':'Fewer payload bytes are read. More small image loads can replace one large load; this is not a claim that file-open counts decrease.'}
    font_path=BUILD/'run42-staged-probes'
    font_report=font_path/'font-parser-verification.json'
    if font_report.exists():
        font=json.loads(font_report.read_text())
        font_meta=json.loads((font_path/'run-metadata.json').read_text())
        assert font_meta['exit_code']==0 and font_meta['font_probe']
        assert font['all_passed'] and font['font_count']>=17
        for item in font['fonts']:
            assert item['all_define_bytes_equal']
            for key in ('compiled_glyph_kerning_layer','render','scaled_render','changed_point_size_render'):
                before='compiled_glyph_kerning_layer_hash' if key=='compiled_glyph_kerning_layer' else key+'_hash'
                after='compiled_hash_after' if key=='compiled_glyph_kerning_layer' else key+'_hash_after'
                assert item[before]==item[after]
        probe_manifest=json.loads((font_path/'prepared.json').read_text())
        game_manifest=json.loads((first/'prepared.json').read_text())
        prepared_changed=sorted(name for name,value in game_manifest['prepared_sources'].items() if probe_manifest['prepared_sources'].get(name)!=value)
        assert prepared_changed==['Lawn/Board.cpp','Lawn/System/PoolEffect.cpp','PvzpLib/PvzpCommon.cpp','PvzpLib/Reanimator.cpp','SexyAppFramework/graphics/MemoryImage.cpp','SexyAppFramework/widget/WidgetManager.cpp']
        probe_sources={r['source']:r['sha256'] for r in json.loads((font_path/'compile-report.json').read_text())['records']}
        game_sources={r['source']:r['sha256'] for r in json.loads((first/'compile-report.json').read_text())['records']}
        changed=sorted(name for name in game_sources if probe_sources.get(name)!=game_sources[name])
        assert changed==['build/host/source/Lawn/Board.cpp','build/host/source/Lawn/System/PoolEffect.cpp','build/host/source/PvzpLib/PvzpCommon.cpp','build/host/source/PvzpLib/Reanimator.cpp','build/host/source/SexyAppFramework/graphics/MemoryImage.cpp','build/host/source/SexyAppFramework/widget/WidgetManager.cpp','platform/Main.cpp','platform/graphics/AllocatorCache.cpp','platform/graphics/DefinitionCompaction.cpp','platform/graphics/GLInterface.cpp','platform/graphics/MemoryImageCache.cpp','platform/graphics/SelectorDefinitionCache.cpp','platform/host/allocator_probe.cpp','platform/host/atlas_entry_probe.cpp','platform/host/host_io.cpp','platform/host/selector_probe.cpp','platform/resources/h1_resources.c']
        probe_states=[json.loads(line) for line in (font_path/'states.jsonl').read_text().splitlines()]
        assert all(s['source_cache_budget']==6*1024*1024 for s in probe_states)
        verification['retained_diagnostic_probe_scope']={
            'directory':relative(font_path),'source_cache_budget_bytes':6*1024*1024,
            'compiled_source_changes_for_gameplay_run':changed,
            'prepared_source_changes_for_gameplay_run':prepared_changed,
            'note':'These historical diagnostics used 6 MiB and precede root-screen atlas collection, reanimation array compaction, paged RGB blits and exact A8 palette preparation. The font parser compaction remains unchanged. Current gameplay uses 7 MiB plus the recorded changed sources; dedicated current fixtures test those additions. The general graphics unit suite passed the paged implementation before its eligibility cutoff was raised from 480000 to 800000 pixels; current paged fixtures and full-game observations test that cutoff. A8 palettes are limited to validated original alpha-only masks; no general RGBA quantization or pixel arena is enabled.'}
        verification['font_parser_compaction']={
            'report':relative(font_report),'report_sha256':digest(font_report),
            'probe_run_metadata':font_meta,
            'font_count':font['font_count'],'all_define_bytes_equal':True,
            'all_compiled_glyph_kerning_layer_hashes_equal':True,
            'all_normal_scaled_changed_point_size_pixel_hashes_equal':True,
            'before':font['total_before'],'after':font['total_after'],
            'native_raw_request_estimated_saving':font['native_raw_request_estimated_saving'],
            'estimate_note':font['native_estimate_note'],
            'memory_measurement_note':'Font fixture measurements belong to the separate probe run. Complete first-level run uses no font probe.'}
    atlas_report=first/'atlas-entry-verification.json'
    if metadata.get('atlas_entry_probe'):
        atlas=json.loads(atlas_report.read_text())
        assert atlas['all_passed'] and atlas['baseline_screen_hash']==atlas['collected_rebuilt_screen_hash']
        for key in ('active_holder_preserved','dead_holder_preserved','encoded_handles_restored','offscreen_root_preserved','nested_outer_raw_pointer_preserved','physical_frames_unchanged'):
            assert atlas[key]
        assert atlas['collector_bytes_released']>=atlas['unused_atlas_pixel_bytes']>0
        verification['root_screen_entry_atlas_collection']={
            'report':relative(atlas_report),'sha256':digest(atlas_report),'result':atlas,
            'policy':'Before any root WidgetManager::DrawScreen survey/Graphics/DrawAll, the existing collector runs only at drawing depth one and only for the actual GL screen. It preserves every holder definition, including dead instances pending original deletion. It restores encoded Image handles before freeing unused atlas pixels. Nested DrawScreen and all offscreen draws do not authorize collection. This introduces no extra present and no pixel-size, capacity or gameplay changes.'}
    definition_path=ROOT/'build/definition-compaction-probe'
    definition_report=definition_path/'run/definition-compaction-verification.json'
    if definition_report.exists():
        definition=json.loads(definition_report.read_text())
        identity_path=definition_path/'identity.json'
        identity=json.loads(identity_path.read_text())
        assert identity['test_only'] and identity['baseline_loader_no_automatic_compaction']
        assert definition['test_only'] and definition['all_passed'] and definition['definition_count']==146
        assert definition['allocation_failure_preserves_old_arrays']
        for item in definition['definitions']:
            assert item['all_field_bits_and_pointers_equal'] and item['text_content_equal'] and item['atlas_and_render_equal']
        helper=next(item for name,item in identity['objects'].items() if name.replace('\\','/').endswith('/DefinitionCompaction.cpp'))
        assert helper['source_sha256']==digest(ROOT/'platform/graphics/DefinitionCompaction.cpp')
        assert 'PVZH1_COMPACT_DEFINITION_ARRAYS' not in (definition_path/'Reanimator.cpp').read_text(encoding='utf8')
        verification['reanimation_array_compaction']={
            'report':relative(definition_report),'report_sha256':digest(definition_report),
            'test_only_identity':relative(identity_path),'identity_sha256':digest(identity_path),
            'definition_count':definition['definition_count'],'tracks':definition['tracks'],'frames':definition['frames'],
            'all_field_bits_and_original_pointers_equal':True,'all_four_pose_screen_and_atlas_hashes_equal':True,
            'optional_allocation_failure_preserves_old_arrays':True,
            'native_full_library_array_payload_saving_bytes':definition['native_bytes_saved'],
            'largest_native_exact_array_bytes':definition['largest_native_new_array'],
            'note':'The standalone fixture freezes an original loader without the automatic hook, original host objects and the same compaction helper source. It compares the same parsed definition before/after manual compaction, preserving image/font/text/name pointer identity, and renders four time points through original atlas/drawing code. It is test-only, distinct from the current automatically compacted full-game runs. The saving covers all 146 resource definitions and is not a claim about definitions loaded in a particular native scene or contiguous free space.'}
    rgb_path=ROOT/'build/paged-rgb-probe'
    rgb_report=rgb_path/'run/paged-rgb-verification.json'
    rgb=json.loads(rgb_report.read_text())
    rgb_identity=rgb_path/'identity.json'
    rgb_identity_data=json.loads(rgb_identity.read_text())
    assert rgb_identity_data['fresh_header_ABI'] and rgb_identity_data['H1Image_header']==digest(ROOT/'platform/resources/h1_resources.h')
    for name in ('platform/graphics/MemoryImageCache.cpp','platform/graphics/GLInterface.cpp','platform/resources/h1_resources.c'):
        tested=(rgb_identity_data.get('paged_font_cache_override',{}) or rgb_identity_data.get('rgba_font_cache_override',{}) or rgb_identity_data.get('a8_cache_override',{}) or rgb_identity_data.get('same_ABI_cache_override',{})) if name=='platform/graphics/MemoryImageCache.cpp' else {}
        assert (tested or rgb_identity_data['sources'][name])['source']==digest(ROOT/name)
    assert rgb['test_only'] and rgb['fresh_header_ABI'] and rgb['all_passed'] and rgb['case_count']==15
    assert all(c['equal'] and c['hash']==c['candidate_hash'] for c in rgb['cases'])
    for key in ('current_epoch_pinned','completed_epoch_reclaimed','partial_allocation_and_read_failure_cleanup','cpp_allocations_balanced_on_failure','pages_freed_before_argb_allocation','mutation_forgets_pages','warm_frame_no_file_reads'):
        assert rgb[key]
    observations=[]
    for recorded in (states,restored):
        seen=[]
        for state in recorded:
            for source in state.get('paged_rgb_sources',[]):
                if source['path'].lower()=='images/background1unsodded' and source['blits']:
                    assert source['pages']==821 and source['bytes']==2520000 and source['max_page']==3072
                    assert source['loads']==1 and source['argb_loads']==0
                    assert not source['argb_resident'] and not source['rl_resident']
                    seen.append({'frame':state['frame'],'scene':state['scene'],'cutscene_time':state.get('cutscene_time'),**source})
        assert len(seen)>=3
        observations.append(seen)
    intro=[s for s in observations[0] if s['scene']==2 and s['cutscene_time'] is not None]
    assert min(s['cutscene_time'] for s in intro)<=100 and max(s['cutscene_time'] for s in intro)>=6000
    assert max(s['cutscene_time'] or 0 for s in observations[0])>=8550
    legacy_title=[{'frame':state['frame'],**image} for state in states if state['scene']==0
        for image in state.get('largest_images',[]) if image['path'].lower()=='images/titlescreen']
    assert legacy_title and max(image['bytes'] for image in legacy_title)==2400004
    assert not any(source['path'].lower()=='images/titlescreen' and source['pages'] for state in states for source in state.get('paged_rgb_sources',[]))
    raw_report=ROOT/'assets/raw-rgb-verification.json'
    raw=json.loads(raw_report.read_text())
    assert raw['result']=='PASS' and raw['sequential_pages']==821 and raw['sequential_fseek_calls']==0
    for name in ('platform/resources/h1_resources.c','platform/resources/h1_resources.h'):
        assert raw['tested_files'][name]==digest(ROOT/name)
    verification['lossless_paged_rgb_background']={
        'renderer_report':relative(rgb_report),'renderer_report_sha256':digest(rgb_report),
        'test_only_identity':relative(rgb_identity),'identity_sha256':digest(rgb_identity),
        'result':rgb,'raw_reader_report':relative(raw_report),'raw_reader_report_sha256':digest(raw_report),
        'gameplay_background_observations':observations,
        'rgb_minimum_pixels':800000,'title_original_buffer_observations':legacy_title,
        'policy':'Only validated immutable RGB3 files without alpha-grid data, at least 800000 pixels, can use this path. The 800x600 Title retains its original contiguous ARGB/RL path. GLInterface requires identity integer positioning, normal white drawing and an opaque screen target. Original clipping/offscreen early returns are preserved. Source pages hold unchanged RGB bytes in 1024-pixel/3072-byte chunks, converted directly into exact opaque ARGB destination pixels. Other drawing modes use original GetBits/raster behavior after releasing paged storage. Pages/table participate in cache budget, epoch pins, eviction, pressure, mutation and destruction.',
        'native_background_storage':'For the original 1400x600 background: 821 data allocations, 2520000 RGB bytes plus 3284 native pointer-table bytes; last page 960 bytes. Header/alignment/firmware descriptors are extra. No 3360004-byte source ARGB or 840000-byte RL allocation is used on the observed original Intro/camera-pan/first-level path.',
        'scope_note':'Current full-game observations prove this background stays paged and loads once per descriptor throughout warm draws; unbounded host memory therefore does not conceal a GetBits materialization. The standalone baseline uses the current ABI and original MemoryImage::Blt, while candidate calls the production GLInterface/cache. Native contiguous-space and performance results still require firmware validation.'}
    alpha_path=ROOT/'build/alpha-loader-probe'
    alpha_report=alpha_path/'run/alpha-loader-verification.json'
    alpha=json.loads(alpha_report.read_text())
    alpha_identity=alpha_path/'identity.json'
    alpha_ids=json.loads(alpha_identity.read_text())
    assert alpha['test_only'] and alpha['production_loader'] and alpha['all_passed']
    assert alpha['case_count']==8 and all(c['equal'] and c['argb_arrays']==0 for c in alpha['direct_blits'])
    for key in ('current_palette_pinned','completed_palette_reclaimed_and_reloaded','warm_reads_unchanged','four_partial_failure_cleanups_balanced','getbits_expands_exact_argb_once','existing_raw_loan_preserved','self_blit_skips_preparation','compose_color_all_256_entries_exact','rgb_rgba_grid_sanded_dynamic_rejected'):
        assert alpha[key]
    assert alpha_ids['H1Image_header']==digest(ROOT/'platform/resources/h1_resources.h')
    assert alpha_ids['objects']['platform/graphics/MemoryImageCache.cpp']['source']==digest(ROOT/'platform/graphics/MemoryImageCache.cpp')
    glyph_path=ROOT/'build/alpha-palette-probe'
    glyph_report=glyph_path/'run/alpha-palette-verification.json'
    glyph=json.loads(glyph_report.read_text())
    glyph_identity=glyph_path/'identity.json'
    glyph_ids=json.loads(glyph_identity.read_text())
    assert glyph_ids['objects']['platform/graphics/MemoryImageCache.cpp']['source']==digest(ROOT/'platform/graphics/MemoryImageCache.cpp')
    assert glyph['production_loader'] and glyph['all_passed'] and glyph['all_pixels_equal']
    assert len(glyph['cases'])==18 and glyph['no_expansion_cases']==12 and glyph['expanded_cases']==6
    assert all(c['baseline_hash']==c['palette_hash'] for c in glyph['cases'])
    assert all(s['alpha_palette'][4]==0 for s in states+restored)
    assert all(s['alpha_palette'][6]==0 for s in states)
    verification['lossless_a8_mask_palette']={
        'loader_report':relative(alpha_report),'loader_report_sha256':digest(alpha_report),
        'loader_test_identity':relative(alpha_identity),'loader_identity_sha256':digest(alpha_identity),'loader_result':alpha,
        'glyph_report':relative(glyph_report),'glyph_report_sha256':digest(glyph_report),
        'glyph_test_identity':relative(glyph_identity),'glyph_identity_sha256':digest(glyph_identity),'glyph_result':glyph,
        'reproduce':'python platform/graphics/tests/run_alpha_palette.py after building the current complete host',
        'actual_game_observations':[[{'frame':s['frame'],'event':s['event'],'scene':s['scene'],'diagnostic':s['alpha_palette']} for s in recorded if s['event'] in ('scene','loading-complete','closed')] for recorded in (states,restored)],
        'policy':'Before NormalBlt/AdditiveBlt borrow either source representation, unopened immutable sources may load exact original alpha bytes as indices and a 256-entry table (alpha<<24)|originalComposeRGB. Both metadata and reopened headers must identify A8 mask-only data. RGB/RGBA, alpha grids, sanding, changed paths/dimensions, existing ARGB or derived representations, and self blits are excluded. No existing raw pointer is invalidated. Original palette raster/NativeAlpha/RL code, epoch pins, budget, pressure, mutation and destruction remain in use. Generic GetBits retains the original exact ARGB expansion; scaling and point-size fixtures intentionally exercise that fallback.',
        'memory_note':'Each 4169x54 Font32 source starts with 225126 alpha-index bytes plus a 1024-byte compose table, rather than a 900508-byte ARGB allocation. NativeAlpha, if needed, uses the original 256-entry table; original RL data and allocation overhead are additional. The fresh Intro/first-level counters record no generic A8 source-file ARGB loads; both runs have zero A8 preparation failures. Restore/menu may use the retained generic fallback, whose counter is recorded per run. The standalone glyph cases separately prove no ARGB expansion for ordinary-size draws and pixel-identical expansion for scaled/point-size draws.'}
    rgba_path=ROOT/'build/rgba-font-palette-probe'
    rgba_report=rgba_path/'run/rgba-font-palette-verification.json'
    rgba=json.loads(rgba_report.read_text())
    rgba_identity=rgba_path/'identity.json'
    rgba_ids=json.loads(rgba_identity.read_text())
    assert rgba['production_loader'] and rgba['all_passed'] and rgba['case_count']==8 and rgba['actual_house28_glyph_cases']==8
    assert all(c['equal'] and c['argb_arrays']==0 for c in rgba['direct_blits'])
    for key in ('current_palette_pinned','completed_palette_reclaimed_and_reloaded','warm_reads_unchanged','four_partial_failure_cleanups_balanced','getbits_expands_exact_argb_once','existing_raw_loan_preserved','self_blit_skips_preparation','all_43_full_rgba_colors_exact','nonfont_rgba_rgb_grid_sanded_dynamic_rejected','color257_rejected_without_loss_or_leak'):
        assert rgba[key]
    assert rgba_ids['objects']['platform/graphics/MemoryImageCache.cpp']['source']==digest(ROOT/'platform/graphics/MemoryImageCache.cpp')
    assert all(s['rgba_font_palette'][4]==0 for s in states+restored)
    assert max(s['rgba_font_palette'][2] for s in states)>0 and max(s['rgba_font_palette'][6] for s in states)>0
    inventory=ROOT/'assets/rgba-font-palette-inventory.json'
    verification['lossless_rgba_font_palette']={
        'report':relative(rgba_report),'report_sha256':digest(rgba_report),
        'test_only_identity':relative(rgba_identity),'identity_sha256':digest(rgba_identity),'result':rgba,
        'inventory':relative(inventory),'inventory_sha256':digest(inventory),
        'reproduce':'python tools/tests/test_rgba_font_palette.py after building the current complete host',
        'actual_game_observations':[[{'frame':s['frame'],'event':s['event'],'scene':s['scene'],'diagnostic':s['rgba_font_palette']} for s in recorded if s['event'] in ('scene','loading-complete','first-projectile','first-hit','closed')] for recorded in (states,restored)],
        'policy':'Only immutable images within the normalized logical data/ directory, with both recorded and actual reopened RGBA4/flags0 headers, may be scanned into an exact full-ARGB palette. A bounded 512-slot lookup preserves every 32-bit color including RGB at zero alpha. At 257 distinct colors the optimization is rejected and memoized only for the immutable descriptor lifetime; original GetBits remains available with no quantization. Existing pixel buffers, source/destination self blits, alpha grids, sanding and changed paths/dimensions are excluded. A8 masks retain their separate compose-color path. RGB3 images, including Title, never enter this feature.',
        'memory_note':'The original 4325x54 HouseOfTerror28 uses 43 complete RGBA colors. Its initial indexed representation is 233550 bytes plus the 1024-byte table, replacing a 934204-byte ARGB allocation; original optional RL/native-alpha data and allocator costs are additional. This does not promise that every font is eligible or that generic scaled drawing avoids its original contiguous expansion.'}
    font_page_path=ROOT/'build/font-page-probe'
    font_page_report=font_page_path/'run/font-page-verification.json'
    font_page=json.loads(font_page_report.read_text())
    font_page_identity=font_page_path/'identity.json'
    font_page_ids=json.loads(font_page_identity.read_text())
    assert font_page['test_only'] and font_page['all_passed'] and font_page['case_count']==8 and font_page['actual_dwarf36_glyph_cases']==8
    for key in ('opaque_and_transparent_crop_full_source_mode_exact','drawn_and_bitschanged_side_effects_equal','cross_row_and_page_copy_exact','current_pages_pinned','old_pages_reclaimed_and_reloaded','warm_reads_unchanged','five_partial_page_failures_balanced','tile_failure_no_writes_and_cleans_pages','getbits_frees_pages_before_full_argb','all_full_rgba_and_canary_equal','existing_raw_loan_preserved','self_oversize_grid_sanded_rejected'):
        assert font_page[key]
    assert font_page['page_count']==271 and font_page['rgba_page_bytes']==1109200 and font_page['max_page_bytes']==4096
    assert font_page_ids['objects']['platform/graphics/MemoryImageCache.cpp']['source']==digest(ROOT/'platform/graphics/MemoryImageCache.cpp')
    assert all(s['rgba_font_pages'][4]==0 for s in states+restored)
    dwarf_observed=[]
    for recorded in (states,restored):
        seen=[]
        for state in recorded:
            for source in state.get('paged_font_sources',[]):
                if source['path'].lower()=='data/dwarventodcraft36greeninset' and source['blits']:
                    assert source['loads']==1 and source['argb_loads']==0 and not source['argb_resident'] and not source['rl_resident']
                    if source['pages']:
                        assert source['pages']==271 and source['bytes']==1109200 and source['max_page']==4096
                        seen.append({'frame':state['frame'],'event':state['event'],**source})
        assert seen
        dwarf_observed.append(seen)
    verification['lossless_paged_rgba_font_glyphs']={
        'report':relative(font_page_report),'report_sha256':digest(font_page_report),
        'test_only_identity':relative(font_page_identity),'identity_sha256':digest(font_page_identity),'result':font_page,
        'reproduce':'python tools/tests/test_font_pages.py after building the current complete host',
        'actual_default_options_font_observations':dwarf_observed,
        'policy':'An immutable RGBA font in normalized data/ with more than 256 colors may retain exact 32-bit pixels in 1024-pixel/4096-byte allocations. Before Normal/Additive blits borrow source pointers, an already-clipped glyph rectangle bounded to 256x256 and at most 16384 pixels is copied into a small dynamic MemoryImage. Its image-mode flags match the complete original source, and the original MemoryImage::Blt performs all blend arithmetic. There is no new pixel approximation or quantization. Empty/outside paths retain original clipping, and self/modified/grid/sanded/existing-buffer sources do not enter the helper. Pages participate in size, largest-buffer selection, current-epoch pins, pressure, frame eviction, mutation and destruction; generic GetBits frees them before the original contiguous expansion.',
        'memory_note':'Dwarven36GreenInset is 4700x59 with 6226 colors: 271 pages contain its unchanged 1109200 pixel bytes, plus 1084 native pointer-table bytes. The full 1109204-byte source ARGB allocation and full-source native-alpha/RL buffers are absent on the observed ordinary Options glyph path. Small temporary glyph ARGB/RL buffers and allocation overhead remain. Scaled/transformed or oversized source requests retain the original generic fallback and are not certified free of large contiguous allocations.'}
    allocator_report=first/'allocator-cache-verification.json'
    allocator=json.loads(allocator_report.read_text())
    assert metadata['allocator_probe'] and allocator['all_passed']
    assert allocator['real_plant_screen_hash']==allocator['after_trim_screen_hash']
    for key in ('active_and_dead_pointers_preserved','empty_pool_identity_preserved','calloc_regrow_exact','v4_byte_identical'):
        assert allocator[key]
    assert (first/'allocator-before.v4').read_bytes()==(first/'allocator-after.v4').read_bytes()
    policy_report=ROOT/'build/global-allocator-policy-test/report.json'
    policy=json.loads(policy_report.read_text());assert policy['result']=='PASS'
    pool_report=ROOT/'build/pool-epoch-probe/run/pool-epoch-verification.json'
    pool=json.loads(pool_report.read_text());assert pool['all_passed'] and pool['case_count']==16
    assert all(c['pixels_equal'] and c['baseline_hash']==c['candidate_hash'] for c in pool['cases'])
    assert pool['nested_and_offscreen_outer_raw_pixels_preserved'] and pool['physical_frame_count_unchanged']
    pool_identity=ROOT/'build/pool-epoch-probe/identity.json'
    pool_ids=json.loads(pool_identity.read_text())
    assert pool_ids['production_patched_pool_sha256']==digest(ROOT/'build/host/source/Lawn/System/PoolEffect.cpp')
    assert pool_ids['pool_patch_sha256']==digest(ROOT/'platform/graphics/pool_epoch_patches.py')
    samples=[]
    for state in states:
        if state['scene']!=2:continue
        pools=state.get('global_allocator_pools',[])
        for p in pools:
            assert p['grow']==max(1,min(16,4096//p['item_size']))
            assert p['capacity']>=p['live']
        samples.append({'frame':state['frame'],'cutscene_time':state.get('cutscene_time'),
            'host_slab_bytes':sum(p['bytes'] for p in pools),
            'host_unused_slot_bytes':sum((p['capacity']-p['live'])*p['item_size'] for p in pools),
            'host_empty_retained_bytes':sum(p['bytes'] for p in pools if p['live']==0)})
    assert samples and any(s['host_empty_retained_bytes']==0 for s in samples)
    verification['global_allocator_refill_and_empty_collection']={
        'real_game_fixture':relative(allocator_report),'fixture_sha256':digest(allocator_report),'result':allocator,
        'original_v4_before_sha256':digest(first/'allocator-before.v4'),'original_v4_after_sha256':digest(first/'allocator-after.v4'),
        'differential_report':relative(policy_report),'differential_report_sha256':digest(policy_report),
        'actual_intro_host_samples':samples,
        'policy':'Only first initialization chooses clamp(4096/itemSize,1,16) on H1. This changes refill batch size, not logical capacity; existing live pool settings never change and further Grow remains unlimited. At root screen entry, only global pools with zero outstanding allocations release all original slabs. Allocator address, index, item size and grow count remain valid for empty lists and future regrowth. Dead reanimations retain their outstanding allocations until original deletion. DataArray capacities and IDs are unchanged.',
        'measurement_note':'Host samples may occur after drawing has created and released temporary instances; those newly empty slabs are eligible at the next root entry. Sizes use host pointers and 112-byte TrackInstances. Target ABI is independently compiled as 4-byte pointers/96-byte TrackInstances; host totals do not assert native charged space or contiguous-hole size.'}
    verification['completed_pool_texture_epochs']={
        'report':relative(pool_report),'report_sha256':digest(pool_report),'identity':relative(pool_identity),'identity_sha256':digest(pool_identity),'result':pool,
        'policy':'The completed Board backdrop advances epoch immediately before its pool effect, and each original complete 150-triangle pool texture batch advances after returning. Calls require the actual screen and existing outermost-screen scope. Pool callbacks retain vertex/image metadata and dynamic caustic pixels, no immutable file pixel pointers. Nested/offscreen calls cannot release outer raw pointers. Original pool wave math, all three layers and day/night colors are unchanged.'}
    selector_gc_path=ROOT/'build/selector-gc-probe'
    selector_gc_report=selector_gc_path/'run/selector-definition-gc-verification.json'
    selector_gc=json.loads(selector_gc_report.read_text())
    selector_gc_identity=selector_gc_path/'identity.json'
    gc_ids=json.loads(selector_gc_identity.read_text())
    helper=next(value for name,value in gc_ids['compiled_sources'].items() if name.replace('\\','/').endswith('/SelectorDefinitionCache.cpp'))
    assert helper==digest(ROOT/'platform/graphics/SelectorDefinitionCache.cpp')
    assert selector_gc['all_passed'] and selector_gc['generation_count']==6
    assert all(g['owned_names_deleted_once']==48 and g['definition_default'] and g['render_equal'] for g in selector_gc['generations'])
    assert len({g['full_field_hash'] for g in selector_gc['generations']})==1
    for key in ('live_and_dead_holders_protected','real_menu_widget_protected','credits_scene_protected','ownership_balanced','shutdown_and_second_free_safe'):
        assert selector_gc[key]
    assert selector_gc['load_guard_checks']>0
    first_menu=next(s for s in states if s['event']=='scene' and s['scene']==1)
    first_intro=next(s for s in states if s['event']=='scene' and s['scene']==2)
    return_menu=next(s for s in restored if s['event']=='scene' and s['scene']==1 and s['selector_definition_cache'][0]>=2)
    assert first_menu['selector_definition_loaded'] and first_menu['selector_definition_cache'][5]==48
    assert not first_intro['selector_definition_loaded'] and first_intro['selector_definition_cache'][2]>=1
    assert first_intro['selector_definition_cache'][3:6]==[48,48,0]
    assert return_menu['selector_definition_loaded'] and return_menu['selector_definition_cache'][5]==48
    assert states[-1]['selector_definition_cache'][3]==states[-1]['selector_definition_cache'][4]
    assert restored[-1]['selector_definition_cache'][3]==restored[-1]['selector_definition_cache'][4]
    verification['selector_only_definition_retirement']={
        'report':relative(selector_gc_report),'report_sha256':digest(selector_gc_report),
        'test_only_identity':relative(selector_gc_identity),'identity_sha256':digest(selector_gc_identity),'result':selector_gc,
        'actual_initial_menu':first_menu['selector_definition_cache'],'actual_first_intro':first_intro['selector_definition_cache'],
        'actual_returned_menu':return_menu['selector_definition_cache'],
        'native_counted_payload_estimate_bytes':33888*44+48*12+762,
        'policy':'Only the global SelectorScreen slot is eligible. A fixed 512-pointer table captures new nonempty name/text allocations immediately after successful XML parsing, before inheritance; no runtime allocation tagging or string interning is used. Load-in-progress, incomplete/overflow capture, SelectorWidget, all holder references including dead entries, Credits and pre-Board phases prevent retirement. At the stable real-screen root boundary, original FreeDefinition runs, captured owned strings are deleted once and the entire definition is reset to defaults. Images/fonts/static parameters remain owned by their original managers. A later menu uses the original parser again. Shutdown uses the same ownership cleanup.',
        'estimate_note':'The target payload estimate uses the pinned 48-track/33888-transform XML and 762 owned-name bytes, excluding allocator headers/descriptors/unused capacity. Host diagnostic bytes use its larger pointer ABI. The six-generation fixture validates semantic fields, original image/font references and four render times; normal fresh/v4/menu runs separately validate real retirement and reload.'}
    widget_path=BUILD/'run42-staged-probes'
    widget_report=widget_path/'widget-epoch-verification.json'
    if widget_report.exists():
        widget=json.loads(widget_report.read_text())
        widget_meta=json.loads((widget_path/'run-metadata.json').read_text())
        assert widget_meta['exit_code']==0 and widget_meta['widget_probe']
        assert widget['all_passed']
        assert widget['screen_baseline_pixel_hash']==widget['screen_pressure_pixel_hash']
        assert widget['offscreen_baseline_pixel_hash']==widget['offscreen_pressure_pixel_hash']
        assert widget['nested_screen_outer_pointer_safe'] and widget['nested_offscreen_outer_pointer_safe']
        assert widget['epoch_does_not_evict_or_present'] and widget['deferred_overlay_reload_equal']
        verification['completed_widget_epoch']={
            'report':relative(widget_report),'sha256':digest(widget_report),'result':widget,
            'policy':'The widget-level boundary advances pixel-use epoch only after a complete top-level WidgetManager::DrawScreen Widget::DrawAll return. DrawWidgetsTo participates in the nesting guard but never advances it. Finer synchronous triangle/matrix/atlas boundaries are recorded separately. Epoch callbacks do not allocate, evict, clear or present. Allocation pressure releases only sources from completed epochs; current-epoch getters remain pinned.'}
    triangle_path=BUILD/'run42-staged-probes'
    triangle_report=triangle_path/'triangle-epoch-verification.json'
    if triangle_report.exists():
        triangle=json.loads(triangle_report.read_text())
        triangle_meta=json.loads((triangle_path/'run-metadata.json').read_text())
        assert triangle_meta['exit_code']==0 and triangle_meta['triangle_probe']
        assert triangle['all_passed'] and triangle['baseline_pixel_hash']==triangle['pressure_pixel_hash']
        for key in ('actual_screen_only','offscreen_pixels_equal','nested_outer_raw_pointer_safe','atlas_noop_paths_do_not_advance_epoch','physical_frame_count_preserved'):
            assert triangle[key]
        verification['completed_triangle_and_atlas_epochs']={
            'report':relative(triangle_report),'sha256':digest(triangle_report),'result':triangle,
            'policy':'A nonempty triangle group advances epoch only after synchronous rendering and reset, on the actual screen in a single top-level screen draw. A newly built atlas advances only after builder/LOW_MEMORY completion, outside drawing or in that screen scope. Existing/NO_ATLAS early returns, offscreen and nested draws never advance either boundary. All retained caller/group references are Image pointers and vertex/UV metadata, not source pixel pointers.'}
    selector_path=BUILD/'run42-staged-probes'
    selector_report=selector_path/'selector-no-atlas-verification.json'
    if selector_report.exists():
        selector=json.loads(selector_report.read_text())
        selector_meta=json.loads((selector_path/'run-metadata.json').read_text())
        assert selector_meta['exit_code']==0 and selector_meta['selector_probe']
        assert selector['all_passed'] and selector['no_atlas']
        assert selector['definition_tracks']==48 and selector['definition_transforms']==33888
        assert selector['matrix_calls']>=15 and all(n>0 for n in selector['background_matrix_calls'])
        assert selector['baseline_pixel_hash']==selector['pressure_pixel_hash']
        assert selector['extra_draw_baseline_pixel_hash']==selector['extra_draw_pressure_pixel_hash']
        assert selector['offscreen_pixels_equal'] and selector['nested_outer_raw_pointer_safe'] and selector['physical_frame_count_preserved']
        verification['real_no_atlas_selector_matrix_epoch']={
            'report':relative(selector_report),'sha256':digest(selector_report),'result':selector,
            'policy':'Reanimation::ReanimBltMatrix advances only after either synchronous draw path has returned and fast-path Graphics state is restored, with actual-screen and top-level screen-scope checks. The complete original Selector definition is tested via original DrawRenderGroup/DrawTrack, including all three BG images and subsequent additive/overlay image re-fetches. This covers the real NO_ATLAS selector path, independently of the separate triangle fixture.'}
    graphics_report=ROOT/'build/graphics-tests/verification.json'
    retry_report=ROOT/'build/pressure-retry-test/report.json'
    if graphics_report.exists() and retry_report.exists():
        retry=json.loads(retry_report.read_text())
        assert retry['passed'] and retry['sources']['runtime/pressure_retry.c']==digest(ROOT/'runtime/pressure_retry.c')
        assert json.loads(graphics_report.read_text())['passed']
        verification['staged_allocation_pressure']={
            'graphics_unit_report':relative(graphics_report),'graphics_report_sha256':digest(graphics_report),
            'runtime_retry_report':relative(retry_report),'runtime_retry_report_sha256':digest(retry_report),
            'runtime_retry':retry,
            'policy':'Each cache callback releases one eligible completed-epoch source, preferring sources unused in the current physical frame, then the smallest individual backing buffer that fits the requested user bytes, then LRU. The native runtime retries the actual raw allocation after each release and stops immediately on success or when no eligible source remains. Released byte sums are never treated as proof of contiguous space.',
            'fixture_note':'Forced-pressure image fixtures repeat callbacks through eligible-source exhaustion to check worst-case pixel/pointer safety. Actual allocation failure, contiguous/coalescing behavior, early success and reentry are covered by the separate production C retry-module tests. Host gameplay uses the host allocator; native I/O improvement must be measured on firmware.',
            'native_cache_diagnostic':'pvzh1_cache_diagnostic[6]: resident bytes, sampled source peak bytes, loads, evictions, physical frames, loaded pixel payload bytes low32; updated without allocation or logging at existing recount/end-frame/pressure points.'}
    old_path=BUILD/'run20-unpaged-v4-compat'
    if (old_path/'run-metadata.json').exists():
        old,old_states,old_meta=read_run(old_path.name)
        unpaged_manifest=BUILD/'run17-final-first/prepared.json'
        old_source=json.loads(unpaged_manifest.read_text())['prepared_sources']['PvzpLib/DataArray.h']
        assert old_source==digest(ROOT/'upstream/src/PvzpLib/DataArray.h')
        paged_source=json.loads((first/'prepared.json').read_text())['prepared_sources']['PvzpLib/DataArray.h']
        assert paged_source!=old_source
        assert 'Loaded save game (v4)' in (old/'pvz.log').read_text()
        old_restored=next(s for s in old_states if s['event']=='scene' and s['scene']==3 and not s['dialogs'])
        assert old_restored['level']==old_restored['profile_level']==2 and old_states[-1]['scene']==1
        verification['unpaged_v4_backward_compatibility']={
            'source_run':'run17-final-first','unpaged_header_sha256':old_source,'paged_header_sha256':paged_source,
            'input_save_manifest':relative(BUILD/'save20-unpaged-v4/original-unpaged-save-manifest.json'),
            'input_save_manifest_sha256':digest(BUILD/'save20-unpaged-v4/original-unpaged-save-manifest.json'),
            'test_run':relative(old),'metadata':old_meta,'restored_state':old_restored,
            'returned_to_original_menu':True,'image_load_failures':0,'states_sha256':digest(old/'states.jsonl')}
    target=BUILD/'verification.json'
    target.write_text(json.dumps(verification,indent=2)+'\n')
    print(relative(target))

if __name__=='__main__':main()
