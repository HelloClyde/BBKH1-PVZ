#pragma once
void H1OptionsBegin();
void H1OptionsPrepare();
void H1OptionsConstructed();
void H1OptionsDrawn();
void H1OptionsPresented();
class LawnApp;
void H1AudioSettingsLoad(LawnApp*);
void H1AudioSettingsSave(LawnApp*);
void H1UIFlowBegin(unsigned kind);
void H1UIFlowMark(unsigned phase);
void H1UIFlowPresented();
void H1BackgroundSaveScopeChange(bool begin);
struct H1BackgroundSaveScope {
    H1BackgroundSaveScope(){H1BackgroundSaveScopeChange(true);}
    ~H1BackgroundSaveScope(){H1BackgroundSaveScopeChange(false);}
};
