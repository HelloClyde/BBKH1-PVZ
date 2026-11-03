"""Checked selector display-only hooks, applied to a prepared copy."""
from pathlib import Path

def replace(text, old, new, label):
    if text.count(old) != 1:
        raise ValueError(f'Selector offline patch context changed: {label}')
    return text.replace(old, new)

def apply(source_root):
    root = Path(source_root)
    path = root/'Lawn/Widget/GameSelector.cpp'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_OFFLINE_SELECTOR' not in text:
        text = '#include "selector/SelectorOffline.h" // PVZH1_OFFLINE_SELECTOR\n'+text
        text = replace(text, 'GameSelector::~GameSelector()\n{\n',
                       'GameSelector::~GameSelector()\n{\n\tH1SelectorOfflineRelease(this);\n', 'selector lifetime')
        text = replace(text, '\taSelectorReanim->DrawRenderGroup(g, 1);  // "SelectorScreen_BG"',
                       '\tif (!H1SelectorOfflineDrawGroup(this, g, aSelectorReanim, 1))\n\t\taSelectorReanim->DrawRenderGroup(g, 1);  // "SelectorScreen_BG"', 'background order')
        text = replace(text, '\taSelectorReanim->DrawRenderGroup(g, RENDER_GROUP_NORMAL);',
                       '\tif (!H1SelectorOfflineDrawGroup(this, g, aSelectorReanim, RENDER_GROUP_NORMAL))\n\t\taSelectorReanim->DrawRenderGroup(g, RENDER_GROUP_NORMAL);', 'foreground order')
        text = replace(text,
                       '\t\tstd::string aWelcomeStr = mApp->mPlayerInfo->mName + \'!\';',
                       '\t\tif (H1SelectorOfflineWelcomeBegin(this, g)) return;\n\t\tstd::string aWelcomeStr = mApp->mPlayerInfo->mName + \'!\';', 'welcome cache begin')
        text = replace(text,
                       '\t\tPvzpDrawStringMatrix(g, Sexy::FONT_BRIANNETOD16, aOverlayMatrix * aOffsetMatrix, aWelcomeStr, Color(255, 245, 200));',
                       '\t\tPvzpDrawStringMatrix(g, Sexy::FONT_BRIANNETOD16, aOverlayMatrix * aOffsetMatrix, aWelcomeStr, Color(255, 245, 200));\n\t\tH1SelectorOfflineWelcomeEnd(this, g);', 'welcome cache end')
        path.write_text(text, encoding='utf8', newline='\n')
    path = root/'Lawn/Widget/AchievementsScreen.cpp'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_CLIPPED_ACHIEVEMENTS' not in text:
        text = '#include "selector/SelectorOffline.h" // PVZH1_CLIPPED_ACHIEVEMENTS\n'+text
        text = replace(text, 'void AchievementsWidget::Draw(Graphics* g) {\n',
                       'void AchievementsWidget::Draw(Graphics* g) {\n\tif (H1SelectorSkipClippedAchievements(g)) return;\n',
                       'fully clipped achievement page')
        path.write_text(text, encoding='utf8', newline='\n')
    path = root/'Lawn/Widget/GameButton.cpp'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_OFFLINE_SELECTOR' not in text:
        text = '#include "selector/SelectorOffline.h" // PVZH1_OFFLINE_SELECTOR\n'+text
        text = replace(text, 'void NewLawnButton::Draw(Graphics* g)\n{\n',
                       'void NewLawnButton::Draw(Graphics* g)\n{\n\tif (H1SelectorOfflineDrawButton(this, g)) return;\n', 'selector child button')
        path.write_text(text, encoding='utf8', newline='\n')
