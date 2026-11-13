"""Optional boundary-only counters for an isolated native diagnostic profile.

The normal build preprocesses the additions away. Pinned game statements and
return paths stay byte-for-byte unchanged; scopes also cover early returns.
"""
from pathlib import Path

GUARD = '#if defined(H1_PVZ_PORTABLE) && defined(PVZ_NATIVE_PROFILE)'
INCLUDE = GUARD + '\n#include "profiler.h"\n#endif\n'


def apply(source_root):
    root = Path(source_root)
    for relative, signature, category, label in (
        ('SexyAppFramework/SexyAppBase.cpp', 'bool SexyAppBase::DoUpdateFrames()',
         'PVZ_PROFILE_UPDATE', 'Update'),
        ('SexyAppFramework/SexyAppBase.cpp', 'void SexyAppBase::DoUpdateFramesF(float theFrac)',
         'PVZ_PROFILE_UPDATE', 'UpdateFraction'),
        ('SexyAppFramework/SexyAppBase.cpp', 'bool SexyAppBase::DrawDirtyStuff()',
         'PVZ_PROFILE_FRAME', 'Frame'),
        ('SexyAppFramework/widget/WidgetManager.cpp', 'bool WidgetManager::DrawScreen()',
         'PVZ_PROFILE_DRAW', 'Draw'),
        ('SexyAppFramework/graphics/SWTri.cpp',
         'void SWHelper::SWDrawShape(XYZStruct *theVerts, int theNumVerts, MemoryImage *theImage, const Color &theColor, [[maybe_unused]] int theDrawMode, const Rect &theClipRect, void *theSurface, int thePitch, int thePixelFormat, bool blend, bool vertexColor)',
         'PVZ_PROFILE_SW_DRAW_SHAPE', 'SWDrawShape'),
        ('Lawn/Plant.cpp', 'void Plant::Update()', 'PVZ_PROFILE_PLANT_UPDATE', 'SubPLANT_UPDATE'),
        ('Lawn/Zombie.cpp', 'void Zombie::Update()', 'PVZ_PROFILE_ZOMBIE_UPDATE', 'SubZOMBIE_UPDATE'),
        ('Lawn/Projectile.cpp', 'void Projectile::Update()', 'PVZ_PROFILE_PROJECTILE_UPDATE', 'SubPROJECTILE_UPDATE'),
        ('Lawn/Coin.cpp', 'void Coin::Update()', 'PVZ_PROFILE_COIN_UPDATE', 'SubCOIN_UPDATE'),
        ('PvzpLib/PvzpParticle.cpp', 'void PvzpParticleSystem::Update()', 'PVZ_PROFILE_PARTICLE_UPDATE', 'SubPARTICLE_UPDATE'),
        ('Lawn/Board.cpp', 'void Board::Update()', 'PVZ_PROFILE_BOARD_UPDATE', 'SubBOARD_UPDATE'),
        ('Lawn/Plant.cpp', 'void Plant::Draw(Graphics* g)', 'PVZ_PROFILE_PLANT_DRAW', 'SubPLANT_DRAW'),
        ('Lawn/Zombie.cpp', 'void Zombie::Draw(Graphics* g)', 'PVZ_PROFILE_ZOMBIE_DRAW', 'SubZOMBIE_DRAW'),
        ('Lawn/Projectile.cpp', 'void Projectile::Draw(Graphics* g)', 'PVZ_PROFILE_PROJECTILE_DRAW', 'SubPROJECTILE_DRAW'),
        ('Lawn/Coin.cpp', 'void Coin::Draw(Graphics* g)', 'PVZ_PROFILE_COIN_DRAW', 'SubCOIN_DRAW'),
        ('Lawn/Board.cpp', 'void Board::DrawGameObjects(Graphics* g)', 'PVZ_PROFILE_OBJECTS_DRAW', 'SubOBJECTS_DRAW'),
        ('SexyAppFramework/widget/WidgetManager.cpp', 'bool WidgetManager::UpdateFrame()', 'PVZ_PROFILE_WIDGET_UPDATE', 'SubWIDGET_UPDATE'),
        ('PvzpLib/PvzpParticle.cpp', 'void PvzpParticleSystem::Draw(Graphics* g)', 'PVZ_PROFILE_PARTICLE_DRAW', 'SubPARTICLE_DRAW'),
        ('Lawn/Board.cpp', 'void Board::DrawBackdrop(Graphics* g)', 'PVZ_PROFILE_BACKDROP_DRAW', 'SubBACKDROP_DRAW'),
        ('Lawn/Board.cpp', 'void Board::DrawUITop(Graphics* g)', 'PVZ_PROFILE_UITOP_DRAW', 'SubUITOP_DRAW'),
        ('Lawn/Projectile.cpp', 'void Projectile::DrawShadow(Graphics* g)', 'PVZ_PROFILE_PROJECTILE_SHADOW', 'SubPROJECTILE_SHADOW'),
        ('Lawn/Zombie.cpp', 'void Zombie::DrawShadow(Graphics* g)', 'PVZ_PROFILE_ZOMBIE_SHADOW', 'SubZOMBIE_SHADOW'),
        ('Lawn/Plant.cpp', 'void Plant::DrawShadow(Sexy::Graphics* g, float theOffsetX, float theOffsetY)', 'PVZ_PROFILE_PLANT_SHADOW', 'SubPLANT_SHADOW'),
        ('Lawn/Board.cpp', 'void Board::Draw(Graphics* g)', 'PVZ_PROFILE_BOARD_DRAW', 'SubBOARD_DRAW'),
        ('Lawn/Board.cpp', 'void Board::UpdateGame()', 'PVZ_PROFILE_BOARDGAME_UPDATE', 'SubBOARDGAME_UPDATE'),
        ('Lawn/Board.cpp', 'void Board::UpdateGameObjects()', 'PVZ_PROFILE_GAMEOBJECTS_UPDATE', 'SubGAMEOBJECTS_UPDATE'),
        ('Lawn/Plant.cpp', 'void Plant::UpdateAbilities()', 'PVZ_PROFILE_PLANT_ABILITIES', 'SubPLANT_ABILITIES'),
        ('Lawn/Plant.cpp', 'void Plant::Animate()', 'PVZ_PROFILE_PLANT_ANIMATE', 'SubPLANT_ANIMATE'),
        ('Lawn/Plant.cpp', 'void Plant::UpdateReanim()', 'PVZ_PROFILE_PLANT_REANIM', 'SubPLANT_REANIM'),
        ('Lawn/Plant.cpp', 'void Plant::UpdateReanimColor()', 'PVZ_PROFILE_PLANT_COLOR', 'SubPLANT_COLOR'),
        ('PvzpLib/Reanimator.cpp', 'void Reanimation::Update()', 'PVZ_PROFILE_REANIM_UPDATE', 'SubREANIM_UPDATE'),
    ):
        path = root / relative
        text = path.read_text(encoding='utf8')
        marker = 'PVZH1_PROFILE_' + label.upper() + '_BOUNDARY'
        if marker in text:
            continue
        anchor = 'using namespace Sexy;'
        entry = signature + '\n{'
        if text.count(entry) != 1:
            raise RuntimeError('Pinned profiling boundary changed: ' + signature)
        if INCLUDE not in text:
            if text.count(anchor)==1:text=text.replace(anchor,INCLUDE+'\n'+anchor,1)
            elif text.count(anchor)==0:text=INCLUDE+'\n'+text
            else:raise RuntimeError('Pinned profiling namespace changed: '+relative)
        text = text.replace(entry, entry + '\n' + GUARD + '\n'
                            '    // ' + marker + ': inclusive whole-call CPU count; no timer waits.\n'
                            '    PVZProfileScope pvzProfile' + label + 'Scope(' + category + ');\n'
                            '#endif', 1)
        path.write_text(text, encoding='utf8', newline='\n')
