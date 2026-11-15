"""Bind immutable image storage to real scene/widget/effect lifetimes."""
from pathlib import Path

def apply(root:Path):
    def patch(name,entries,replace=()):
        path=root/name;text=path.read_text(encoding='utf8')
        text='#include "graphics/ImageLifetime.h"\n'+text
        for signature,statement in entries:
            anchor=signature+'\n{'
            if text.count(anchor)!=1:raise ValueError('Image lifetime entry changed: '+signature)
            text=text.replace(anchor,anchor+'\n    '+statement,1)
        for before,after in replace:
            if text.count(before)!=1:raise ValueError('Image lifetime boundary changed: '+before)
            text=text.replace(before,after,1)
        path.write_text(text,encoding='utf8',newline='\n')
    use=lambda kind,obj='this':f'Sexy::H1ImageUseScope imageOwner(Sexy::H1ImageOwner::{kind},{obj});'
    release=lambda kind,obj='this':f'Sexy::H1ImageResourcesReleaseOwner(Sexy::H1ImageOwner::{kind},{obj});'
    patch('SexyAppFramework/widget/WidgetContainer.cpp',[
        ('void WidgetContainer::DrawAll(ModalFlags* theFlags, Graphics* g)',use('Widget')),
        ('WidgetContainer::~WidgetContainer()',release('Widget')),
        ('void WidgetContainer::RemovedFromManager(WidgetManager* theWidgetManager)',release('Widget'))])
    patch('SexyAppFramework/widget/Widget.cpp',[],[
        ('\tmVisible = isVisible;','\tmVisible = isVisible;\n    if(!isVisible)Sexy::H1ImageResourcesReleaseWidgetTree(this);')])
    patch('Lawn/Board.cpp',[('void Board::Draw(Graphics* g)',use('Widget'))])
    patch('PvzpLib/Reanimator.cpp',[
        ('void Reanimation::DrawRenderGroup(Graphics* g, int theRenderGroup)',use('Animation')),
        ('void Reanimation::ReanimationDelete()',release('Animation')),
        ('void Reanimation::ReanimationDie()',release('Animation'))])
    patch('PvzpLib/PvzpParticle.cpp',[
        ('void PvzpParticleSystem::Draw(Graphics* g)',use('Particle')),
        ('void PvzpParticleEmitter::Draw(Graphics* g)',use('Particle','mParticleSystem')),
        ('void PvzpParticleSystem::ParticleSystemDie()',release('Particle'))])
    patch('PvzpLib/Trail.cpp',[
        ('void Trail::Draw(Graphics* g)',use('Trail'))],
        [('\t\t\tmDead = true;','\t\t\tmDead = true;\n            '+release('Trail'))])
    path=root/'PvzpLib/Trail.h';text=path.read_text(encoding='utf8')
    assert text.count('\tTrail();')==1;text=text.replace('\tTrail();','\tTrail();\n    ~Trail();')
    path.write_text(text,encoding='utf8',newline='\n')
    path=root/'PvzpLib/Trail.cpp';text=path.read_text(encoding='utf8')
    text+='\nTrail::~Trail(){'+release('Trail')+'}\n';path.write_text(text,encoding='utf8',newline='\n')
    patch('LawnApp.cpp',[],[
        ('void LawnApp::MakeNewBoard()\n{\n\tKillBoard();',
         'void LawnApp::MakeNewBoard()\n{\n\tKillBoard();\n    Sexy::H1ImageResourcesBeginScene(Sexy::H1ImageScene::Board);'),
        ('void LawnApp::ShowGameSelector()\n{\n\tKillBoard();',
         'void LawnApp::ShowGameSelector()\n{\n\tKillBoard();\n    Sexy::H1ImageResourcesBeginScene(Sexy::H1ImageScene::Menu);'),
        ('void LawnApp::ShowAwardScreen(AwardType theAwardType, bool theShowAchievements)\n{',
         'void LawnApp::ShowAwardScreen(AwardType theAwardType, bool theShowAchievements)\n{\n    Sexy::H1ImageResourcesBeginScene(Sexy::H1ImageScene::Award);'),
        ('void LawnApp::ShowCreditScreen()\n{',
         'void LawnApp::ShowCreditScreen()\n{\n    Sexy::H1ImageResourcesBeginScene(Sexy::H1ImageScene::Credits);')])
