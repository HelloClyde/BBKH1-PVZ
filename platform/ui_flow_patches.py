"""H1 menu transitions and touch audio controls, applied to the pinned source."""
from pathlib import Path


def apply(root: Path):
    def patch(relative, old, new):
        path = root / relative
        text = path.read_text(encoding='utf8')
        assert text.count(old) == 1, (relative, old[:80])
        path.write_text(text.replace(old, new), encoding='utf8', newline='\n')

    selector=root/'Lawn/Widget/GameSelector.cpp'
    text=selector.read_text(encoding='utf8')
    start=text.index('\tif (theShowLoading)\n\t{',text.index('void GameSelector::SyncProfile('))
    end=text.index('\n\tPvzpParticleSystem*',start)
    text=text[:start]+'    (void)theShowLoading; // Actual-board dependencies are warmed at level entry.\n'+text[end:]
    selector.write_text(text,encoding='utf8',newline='\n')
    patch('LawnApp.cpp', '\tmMusicVolume = 0.85;', '\tmMusicVolume = 0.10;')
    patch('LawnApp.cpp', '\tmSfxVolume = 0.5525;', '\tmSfxVolume = 0.10;')
    patch('LawnApp.cpp', '\tSexy::MkDir(Sexy::GetAppDataPath("userdata"));',
          '\tSexy::MkDir(Sexy::GetAppDataPath("userdata"));\n    H1AudioSettingsLoad(this);')
    patch('LawnApp.cpp', 'LawnApp::~LawnApp()\n{',
          'LawnApp::~LawnApp()\n{\n    H1AudioSettingsSave(this);')
    patch('LawnApp.cpp', 'void LawnApp::DoBackToMain()\n{',
          'void LawnApp::DoBackToMain()\n{\n    H1UIFlowMark(2);')
    patch('LawnApp.cpp', '\tWriteCurrentUserConfig();\n\tKillNewOptionsDialog();',
          '\tWriteCurrentUserConfig();\n    H1UIFlowMark(3);\n\tKillNewOptionsDialog();')
    patch('LawnApp.cpp', '\tKillBoard();\n\tShowGameSelector();',
          '\tKillBoard();\n    H1UIFlowMark(4);\n\tShowGameSelector();\n    H1UIFlowMark(7);')
    patch('LawnApp.cpp', '\t\t\tH1RetireBoardReplay();mBoard->TryToSaveGame();\n\t\t\tDoBackToMain();',
          '\t\t\tH1UIFlowBegin(2);\n            H1RetireBoardReplay();mBoard->TryToSaveGame();\n            H1UIFlowMark(1);\n\t\t\tDoBackToMain();')
    patch('LawnApp.cpp', 'void LawnApp::FinishCreateUserDialog(bool isYes)\n{',
          'void LawnApp::FinishCreateUserDialog(bool isYes)\n{\n    H1BackgroundSaveScope backgroundSave;\n    H1UIFlowBegin(1);')
    patch('LawnApp.cpp', '\t\t\tmProfileMgr->Save();\n\t\t\tmPlayerInfo = aProfile;',
          '\t\t\tmProfileMgr->Save();\n            H1UIFlowMark(1);\n\t\t\tmPlayerInfo = aProfile;')
    patch('LawnApp.cpp', '\t\t\tmWidgetManager->MarkAllDirty();\n\n\t\t\tif (mGameSelector)',
          '\t\t\tmWidgetManager->MarkAllDirty();\n            H1UIFlowMark(2);\n\n\t\t\tif (mGameSelector)')
    patch('LawnApp.cpp', '\t\t\tmGameSelector->SyncProfile(true);\n\t\t\t}',
          '\t\t\tmGameSelector->SyncProfile(true);\n                H1UIFlowMark(7);\n\t\t\t}')
    patch('LawnApp.cpp', 'case Dialogs::DIALOG_CONFIRM_BACK_TO_MAIN:\n\t\t\tKillDialog',
          'case Dialogs::DIALOG_CONFIRM_BACK_TO_MAIN: {\n            H1BackgroundSaveScope backgroundSave;\n\t\t\tKillDialog')
    patch('LawnApp.cpp', '\t\t\tDoBackToMain();\n\t\t\treturn;',
          '\t\t\tDoBackToMain();\n\t\t\treturn; }')
    patch('LawnApp.cpp', 'void LawnApp::FinishConfirmDeleteUserDialog(bool isYes)\n{',
          'void LawnApp::FinishConfirmDeleteUserDialog(bool isYes)\n{\n    H1BackgroundSaveScope backgroundSave;\n    if(isYes)H1UIFlowBegin(3);')
    patch('LawnApp.cpp', '\tmProfileMgr->DeleteProfile(aName);',
          '\tmProfileMgr->DeleteProfile(aName);\n    H1UIFlowMark(1);')
    patch('LawnApp.cpp', '\tmProfileMgr->Save();\n\tif (mPlayerInfo == nullptr)',
          '\tmProfileMgr->Save();\n    H1UIFlowMark(2);\n\tif (mPlayerInfo == nullptr)')
    patch('LawnApp.cpp', '\t\tmGameSelector->SyncProfile(true);\n\t}\n}\n\nvoid LawnApp::DoRenameUserDialog',
          '\t\tmGameSelector->SyncProfile(true);\n\t}\n    H1UIFlowMark(7);\n}\n\nvoid LawnApp::DoRenameUserDialog')
    player=root/'Lawn/System/PlayerInfo.cpp'
    text=player.read_text(encoding='utf8');start=text.index('void PlayerInfo::DeleteUserFiles()\n{');end=text.index('\nvoid PlayerInfo::Reset()',start)
    text='#include "ProfileFiles.h"\n'+text[:start]+'''void PlayerInfo::DeleteUserFiles()
{
    H1DeletePlayerFiles(mId,static_cast<unsigned>(GameMode::NUM_GAME_MODES));
}
'''+text[end:]
    player.write_text(text,encoding='utf8',newline='\n')

    options = 'Lawn/Widget/NewOptionsDialog.cpp'
    patch(options, 'using namespace Sexy;', '''using namespace Sexy;
namespace {
class H1AudioSlider final : public Slider {
public:
    using Slider::Slider;
    void Draw(Graphics* g) override {
        const int thumb = mThumbImage->GetWidth();
        const int x = int(mVal * (mWidth - thumb));
        g->SetColor(Color(35, 42, 36));g->FillRect(0, mHeight/2-5, mWidth, 10);
        g->SetColor(Color(118, 190, 72));g->FillRect(0, mHeight/2-4, x+thumb/2, 8);
        g->SetColor(Color(224, 235, 181));g->FillRect(x, mHeight/2-15, thumb, 30);
        g->SetColor(Color(62, 91, 48));g->DrawRect(x, mHeight/2-15, thumb-1, 29);
    }
};
}''')
    path=root/options
    text=path.read_text(encoding='utf8').replace('std::make_unique<Slider>(', 'std::make_unique<H1AudioSlider>(')
    text=text.replace('theApp->GetSfxVolume() / 0.65', 'theApp->GetSfxVolume()').replace('mApp->SetSfxVolume(theVal * 0.65);', 'mApp->SetSfxVolume(theVal);')
    text=text.replace('mMusicVolumeSlider->Resize(199, 116, 135, 40);', 'mMusicVolumeSlider->Resize(190, 106, 170, 52);')
    text=text.replace('mSfxVolumeSlider->Resize(199, 143, 135, 40);', 'mSfxVolumeSlider->Resize(190, 172, 170, 52);')
    text=text.replace('mMusicVolumeSlider->mY += 5;', '').replace('mSfxVolumeSlider->mY += 10;', '')
    at=text.index('\n\tint aMusicOffset = 0;', text.index('void NewOptionsDialog::Draw('))
    end=text.index('\n}\n',at)
    text=text[:at]+'''
    const Color labelColor(224, 230, 190);
    const auto music = "音乐 " + std::to_string(int(mApp->GetMusicVolume()*100+0.5)) + "%";
    const auto sound = "音效 " + std::to_string(int(mApp->GetSfxVolume()*100+0.5)) + "%";
    PvzpDrawString(g, music, 175, 140, FONT_DWARVENTODCRAFT18, labelColor, DS_ALIGN_RIGHT);
    PvzpDrawString(g, sound, 175, 206, FONT_DWARVENTODCRAFT18, labelColor, DS_ALIGN_RIGHT);'''+text[end:]
    anchor='\tmHardwareAccelerationCheckbox = MakeNewCheckbox(NewOptionsDialog::NewOptionsDialog_HardwareAcceleration, this, theApp->Is3DAccelerated());'
    assert text.count(anchor)==1
    text=text.replace(anchor,anchor+'\n    mFullscreenCheckbox->SetVisible(false);\n    mHardwareAccelerationCheckbox->SetVisible(false);')
    text=text.replace('void NewOptionsDialog::SliderVal(int theId, double theVal)\n{', 'void NewOptionsDialog::SliderVal(int theId, double theVal)\n{\n    MarkDirty();')
    path.write_text(text,encoding='utf8',newline='\n')
