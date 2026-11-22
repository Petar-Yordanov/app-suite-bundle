#include "Layout.hpp"
#include <cmath>
void PaneNode::setRatio(double r){
    if(!std::isfinite(r))r=.5;r=qBound(.1,r,.9);
    if(std::abs(r-ratio_)<.001)return;ratio_=r;emit ratioChanged();emit layoutEdited();
}
void PaneNode::connectChild(PaneNode* n){connect(n,&PaneNode::layoutEdited,this,&PaneNode::layoutEdited);}
void PaneNode::attach(PaneNode* a,PaneNode* b,bool horizontal,double r){
    first_=a;second_=b;session_=nullptr;horizontal_=horizontal;ratio_=.5;
    a->setParent(this);b->setParent(this);connectChild(a);connectChild(b);setRatio(r);
    emit changed();emit layoutEdited();
}
void PaneNode::split(Session* s,bool horizontal){
    if(!leaf()||!session_||!s)return;
    auto* a=new PaneNode(session_);auto* b=new PaneNode(s);
    attach(a,b,horizontal,.5);
}
PaneNode* PaneNode::find(Session* s){
    if(leaf())return session_==s?this:nullptr;
    if(auto* n=first_->find(s))return n;return second_->find(s);
}
Session* PaneNode::firstSession()const{return leaf()?session_:first_->firstSession();}
int PaneNode::count()const{return leaf()?1:first_->count()+second_->count();}
int PaneNode::depth()const{return leaf()?1:1+qMax(first_->depth(),second_->depth());}
void PaneNode::forEach(const std::function<void(Session*)>& fn)const{
    if(leaf()){if(session_)fn(session_);}else{first_->forEach(fn);second_->forEach(fn);}
}
void PaneNode::absorb(PaneNode* s){
    disconnect(s,nullptr,this,nullptr);
    session_=s->session_;horizontal_=s->horizontal_;ratio_=s->ratio_;
    first_=s->first_;second_=s->second_;
    s->first_=s->second_=nullptr;s->session_=nullptr;
    if(first_){
        disconnect(first_,nullptr,s,nullptr);disconnect(second_,nullptr,s,nullptr);
        first_->setParent(this);second_->setParent(this);connectChild(first_);connectChild(second_);
    }
}
bool PaneNode::remove(Session* target){
    if(leaf())return false;
    PaneNode* removed=nullptr;PaneNode* other=nullptr;
    if(first_->leaf()&&first_->session_==target){removed=first_;other=second_;}
    else if(second_->leaf()&&second_->session_==target){removed=second_;other=first_;}
    else return first_->remove(target)||second_->remove(target);
    disconnect(removed,nullptr,this,nullptr);absorb(other);
    removed->deleteLater();other->deleteLater();emit changed();emit ratioChanged();emit layoutEdited();
    return true;
}
QJsonObject PaneNode::serialize()const{
    if(leaf())return session_?QJsonObject{{"shell",session_->shellId()},{"cwd",session_->cwd()}}:QJsonObject{};
    return {{"horizontal",horizontal_},{"ratio",ratio_},{"first",first_->serialize()},{"second",second_->serialize()}};
}
void TerminalTab::setTitle(QString s){
    s=s.trimmed().left(80);if(s.isEmpty()||s==title_)return;title_=s;emit titleChanged();
}
