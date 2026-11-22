#include "../Core/Http.hpp"
#include <iostream>
#include <cstdlib>
using namespace dock;
void check(bool good,const char* why){if(!good){std::cerr<<why<<"\n";std::exit(1);}}
int main(){
    Response r;
    check(decode("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n[]",r)==Parse::complete&&r.body=="[]"&&r.status==200,"content-length");
    check(decode("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n[",r)==Parse::incomplete,"partial");
    check(decode("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n[",r,true)==Parse::invalid,"truncated");
    check(decode("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\n[\r\n1\r\n]\r\n0\r\n\r\n",r)==Parse::complete&&r.body=="[]","chunked");
    check(decode("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;foo=bar\r\nx\r\n0\r\nX-Foo: y\r\n\r\n",r)==Parse::complete&&r.body=="x","extensions");
    check(decode("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx\r\n0\r\n",r)==Parse::incomplete,"partial chunk completion");
    check(decode("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\n",r)==Parse::invalid,"invalid chunk");
    check(decode("HTTP/1.1 200 OK\r\nContent-Length: 999999999\r\n\r\n",r)==Parse::invalid,"body limit");
    check(decode("HTTP/1.1 500 Error\r\n\r\nerror",r,true)==Parse::complete&&r.status==500&&r.body=="error","close-delimited");
    check(decode("bad\r\n\r\n",r)==Parse::invalid,"bad status");
    std::cout<<"HTTP parser tests passed\n";
}
