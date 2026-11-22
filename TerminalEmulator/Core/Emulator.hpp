#pragma once
#ifdef small
#undef small
#endif
#include <vterm.h>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace term {
struct Cell { VTermScreenCell value{}; uint32_t link=0; };
using Line=std::vector<Cell>;
class Emulator {
public:
    using Send=std::function<void(std::string_view)>;
    static constexpr size_t HistoryBytes=32*1024*1024;
    explicit Emulator(int rows=24,int cols=80,Send send={});
    ~Emulator();
    Emulator(const Emulator&)=delete;Emulator& operator=(const Emulator&)=delete;
    void feed(std::string_view);
    void resize(int,int);
    void setHistoryLimit(size_t);
    void setHistoryByteLimit(size_t bytes){byteLimit_=std::min(bytes,HistoryBytes);trim();}
    void clearHistory();
    void key(VTermKey,VTermModifier);
    void character(uint32_t,VTermModifier);
    void paste(std::string_view);
    void mouseMove(int,int,VTermModifier);
    void mouseButton(int,bool,VTermModifier);
    void focus(bool);
    void palette(const uint32_t* rgb,uint32_t fg,uint32_t bg);
    int rows()const{return rows_;}int cols()const{return cols_;}
    size_t historySize()const{return alternate_?0:history_.size();}
    size_t historyBytes()const{return bytes_;}
    uint64_t historySerial()const{return pushes_;}
    uint64_t historyBase()const{return base_;}
    bool alternate()const{return alternate_;}
    int mouseMode()const{return mouse_;}
    bool cursorVisible()const{return cursorVisible_;}
    int cursorShape()const{return cursorShape_;}
    VTermPos cursor()const{return cursor_;}
    const std::string& title()const{return title_;}
    const std::string& cwdReport()const{return cwd_;}
    const std::string& hyperlink(uint32_t id)const;
    Line line(size_t index)const;
    uint32_t color(VTermColor value)const;
    static bool continuation(const VTermScreenCell& c){return c.chars[0]==UINT32_MAX||c.width==0;}
private:
    static int damaged(VTermRect,void*);
    static int moved(VTermRect,VTermRect,void*);
    static int cursorMoved(VTermPos,VTermPos,int,void*);
    static int property(VTermProp,VTermValue*,void*);
    static int pushLine(int,const VTermScreenCell*,void*);
    static int popLine(int,VTermScreenCell*,void*);
    static int clearLines(void*);
    static int osc(int,VTermStringFragment,void*);
    static void sendOut(const char*,size_t,void*);
    void trim();
    Cell blank()const;
    VTerm* vt_=nullptr;VTermScreen* screen_=nullptr;VTermState* state_=nullptr;
    Send send_;
    int rows_,cols_;size_t limit_=10000,bytes_=0,byteLimit_=HistoryBytes;uint64_t pushes_=0,base_=0;
    bool alternate_=false,cursorVisible_=true,resizing_=false;
    int cursorShape_=1,mouse_=0,pushRow_=0;
    VTermPos cursor_{};
    std::deque<Line> history_;
    std::vector<uint32_t> screenLinks_,primaryLinks_;
    std::vector<std::string> links_{""};
    uint32_t activeLink_=0;
    bool collecting_=false;
    std::string collected_;
    std::string title_,titleFragment_,cwd_,oscFragment_;
};
}
