"""Bind the final texture/label change without relabeling earlier gameplay runs."""
from verification import ROOT,BUILD,digest,relative,read_run,image
import json

def main():
    path,states,meta=read_run('run66-raster-pages-resume-menu')
    assert meta['read_only_entity_input_controller'] and 'Loaded save game (v4)' in (path/'pvz.log').read_text()
    assert any(s['event']=='resume-save-input' for s in states)
    played=next(s for s in states if s['scene']==3 and not s['dialogs'])
    assert played['level']==played['profile_level']==2 and states[-1]['scene']==1
    labels=[{'frame':s['frame'],'id':d['id'],'label':d['restart_label']} for s in states for d in s['dialogs'] if 'restart_label' in d]
    assert len({entry['id'] for entry in labels})==2 and all(entry['label']=='Restart Level' for entry in labels)
    original_menu=next(s for s in states if s['event']=='scene' and s['scene']==1)
    returned=next(s for s in states if s['event']=='scene' and s['scene']==1 and s['frame']>played['frame'])
    def right(state):return next(p for p in state['paged_font_sources'] if p['path'].lower()=='reanim/selectorscreen_bg_right')
    startup_argb=right(original_menu)['argb_loads']
    observed=[]
    for state in states:
        if state['frame']<returned['frame'] or state['event']=='closed':continue
        source=right(state)
        assert source['pages']==400 and source['bytes']==1635200 and source['max_page']==4096
        assert source['loads']==1 and source['argb_loads']==startup_argb
        assert not source['argb_resident'] and not source['rl_resident']
        observed.append({'frame':state['frame'],'raster_diagnostic':state['rgba_raster_pages'],**source})
    assert observed and all(s['rgba_raster_pages'][4]==0 for s in states)
    compiled=json.loads((path/'compile-report.json').read_text())
    for item in compiled['records']:assert digest(ROOT/item['source'])==item['sha256']
    sources=['platform/graphics/MemoryImageCache.cpp','platform/graphics/MemoryImageCache.h',
        'platform/graphics/memory_patches.py','platform/graphics/SoftwareRaster.cpp',
        'platform/graphics/texture_page_patches.py','platform/source_patches.py']
    fixture=ROOT/'build/texture-page-probe/run/texture-page-verification.json'
    identity=ROOT/'build/texture-page-probe/identity.json'
    result=json.loads(fixture.read_text());assert result['all_passed']
    ids=json.loads(identity.read_text())
    assert ids['H1Image_header']==digest(ROOT/'platform/resources/h1_resources.h')
    for name in ('platform/graphics/MemoryImageCache.cpp','platform/graphics/SoftwareRaster.cpp','build/host/source/SexyAppFramework/graphics/SWTri.cpp'):
        assert ids['objects'][name]['source']==digest(ROOT/name)
    historical=BUILD/'verification.json'
    menu=sorted(path.glob('frame-*-scene-1.ppm'),key=lambda p:int(p.name.split('-')[1]))[-1]
    report=dict(scope='Final host-only v4/Continue/Options/return-menu integration of paged transformed textures and two button labels; not a repeat of the earlier full first-level test.',
        input_only=True,input_statement='The existing controller and script send ordinary Input.cpp key/mouse events; game fields and saves are not written by the test.',
        run=relative(path),metadata=meta,actual_presented_frames=states[-1]['frame'],
        states_sha256=digest(path/'states.jsonl'),image_failures=0,
        current_implementation_sha256={name:digest(ROOT/name) for name in sources},
        historical_full_game_report=relative(historical),historical_full_game_report_sha256=digest(historical),
        historical_scope='Run64/65 proved full Intro/first-level four-wave win/reward/level2/v4/pause using the previous shader and labels. Their original source identities remain unchanged in that report. Current additions are covered by this integration run and the actual Selector/texel differential fixture.',
        fixture=relative(fixture),fixture_sha256=digest(fixture),fixture_identity=relative(identity),fixture_identity_sha256=digest(identity),fixture_result=result,
        restart_buttons=labels,initial_selector_argb_loads=startup_argb,returned_selector_observations=observed,
        source_policy='Initial startup sanding remains unchanged. An evicted sanded RGBA source reloads into 4KiB pages and reuses the original AverageNearByPixels callback through three row buffers. All post-sanding ARGB values, including transparent RGB, are preserved. SWTri changes only five texture reads; original unsigned linear indices, guards, UVs, interpolation and blend math remain intact. RGB3 Title is excluded. Raster-origin diagnostics remain separate from font-origin storage.',
        menu_screenshot=image(menu,ROOT/'docs/host-current-menu.png'))
    target=BUILD/'final-ui-verification.json';target.write_text(json.dumps(report,indent=2)+'\n')
    print(relative(target));print(digest(target))

if __name__=='__main__':main()
