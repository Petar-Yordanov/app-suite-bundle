#include "../App/Controller.hpp"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <iostream>
#include <cstdlib>
static void check(bool ok,const char* text){if(!ok){std::cerr<<"FAIL: "<<text<<'\n';std::exit(1);}}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);QTemporaryDir temp;
    check(temp.isValid(),"temporary directory");
    const auto path=temp.filePath("settings.ini");
    {
        Controller c(path);check(c.currentRoot()!=nullptr,"initial shell");
        auto* initial=c.active();
        c.split(true,{});c.split(false,{});
        check(c.currentRoot()->count()==3&&c.currentRoot()->horizontal()&&!c.currentRoot()->second()->horizontal(),"recursive split");
        c.currentRoot()->setRatio(.36);c.renameTab(0,"Original");
        c.duplicateTab(0);check(c.tabs().size()==2&&c.currentRoot()->count()==3,"duplicate layout");
        c.moveTab(1,0);c.selectTab(1);check(c.currentRoot()->find(initial)!=nullptr,"reorder retains session");
        c.closePane();check(c.currentRoot()->count()==2,"close pane");
        c.save();
    }
    {
        Controller c(path);check(c.tabs().size()==2&&c.currentRoot()->count()==2,"restore tabs and panes");
        c.setOption("restorePanes",false);c.save();
    }
    {
        Controller c(path);check(c.tabs().size()==2&&c.currentRoot()->count()==1,"tabs only");
        c.setOption("restoreTabs",false);c.setOption("restorePanes",true);c.split(true,{});c.save();
    }
    {
        Controller c(path);check(c.tabs().size()==1&&c.currentRoot()->count()==2,"panes only");
    }
    std::cout<<"Layout operations and independent restoration passed\n";
}
