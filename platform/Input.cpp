/* LGPL-3.0-or-later. Full upstream widgets receive native mouse/key events. */
#include "SexyAppBase.h"
#include "widget/WidgetManager.h"
#include "misc/KeyCodes.h"
#include "h1_host.h"
#include "h1_input.h"
using namespace Sexy;
static bool held[45],text_active;
static int mx=400,my=300;
static const char letters[45]={0,'q','w','e','r','t','y','u','a','s','d','f','g','h','j',0,'z','x','c','v','b','n',0,' ',0,0,0,0,0,'i','o','p','k','l',0,0,'m',0,0,0,0,0,0,0,0};
void SexyAppBase::InitInput(){for(bool &h:held)h=false;mMouseIn=true;}
bool SexyAppBase::StartTextInput(std::string&){text_active=true;return false;}
void SexyAppBase::StopTextInput(){text_active=false;}
void SexyAppBase::SetTextInputRect(const Rect&){}
bool SexyAppBase::ProcessDeferredMessages(bool singleMessage){
    bool processed=false;int kind,key,x,y;
    for(int n=0;n<(singleMessage?1:64)&&pvzp_input_poll(&kind,&key,&x,&y);n++){
        processed=true;mLastUserInputTick=mLastTimerTime;
        if(kind>=3){mx=x;my=y;pvzp_pointer_set(mx,my);mMouseIn=true;mWidgetManager->MouseMove(mx,my);
            if(kind==3)mWidgetManager->MouseDown(mx,my,1);if(kind==4)mWidgetManager->MouseUp(mx,my,1);continue;}
        bool down=kind==1;if(key<1||key>44)continue;if(down&&held[key])continue;held[key]=down;
        if(!text_active&&(key==H1_KEY_LEFT||key==H1_KEY_RIGHT||key==H1_KEY_UP||key==H1_KEY_DOWN)){
            if(down){mx+=key==H1_KEY_LEFT?-20:key==H1_KEY_RIGHT?20:0;my+=key==H1_KEY_UP?-20:key==H1_KEY_DOWN?20:0;
                mx=std::clamp(mx,0,mWidth-1);my=std::clamp(my,0,mHeight-1);pvzp_pointer_set(mx,my);mWidgetManager->MouseMove(mx,my);}continue;
        }
        if(key==H1_KEY_CONFIRM||(!text_active&&key==H1_KEY_Z)){
            mWidgetManager->MouseMove(mx,my);if(down)mWidgetManager->MouseDown(mx,my,1);else mWidgetManager->MouseUp(mx,my,1);continue;
        }
        int vk=0;if(key==H1_KEY_ENTER)vk=KEYCODE_RETURN;
        else if(key==H1_KEY_BACK||key==H1_KEY_ESCAPE)vk=KEYCODE_ESCAPE;
        else if(key==H1_KEY_DELETE)vk=KEYCODE_BACK;
        else if(key==H1_KEY_LEFT)vk=KEYCODE_LEFT;else if(key==H1_KEY_RIGHT)vk=KEYCODE_RIGHT;
        else if(key==H1_KEY_UP)vk=KEYCODE_UP;else if(key==H1_KEY_DOWN)vk=KEYCODE_DOWN;
        else if(letters[key])vk=letters[key]>='a'&&letters[key]<='z'?letters[key]-'a'+'A':letters[key];
        if(vk){if(down){mWidgetManager->KeyDown(static_cast<KeyCode>(vk));if(letters[key])mWidgetManager->KeyChar(letters[key]);}
            else mWidgetManager->KeyUp(static_cast<KeyCode>(vk));}
    }
    /* Return whether another event may need processing, matching upstream's
       one-event contract. The outer game loop invokes us again every tick. */
    return singleMessage&&processed;
}
