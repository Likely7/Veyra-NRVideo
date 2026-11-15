// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/StartupMusic.h"
#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <windows.h>
#include <objbase.h>
#include <cstdio>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(argc<3)return 2;
    const QString mode=QString::fromLocal8Bit(argv[1]),path=QString::fromLocal8Bit(argv[2]);
    veyra::ui::StartupMusic music(path);music.setMuted(true); // BEFORE source or playback.
    int failed=0,playing=0,ended=0;bool advanced=false;
    auto check=[&](bool ok,const char* text){std::printf("%s %s state=%s position=%.3f\n",ok?"PASS":"FAIL",text,qPrintable(music.state()),music.position());fflush(stdout);if(!ok)++failed;};
    if(mode=="before"){
        music.pause();music.prepare(1);music.play();check(music.state()=="paused","media intent before prepare consumes startup");
    }else if(mode=="missing"){
        check(!music.available(),"absent asset is optional");music.prepare(1);music.play();check(music.state()=="idle","missing asset does not play");
    }else{
        QElapsedTimer elapsed;elapsed.start();
        QObject::connect(&music,&veyra::ui::StartupMusic::stateChanged,&app,[&]{
            std::printf("EVENT %s at %lld ms\n",qPrintable(music.state()),elapsed.elapsed());fflush(stdout);
            if(music.state()=="playing")++playing;
            if(music.state()=="ended"){++ended;QTimer::singleShot(1200,&app,&QCoreApplication::quit);}
            if(music.state()=="error")app.exit(3);
        });
        music.prepare(.5);
        if(mode=="loading")music.pause();
        music.play();
        QTimer watch;watch.setInterval(100);
        QObject::connect(&watch,&QTimer::timeout,&app,[&]{
            if(music.position()>4)advanced=true;
            if(mode=="stop"&&music.position()>4){music.pause();music.prepare(1);music.play();check(music.state()=="paused","playing stop releases and never restarts");app.quit();}
        });watch.start();
        QTimer::singleShot(mode=="loading"?2000:145000,&app,&QCoreApplication::quit);
        const int rc=app.exec();if(rc)return rc;
        if(mode=="full"){
            check(advanced,"audio continues beyond intro duration");check(playing==1&&ended==1&&music.state()=="ended","one natural end and no loop");
            music.prepare(1);music.play();check(music.state()=="ended","natural end cannot restart");
        }else if(mode=="loading")check(playing==0&&music.state()=="paused","stop during loading ignores queued playback events");
        else check(advanced,"stop tested after audio progressed beyond intro");
    }
    CoUninitialize();return failed?1:0;
}
