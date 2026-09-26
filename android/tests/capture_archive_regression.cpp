#include "lcs_render_capture.hpp"
#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>
using namespace lcs;
int main(int argc,char **argv){
    assert(argc==2);const std::string dir=argv[1];std::filesystem::create_directories(dir);
    render_capture_text("never.txt","not enabled");assert(render_capture_status()==0);
    std::thread request([&]{assert(render_capture_request(dir+"/normal.zip"));});request.join();
    assert(!render_capture_request(dir+"/duplicate.zip"));assert(!render_capture_active());
    render_capture_frame_boundary();assert(render_capture_active());
    render_capture_frame_boundary();assert(render_capture_active()); // Ignore an empty vblank.
    assert(!render_capture_has_draws());
    std::array<std::byte,256>bytes{};for(unsigned i=0;i<256;++i)bytes[i]=std::byte(i);
    render_capture_bytes("binary.bin",bytes);render_capture_bytes("empty.bin",{});
    assert(render_capture_next_draw()==0);assert(render_capture_next_draw()==1);
    render_capture_frame_boundary();assert(render_capture_status()==3);
    assert(!std::filesystem::exists(dir+"/normal.zip.partial"));
    assert(render_capture_request(dir+"/limited.zip"));render_capture_frame_boundary();
    std::vector<std::byte>chunk(1024*1024,std::byte{0x35});
    for(int i=0;i<70;++i)render_capture_bytes("data_"+std::to_string(i),chunk);
    render_capture_finish();assert(render_capture_status()==3);
    assert(std::filesystem::file_size(dir+"/limited.zip")<65u*1024u*1024u);
    assert(render_capture_request(dir+"/marked.zip"));render_capture_frame_boundary();
    render_capture_incomplete();render_capture_finish();assert(render_capture_status()==3);
    assert(render_capture_request(dir+"/no-directory/no.zip"));render_capture_frame_boundary();
    assert(render_capture_status()==-1);
    assert(render_capture_request(dir+"/cancelled.zip"));render_capture_finish();
    assert(render_capture_status()==-1 && !std::filesystem::exists(dir+"/cancelled.zip"));
    std::cout<<"PASS: explicit request, whole-frame boundaries, concurrent request rejection, binary/empty entries, 64 MiB bound, partial marker, atomic finalization and I/O failure\n";
}
