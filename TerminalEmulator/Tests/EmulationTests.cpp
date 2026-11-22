#include "../Core/Emulator.hpp"
#include <chrono>
#include <iostream>
#include <cstdlib>

static void check(bool ok,const char* label){if(!ok){std::cerr<<"FAIL: "<<label<<'\n';std::exit(1);}}
int main(){
    std::string sent;
    term::Emulator e(5,40,[&](std::string_view s){sent.append(s);});
    e.feed("abc\x1b[2;4H\x1b[38;2;12;34;56mX");
    auto l=e.line(1);
    check(l[3].value.chars[0]=='X'&&e.color(l[3].value.fg)==0x0c2238,"ANSI cursor and RGB");
    e.feed("\x1b[0m\x1b[3;1H\xe4\xb8");e.feed("\xad" "e\xcc\x81");
    l=e.line(2);check(l[0].value.chars[0]==0x4e2d&&l[0].value.width==2,"split UTF-8 wide character");
    check(l[2].value.chars[0]=='e'&&l[2].value.chars[1]==0x301,"combining character");
    e.feed("\x1b]2;title\x07\x1b]7;file:///tmp\x07");
    check(e.title()=="title"&&e.cwdReport()=="file:///tmp","OSC title and cwd");
    e.feed("\x1b[4;1H\x1b]8;;https://example.com\x1b\\link\x1b]8;;\x1b\\");
    l=e.line(3);check(e.hyperlink(l[0].link)=="https://example.com","OSC8 hyperlink");
    e.feed("\x1b[?1049hALT");check(e.alternate()&&e.historySize()==0,"alternate screen");
    e.feed("\x1b[?1049l");check(!e.alternate()&&e.line(0)[0].value.chars[0]=='a',"primary screen restored");
    e.feed("\x1b[?2004h");sent.clear();e.paste("hello\nthere");
    check(sent=="\x1b[200~hello\nthere\x1b[201~","bracketed paste");
    sent.clear();e.character('c',VTERM_MOD_CTRL);check(sent=="\x03","Ctrl+C");
    e.feed("\x1b[?1h");sent.clear();e.key(VTERM_KEY_UP,VTERM_MOD_NONE);check(sent=="\x1bOA","application cursor");
    sent.clear();e.feed("\x1b[6n");check(!sent.empty()&&sent.back()=='R',"terminal query response");
    e.setHistoryLimit(16);e.setHistoryByteLimit(8192);
    auto start=std::chrono::steady_clock::now();
    for(int i=0;i<100000;++i)e.feed("line 012345678901234567890123456789\r\n");
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    check(e.historySize()>0&&e.historySize()<=16&&e.historyBytes()<=8192,"bounded scrollback");
    e.resize(8,80);check(e.cols()==80&&e.rows()==8&&e.line(e.historySize()).size()==80,"resize");
    e.clearHistory();check(e.historySize()==0&&e.historyBytes()==0,"clear history");
    std::cout<<"VT compatibility passed; 100000 lines parsed in "<<ms<<" ms\n";
}
