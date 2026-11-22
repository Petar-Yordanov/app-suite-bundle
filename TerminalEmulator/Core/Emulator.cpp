#include "Emulator.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace term {
namespace {
constexpr size_t FragmentLimit=8192;
bool fragment(std::string& dest,VTermStringFragment frag){
    if(frag.initial)dest.clear();
    if(frag.len>FragmentLimit-dest.size()){dest.clear();return false;}
    if(frag.str&&frag.len)dest.append(frag.str,frag.len);
    return frag.final;
}
}
Emulator::Emulator(int r,int c,Send send):send_(std::move(send)),rows_(std::clamp(r,2,300)),cols_(std::clamp(c,2,500)){
    vt_=vterm_new(rows_,cols_);if(!vt_)throw std::bad_alloc();
    vterm_set_utf8(vt_,1);
    state_=vterm_obtain_state(vt_);screen_=vterm_obtain_screen(vt_);
    screenLinks_.resize(size_t(rows_)*cols_);
    static const auto callbacks=[]{
        VTermScreenCallbacks cb{};
        cb.damage=&damaged;cb.moverect=&moved;cb.movecursor=&cursorMoved;cb.settermprop=&property;
        cb.sb_pushline=&pushLine;cb.sb_popline=&popLine;cb.sb_clear=&clearLines;
        return cb;
    }();
    static const auto fallback=[]{
        VTermStateFallbacks cb{};cb.osc=&osc;return cb;
    }();
    vterm_output_set_callback(vt_,&sendOut,this);
    vterm_screen_set_callbacks(screen_,&callbacks,this);
    vterm_screen_set_unrecognised_fallbacks(screen_,&fallback,this);
    vterm_screen_enable_altscreen(screen_,1);
    vterm_screen_enable_reflow(screen_,1);
    // Cell-level callbacks keep OSC 8 metadata aligned; rendering is batched.
    vterm_screen_set_damage_merge(screen_,VTERM_DAMAGE_CELL);
    vterm_screen_reset(screen_,1);
}
Emulator::~Emulator(){vterm_free(vt_);}
void Emulator::feed(std::string_view bytes){
    while(!bytes.empty()){
        auto n=vterm_input_write(vt_,bytes.data(),bytes.size());
        if(!n)break;bytes.remove_prefix(n);
    }
    vterm_screen_flush_damage(screen_);
}
void Emulator::resize(int r,int c){
    r=std::clamp(r,2,300);c=std::clamp(c,2,500);
    if(r==rows_&&c==cols_)return;
    // libvterm reflows cells; its public API does not reflow external link
    // metadata. Avoid incorrectly attaching an old URL to a different cell.
    resizing_=true;vterm_set_size(vt_,r,c);rows_=r;cols_=c;
    screenLinks_.assign(size_t(rows_)*cols_,0);primaryLinks_.clear();resizing_=false;pushRow_=0;
    vterm_screen_flush_damage(screen_);
}
void Emulator::setHistoryLimit(size_t n){limit_=std::min<size_t>(100000,n);trim();}
void Emulator::clearHistory(){base_+=history_.size();history_.clear();bytes_=0;}
void Emulator::trim(){
    while(!history_.empty()&&(history_.size()>limit_||bytes_>byteLimit_)){
        bytes_-=history_.front().capacity()*sizeof(Cell);history_.pop_front();++base_;
    }
}
Cell Emulator::blank()const{
    Cell result{};result.value.width=1;
    vterm_state_get_default_colors(state_,&result.value.fg,&result.value.bg);return result;
}
Line Emulator::line(size_t index)const{
    auto count=historySize();
    if(index<count)return history_[index];
    if(index-count>=size_t(rows_))return {};
    int r=int(index-count);Line line(cols_,blank());
    for(int col=0;col<cols_;++col){
        auto& cell=line[col];
        vterm_screen_get_cell(screen_,{r,col},&cell.value);
        cell.link=screenLinks_[size_t(r)*cols_+col];
    }
    return line;
}
uint32_t Emulator::color(VTermColor value)const{
    vterm_screen_convert_color_to_rgb(screen_,&value);
    return (uint32_t(value.rgb.red)<<16)|(uint32_t(value.rgb.green)<<8)|value.rgb.blue;
}
void Emulator::palette(const uint32_t* rgb,uint32_t fg,uint32_t bg){
    VTermColor a,b;vterm_color_rgb(&a,(fg>>16)&255,(fg>>8)&255,fg&255);
    vterm_color_rgb(&b,(bg>>16)&255,(bg>>8)&255,bg&255);
    vterm_state_set_default_colors(state_,&a,&b);
    for(int i=0;i<16;++i){auto v=rgb[i];VTermColor color;vterm_color_rgb(&color,(v>>16)&255,(v>>8)&255,v&255);vterm_state_set_palette_color(state_,i,&color);}
}
void Emulator::key(VTermKey k,VTermModifier m){vterm_keyboard_key(vt_,k,m);}
void Emulator::character(uint32_t c,VTermModifier m){vterm_keyboard_unichar(vt_,c,m);}
void Emulator::paste(std::string_view data){
    collecting_=true;collected_.clear();vterm_keyboard_start_paste(vt_);collected_.append(data);vterm_keyboard_end_paste(vt_);
    collecting_=false;if(send_)send_(collected_);collected_.clear();
}
void Emulator::mouseMove(int row,int col,VTermModifier m){vterm_mouse_move(vt_,row,col,m);}
void Emulator::mouseButton(int button,bool pressed,VTermModifier m){vterm_mouse_button(vt_,button,pressed,m);}
void Emulator::focus(bool in){if(in)vterm_state_focus_in(state_);else vterm_state_focus_out(state_);}
const std::string& Emulator::hyperlink(uint32_t id)const{return links_[id<links_.size()?id:0];}
void Emulator::sendOut(const char* p,size_t n,void* self){
    auto& s=*static_cast<Emulator*>(self);
    if(s.collecting_)s.collected_.append(p,n);else if(s.send_)s.send_({p,n});
}
int Emulator::damaged(VTermRect rect,void* self){
    auto& s=*static_cast<Emulator*>(self);if(s.resizing_||s.links_.size()==1){s.pushRow_=0;return 1;}
    for(int row=std::max(0,rect.start_row);row<std::min(s.rows_,rect.end_row);++row)
        for(int col=std::max(0,rect.start_col);col<std::min(s.cols_,rect.end_col);++col){
            VTermScreenCell c{};vterm_screen_get_cell(s.screen_,{row,col},&c);
            s.screenLinks_[size_t(row)*s.cols_+col]=c.chars[0]?s.activeLink_:0;
        }
    s.pushRow_=0;return 1;
}
int Emulator::moved(VTermRect dest,VTermRect src,void* self){
    auto& s=*static_cast<Emulator*>(self);s.pushRow_=0;
    if(s.resizing_)return 1;
    auto h=std::min(dest.end_row-dest.start_row,src.end_row-src.start_row);
    auto w=std::min(dest.end_col-dest.start_col,src.end_col-src.start_col);
    if(h<=0||w<=0)return 1;
    // Copy overlap in the correct direction without allocating per scroll.
    for(int i=0;i<h;++i){
        int y=dest.start_row>src.start_row?h-1-i:i;
        for(int j=0;j<w;++j){
            int x=dest.start_col>src.start_col?w-1-j:j;
            auto sy=src.start_row+y,dy=dest.start_row+y,sx=src.start_col+x,dx=dest.start_col+x;
            if(sy>=0&&dy>=0&&sy<s.rows_&&dy<s.rows_&&sx>=0&&dx>=0&&sx<s.cols_&&dx<s.cols_)
                s.screenLinks_[size_t(dy)*s.cols_+dx]=s.screenLinks_[size_t(sy)*s.cols_+sx];
        }
    }
    return 1;
}
int Emulator::cursorMoved(VTermPos pos,VTermPos,int visible,void* self){
    auto& s=*static_cast<Emulator*>(self);s.cursor_=pos;s.cursorVisible_=visible!=0;return 1;
}
int Emulator::property(VTermProp prop,VTermValue* val,void* self){
    auto& s=*static_cast<Emulator*>(self);
    switch(prop){
    case VTERM_PROP_CURSORVISIBLE:s.cursorVisible_=val->boolean;break;
    case VTERM_PROP_CURSORSHAPE:s.cursorShape_=val->number;break;
    case VTERM_PROP_MOUSE:s.mouse_=val->number;break;
    case VTERM_PROP_TITLE:if(fragment(s.titleFragment_,val->string))s.title_=s.titleFragment_;break;
    case VTERM_PROP_ALTSCREEN:
        if(bool(val->boolean)!=s.alternate_){
            if(val->boolean){s.primaryLinks_=s.screenLinks_;std::fill(s.screenLinks_.begin(),s.screenLinks_.end(),0);}
            else if(s.primaryLinks_.size()==s.screenLinks_.size())s.screenLinks_.swap(s.primaryLinks_);
            else std::fill(s.screenLinks_.begin(),s.screenLinks_.end(),0);
            s.primaryLinks_.clear();s.activeLink_=0;s.alternate_=val->boolean;
        }break;
    default:break;
    }return 1;
}
int Emulator::pushLine(int cols,const VTermScreenCell* cells,void* self){
    auto& s=*static_cast<Emulator*>(self);
    if(s.alternate_)return 1;
    ++s.pushes_;
    if(!s.limit_){++s.base_;return 1;}
    Line row(size_t(std::max(0,cols)));
    for(int i=0;i<cols;++i){
        row[i].value=cells[i];
        if(!s.resizing_&&s.pushRow_<s.rows_&&i<s.cols_)row[i].link=s.screenLinks_[size_t(s.pushRow_)*s.cols_+i];
    }
    ++s.pushRow_;s.bytes_+=row.capacity()*sizeof(Cell);s.history_.push_back(std::move(row));s.trim();return 1;
}
int Emulator::popLine(int cols,VTermScreenCell* cells,void* self){
    auto& s=*static_cast<Emulator*>(self);if(s.history_.empty())return 0;
    auto& row=s.history_.back();for(int i=0;i<cols;++i)cells[i]=i<int(row.size())?row[i].value:s.blank().value;
    if(cols>0&&cells[cols-1].width==2)cells[cols-1]=s.blank().value;
    s.bytes_-=row.capacity()*sizeof(Cell);s.history_.pop_back();return 1;
}
int Emulator::clearLines(void* self){static_cast<Emulator*>(self)->clearHistory();return 1;}
int Emulator::osc(int command,VTermStringFragment frag,void* self){
    auto& s=*static_cast<Emulator*>(self);
    if(command!=7&&command!=8&&command!=777)return 0;
    if(!fragment(s.oscFragment_,frag))return 1;
    if(command==7||command==777){s.cwd_=s.oscFragment_;return 1;}
    auto delim=s.oscFragment_.find(';');
    s.activeLink_=0;if(delim==std::string::npos)return 1;
    auto uri=s.oscFragment_.substr(delim+1);
    if(uri.empty()||uri.size()>4096)return 1;
    // Only user-activated http(s) destinations are supported. No automatic
    // file, shell or custom-protocol launches from untrusted terminal output.
    if(!uri.starts_with("https://")&&!uri.starts_with("http://"))return 1;
    auto it=std::find(s.links_.begin(),s.links_.end(),uri);
    if(it!=s.links_.end())s.activeLink_=uint32_t(it-s.links_.begin());
    else if(s.links_.size()<1024){s.links_.push_back(std::move(uri));s.activeLink_=uint32_t(s.links_.size()-1);}
    return 1;
}
}
