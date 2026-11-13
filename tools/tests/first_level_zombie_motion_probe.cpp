/* Real first-level update chain, including opening handoff and pause/resume. */
#include "ui_flow_engine_probe.cpp"
#include "Lawn/Zombie.h"
#include "PvzpLib/Reanimator.h"
#include "Lawn/Widget/ContinueDialog.h"
#include <set>
#include <iostream>
#include <bit>

extern "C" void pvzh1_first_level_zombie_motion_probe(){
    using namespace Sexy;
    pvzh1_ui_flow_probe();
    gLawnApp->KillGameSelector();
    gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
    auto* b=gLawnApp->mBoard;
    for(unsigned i=0;i<5000&&gLawnApp->mGameScene!=SCENE_PLAYING;++i)gLawnApp->UpdateFrames();
    if(gLawnApp->mGameScene!=SCENE_PLAYING||b->mLevel!=1)throw std::runtime_error("Not playing first level");
    auto* z=b->AddZombieInRow(ZOMBIE_NORMAL,2,0);
    auto* r=gLawnApp->ReanimationGet(z->mBodyReanimID);
    r->mAnimRate=0.0f; // First active update must reconcile a stopped walker.
    auto* screen=gLawnApp->mGLInterface->GetScreenImage();
    auto* physical=H1GetPhysicalScreen(screen);
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"zombie-motion.jsonl");
    std::set<unsigned> phases,pictures;
    const auto x=z->mPosX;
    for(unsigned i=0;i<=600;++i){
        if(i==200){
            gLawnApp->DoNewOptions(false);
            const auto stoppedX=z->mPosX,stoppedTime=r->mAnimTime;
            for(unsigned pause=0;pause<30;++pause)gLawnApp->UpdateFrames();
            if(z->mPosX!=stoppedX||r->mAnimTime!=stoppedTime)throw std::runtime_error("Options did not pause zombie");
            gLawnApp->KillNewOptionsDialog();
        }
        if(i%20==0){
            out<<"{\"step\":"<<i<<",\"board_main\":"<<b->mMainCounter<<",\"paused\":"<<b->mPaused<<",\"age\":"<<z->mZombieAge<<",\"x\":"<<z->mPosX<<",\"speed\":"<<z->mVelX<<",\"anim_rate\":"<<r->mAnimRate<<",\"anim_time\":"<<r->mAnimTime<<",\"time_stop\":"<<b->mTimeStopCounter<<"}\n";out.flush();
        }
        phases.insert(std::bit_cast<unsigned>(r->mAnimTime));
        h1DirtyCanvas.Begin(physical->Target());
        if(i%5==0){
            Graphics g(screen);g.Translate(b->mX,b->mY);b->Draw(&g);
            auto* pixels=physical->Target()->GetBits();unsigned hash=2166136261u;
            for(unsigned y=125;y<205;++y)for(unsigned px=365;px<480;++px)hash=(hash^pixels[y*480+px])*16777619u;
            pictures.insert(hash);
        }
        if(i<600)gLawnApp->UpdateFrames();
    }
    out.close();
    std::cerr<<"ZOMBIE_MOTION start="<<x<<" end="<<z->mPosX<<" phases="<<phases.size()<<" age="<<z->mZombieAge<<" rate="<<r->mAnimRate<<"\n";
    if(z->mZombieAge<600||phases.size()<100||pictures.size()<40||x-z->mPosX<20)throw std::runtime_error("First-level zombie stopped");
    const auto savedX=z->mPosX;
    // Exact stopped-clock precondition observed on physical H1: an alive,
    // unchilled normal walker has a valid clip and positive speed, but rate 0.
    // Loading such a save must restore motion without restarting the level.
    r->mAnimRate=0.0f;r->mAnimTime=0.0f;
    gLawnApp->DoNewOptions(false);gLawnApp->DoConfirmBackToMain();
    gLawnApp->ButtonDepress(2000+DIALOG_CONFIRM_BACK_TO_MAIN);
    gLawnApp->KillGameSelector();gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,true);
    auto* continueDialog=static_cast<ContinueDialog*>(gLawnApp->GetDialog(DIALOG_CONTINUE));
    if(!continueDialog)throw std::runtime_error("Continue dialog missing");
    continueDialog->ButtonDepress(ContinueDialog::ContinueDialog_Continue);
    b=gLawnApp->mBoard;z=nullptr;
    for(auto* candidate:b->mZombies)if(!candidate->mDead&&candidate->mFromWave>=0){z=candidate;break;}
    if(!z||std::abs(z->mPosX-savedX)>.001f)throw std::runtime_error("Zombie not restored");
    r=gLawnApp->ReanimationGet(z->mBodyReanimID);const auto resumedTime=r->mAnimTime;
    for(unsigned i=0;i<200;++i)gLawnApp->UpdateFrames();
    {std::ofstream observed(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"continue-observed.json");
     observed<<"{\"before_x\":"<<savedX<<",\"after_x\":"<<z->mPosX<<",\"before_time\":"<<resumedTime<<",\"after_time\":"<<r->mAnimTime<<",\"rate\":"<<r->mAnimRate<<",\"paused\":"<<b->mPaused<<"}\n";}
    if(z->mPosX>=savedX-5||r->mAnimTime==resumedTime||b->mPaused)throw std::runtime_error("Zombie stopped after Continue");
    // A real immobilized zombie must keep its zero rate across a load.
    z->mButteredCounter=1000;z->UpdateAnimSpeed();const auto frozenX=z->mPosX;
    gLawnApp->DoNewOptions(false);gLawnApp->DoConfirmBackToMain();
    gLawnApp->ButtonDepress(2000+DIALOG_CONFIRM_BACK_TO_MAIN);
    gLawnApp->KillGameSelector();gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,true);
    continueDialog=static_cast<ContinueDialog*>(gLawnApp->GetDialog(DIALOG_CONTINUE));
    if(!continueDialog)throw std::runtime_error("Frozen Continue dialog missing");
    continueDialog->ButtonDepress(ContinueDialog::ContinueDialog_Continue);b=gLawnApp->mBoard;
    for(auto* candidate:b->mZombies)if(!candidate->mDead&&candidate->mFromWave>=0){z=candidate;break;}
    r=gLawnApp->ReanimationGet(z->mBodyReanimID);
    for(unsigned i=0;i<10;++i)gLawnApp->UpdateFrames();
    if(z->mPosX!=frozenX||r->mAnimRate!=0||z->mButteredCounter!=990)throw std::runtime_error("Load repair bypassed butter");
    std::ofstream proof(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"zombie-motion-verification.json");
    proof<<"{\"passed\":true,\"steps\":600,\"start_x\":"<<x<<",\"end_x\":"<<savedX<<",\"distinct_animation_times\":"<<phases.size()<<",\"distinct_rendered_zombie_regions\":"<<pictures.size()<<",\"zero_rate_first_update_recovered\":true,\"pause_and_resume\":true,\"save_and_continue_200_steps\":true,\"zero_rate_save_recovered\":true,\"legitimate_butter_preserved_on_load\":true,\"resumed_end_x\":"<<frozenX<<",\"scope\":\"Original full game logic on host, not physical H1 performance\"}\n";
    gLawnApp->DoBackToMain();
}
