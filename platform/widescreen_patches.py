"""Checked presentation and pointer offsets; game grid/save coordinates stay original."""
from pathlib import Path
import re

def apply(root):
    def patch(name,old,new):
        p=Path(root)/name;s=p.read_text(encoding='utf8')
        if s.count(old)!=1:raise ValueError('Wide patch context: '+name+': '+old[:80])
        p.write_text(s.replace(old,new),encoding='utf8')
    app='LawnApp.cpp'
    p=Path(root)/app;p.write_text('#include "graphics/ScreenLayout.h"\n'+p.read_text(encoding='utf8'),encoding='utf8')
    patch(app,'\tmWidth = BOARD_WIDTH;','\tmWidth = Sexy::H1LogicalWidth;')
    patch(app,'\tmBoard->Resize(0, 0, mWidth, mHeight);',
        '''#ifdef PVZH1_WIDESCREEN
    mBoard->Resize(Sexy::H1BoardOffset,0,Sexy::H1LogicalWidth-Sexy::H1BoardOffset,mHeight);
    mBoard->mClip=false;
    if(mBoard->mMenuButton){mBoard->mMenuButton->mX+=Sexy::H1BoardOffset;mBoard->mMenuButton->mParentWidget=mBoard;}
    if(mBoard->mStoreButton){mBoard->mStoreButton->mX+=Sexy::H1BoardOffset;mBoard->mStoreButton->mParentWidget=mBoard;}
#else
    mBoard->Resize(0,0,mWidth,mHeight);
#endif''')
    patch(app,'theDialog->Resize((BOARD_WIDTH - theWidth) / 2, (BOARD_HEIGHT - theHeight) / 2, theWidth, theHeight);',
              'theDialog->Resize((Sexy::H1LogicalWidth - theWidth) / 2, (Sexy::H1LogicalHeight - theHeight) / 2, theWidth, theHeight);')
    patch(app,'\tmSeedChooserScreen->Resize(0, 0, mWidth, mHeight);',
              '\tmSeedChooserScreen->Resize(Sexy::H1BoardOffset,0,BOARD_WIDTH,mHeight);')
    patch(app,'\tmAwardScreen->Resize(0, 0, mWidth, mHeight);',
              '\tmAwardScreen->Resize(Sexy::H1BoardOffset,0,BOARD_WIDTH,mHeight);')
    award='Lawn/Widget/AwardScreen.cpp'
    p=Path(root)/award;p.write_text('#include "graphics/ScreenLayout.h"\n'+p.read_text(encoding='utf8'),encoding='utf8')
    patch(award,'\tmStartButton->mTextOffsetY = -1;',
              '\tmStartButton->mTextOffsetY = -1;\n\tmStartButton->mParentWidget = this;')
    patch(award,'g->DrawImage(Sexy::IMAGE_AWARDSCREEN_BACK, 0, 0);',
              'g->DrawImage(Sexy::IMAGE_AWARDSCREEN_BACK, -Sexy::H1BoardOffset, 0);')
    patch(award,'g->FillRect(0, 0, BOARD_WIDTH, BOARD_HEIGHT);',
              'g->FillRect(-Sexy::H1BoardOffset, 0, Sexy::H1LogicalWidth, BOARD_HEIGHT);')
    patch(award,'g->FillRect(0, 525, BOARD_WIDTH, BOARD_HEIGHT);',
              'g->FillRect(-Sexy::H1BoardOffset, 525, Sexy::H1LogicalWidth, BOARD_HEIGHT);')
    patch(award,'g->FillRect(0, 0, mWidth, mHeight);',
              'g->FillRect(-Sexy::H1BoardOffset, 0, Sexy::H1LogicalWidth, mHeight);')
    # The upstream scaled-price matrix bypasses Graphics translation. It was
    # invisible with a root at x=0, but a centered award exposes the omission.
    patch('Lawn/SeedPacket.cpp',
          'PvzpScaleTransformMatrix(aMatrix, aTextOffsetX * g->mScaleX + x, aTextOffsetY * g->mScaleY + y, g->mScaleX, g->mScaleY);',
          '''#ifdef PVZH1_WIDESCREEN
            PvzpScaleTransformMatrix(aMatrix,aTextOffsetX*g->mScaleX+x+g->mTransX,aTextOffsetY*g->mScaleY+y+g->mTransY,g->mScaleX,g->mScaleY);
#else
            PvzpScaleTransformMatrix(aMatrix,aTextOffsetX*g->mScaleX+x,aTextOffsetY*g->mScaleY+y,g->mScaleX,g->mScaleY);
#endif''')
    board='Lawn/Board.cpp';p=Path(root)/board;s=p.read_text(encoding='utf8')
    # Center offset is presentation state, deliberately absent from SAVE4.
    old='\t\t\tmX = 0;';assert s.count(old)==1
    s=s.replace(old,'\t\t\tmX = Sexy::H1BoardOffset;')
    s=s.replace('mX = PvzpAnimateCurve(12, 0, mShakeCounter, 0, mShakeAmountX, PvzpCurves::CURVE_BOUNCE);',
                'mX = Sexy::H1BoardOffset + PvzpAnimateCurve(12, 0, mShakeCounter, 0, mShakeAmountX, PvzpCurves::CURVE_BOUNCE);')
    pair='mApp->mWidgetManager->mLastMouseX, mApp->mWidgetManager->mLastMouseY, aCursorSeedType'
    assert s.count(pair)==6
    s=s.replace(pair,'mApp->mWidgetManager->mLastMouseX - mX, mApp->mWidgetManager->mLastMouseY - mY, aCursorSeedType')
    s=s.replace('mSeedBank->ContainsPoint(mWidgetManager->mLastMouseX, mWidgetManager->mLastMouseY)',
                'mSeedBank->ContainsPoint(mWidgetManager->mLastMouseX - mX, mWidgetManager->mLastMouseY - mY)')
    old='mApp->AddReanimation(-BOARD_OFFSET, 0, MakeRenderOrder(RenderLayer::RENDER_LAYER_SCREEN_FADE, 0, 0), ReanimationType::REANIM_ZOMBIES_WON);'
    assert s.count(old)==1
    s=s.replace(old,'mApp->AddReanimation(int(Sexy::H1BoardOffset)-mX,-mY,MakeRenderOrder(RenderLayer::RENDER_LAYER_SCREEN_FADE,0,0),ReanimationType::REANIM_ZOMBIES_WON);')
    p.write_text(s,encoding='utf8')
    p=Path(root)/'Lawn/CutScene.cpp';s='#include "graphics/ScreenLayout.h"\n'+p.read_text(encoding='utf8')
    s=s.replace('mApp->mWidth','BOARD_WIDTH')
    s,count=re.subn(r'mBoard->Move\((.+), 0\);',r'mBoard->Move(Sexy::H1BoardCameraX(\1), 0);',s)
    assert count==7,count
    s=s.replace('aSeedChoser->Move(0,','aSeedChoser->Move(Sexy::H1BoardOffset,')
    old='mApp->AddReanimation(-BOARD_OFFSET, 0, aRenderPosition, ReanimationType::REANIM_ZOMBIES_WON);'
    assert s.count(old)==1
    s=s.replace(old,'mApp->AddReanimation(int(Sexy::H1BoardOffset)-mBoard->mX,-mBoard->mY,aRenderPosition,ReanimationType::REANIM_ZOMBIES_WON);')
    p.write_text(s,encoding='utf8')
    # Ghost seed and grid highlight consume board-local coordinates.
    p=Path(root)/'Lawn/CursorObject.cpp';s=p.read_text(encoding='utf8')
    s=s.replace('mApp->mWidgetManager->mLastMouseX','(mApp->mWidgetManager->mLastMouseX - mBoard->mX)')
    s=s.replace('mApp->mWidgetManager->mLastMouseY','(mApp->mWidgetManager->mLastMouseY - mBoard->mY)')
    p.write_text(s,encoding='utf8')
    p=Path(root)/'Lawn/Challenge.cpp';s=p.read_text(encoding='utf8')
    for name in ('BeghouledTwistSquareFromMouse','mBoard->MouseHitTest'):
        s=s.replace(name+'(mApp->mWidgetManager->mLastMouseX, mApp->mWidgetManager->mLastMouseY,',
                    name+'(mApp->mWidgetManager->mLastMouseX - mBoard->mX, mApp->mWidgetManager->mLastMouseY - mBoard->mY,')
    p.write_text(s,encoding='utf8')
    p=Path(root)/'Lawn/Widget/SeedChooserScreen.cpp';s=p.read_text(encoding='utf8')
    s='#include "graphics/ScreenLayout.h"\n'+s
    # Manual buttons need the centered chooser's origin for hit testing.
    # The menu intentionally draws in screen coordinates and stays unparented.
    for name in ('Start','Random'):
        anchor=f'm{name}Button = std::make_unique<GameButton>(SeedChooserScreen::SeedChooserScreen_{name});'
        assert s.count(anchor)==1
        s=s.replace(anchor,anchor+f'\n    m{name}Button->mParentWidget=this;')
    anchor='mLastMouseX = -1;';assert s.count(anchor)==1
    s=s.replace(anchor,anchor+'\n    mMouseInsets.mLeft=-int(Sexy::H1BoardOffset);')
    # GetSeedPositionInBank already returns chooser-local X. A second
    # subtraction hid the first selected card beyond the physical left edge.
    anchor='aPosX -= mX;';assert s.count(anchor)==1
    s=s.replace(anchor,'// H1: bank X is already local to this centered widget.')
    s=s.replace('mApp->mWidth','BOARD_WIDTH')
    s,count=re.subn(r'mBoard->Move\((.+), 0\);',r'mBoard->Move(Sexy::H1BoardCameraX(\1), 0);',s)
    assert count==3,count
    s=s.replace('mLastMouseX = mApp->mWidgetManager->mLastMouseX;','mLastMouseX = mApp->mWidgetManager->mLastMouseX - mX;')
    s=s.replace('mLastMouseY = mApp->mWidgetManager->mLastMouseY;','mLastMouseY = mApp->mWidgetManager->mLastMouseY - mY;')
    p.write_text(s,encoding='utf8')
    # The generated clean plate already contains the distant house/garden.
    selector='Lawn/Widget/GameSelector.cpp'
    p=Path(root)/selector;p.write_text('#include "graphics/ScreenLayout.h"\n'+p.read_text(encoding='utf8'),encoding='utf8')
    patch(selector,'\taSelectorReanim->AssignRenderGroupToTrack("SelectorScreen_BG", 1);',
        '''\taSelectorReanim->AssignRenderGroupToTrack("SelectorScreen_BG", 1);
#ifdef PVZH1_WIDESCREEN
    aSelectorReanim->AssignRenderGroupToTrack("SelectorScreen_BG_Center",RENDER_GROUP_HIDDEN);
#endif''')
    # Flower hit regions follow their shifted original animation attachment.
    patch(selector,'static float gFlowerCenter[3][2] = { { 765.0f, 483.0f }, { 663.0f, 455.0f }, { 701.0f, 439.0f } };',
        '''static float gFlowerCenter[3][2] = { { 765.0f + Sexy::H1BoardOffset*2, 483.0f }, { 663.0f + Sexy::H1BoardOffset*2, 455.0f }, { 701.0f + Sexy::H1BoardOffset*2, 439.0f } };''')
    p=Path(root)/selector;s=p.read_text(encoding='utf8').replace('BOARD_WIDTH','Sexy::H1LogicalWidth');p.write_text(s,encoding='utf8')
    p=Path(root)/'Lawn/Widget/ZombatarWidget.cpp';s='#include "graphics/ScreenLayout.h"\n'+p.read_text(encoding='utf8')
    s=s.replace('Resize(800, 0, BOARD_WIDTH, BOARD_HEIGHT);','Resize(Sexy::H1LogicalWidth,0,Sexy::H1LogicalWidth,BOARD_HEIGHT);')
    s=s.replace('mX >= BOARD_WIDTH','mX >= (int)Sexy::H1LogicalWidth').replace('SlideTo(-BOARD_WIDTH, 0)','SlideTo(-(int)Sexy::H1LogicalWidth, 0)')
    p.write_text(s,encoding='utf8')
