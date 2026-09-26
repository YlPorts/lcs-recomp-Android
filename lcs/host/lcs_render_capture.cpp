#include "lcs_render_capture.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
namespace lcs {
namespace {
constexpr std::size_t kMaxPayload=64u*1024u*1024u, kMaxEntries=16000u;
std::atomic<int> status{0};
std::mutex request_mutex;
std::string requested_path;
std::uint32_t draw_number{};
bool incomplete{};
struct Entry { std::string name; std::uint32_t crc{},size{},offset{}; };
std::uint32_t capture_crc(std::span<const std::byte> data) {
    static const auto table=[] {
        std::array<std::uint32_t,256> t{};
        for(std::uint32_t n=0;n<256;++n){auto v=n;for(int j=0;j<8;++j)v=(v>>1)^((v&1)?0xEDB88320u:0u);t[n]=v;}
        return t;
    }();
    auto v=0xffffffffu;
    for(auto b:data)v=table[(v^std::to_integer<unsigned char>(b))&255u]^(v>>8);
    return ~v;
}
struct Archive {
    std::ofstream out;
    std::string path;
    std::vector<Entry> entries;
    std::uint64_t bytes{};
    explicit Archive(std::string name):out(name+".partial",std::ios::binary|std::ios::trunc),path(std::move(name)){
        if(!out)throw std::runtime_error("capture open failed");
    }
    void u16(std::uint32_t v){const char b[2]{char(v),char(v>>8)};out.write(b,2);}
    void u32(std::uint32_t v){u16(v);u16(v>>16);}
    bool add(std::string_view name,std::span<const std::byte> data,bool essential=false){
        if(name.empty()||name.size()>200||name.front()=='/'||name.find("..")!=std::string_view::npos)
            throw std::runtime_error("invalid capture name");
        if(!essential&&(bytes+data.size()>kMaxPayload||entries.size()>=kMaxEntries)){incomplete=true;return false;}
        Entry e{std::string(name),capture_crc(data),static_cast<std::uint32_t>(data.size()),static_cast<std::uint32_t>(out.tellp())};
        u32(0x04034b50);u16(20);u16(0);u16(0);u16(0);u16(0x21);
        u32(e.crc);u32(e.size);u32(e.size);u16(e.name.size());u16(0);
        out.write(e.name.data(),e.name.size());
        if(!data.empty())out.write(reinterpret_cast<const char*>(data.data()),data.size());
        if(!out)throw std::runtime_error("capture write failed");
        bytes+=data.size();entries.push_back(std::move(e));return true;
    }
    void close(){
        const auto offset=static_cast<std::uint32_t>(out.tellp());
        for(const auto&e:entries){
            u32(0x02014b50);u16(20);u16(20);u16(0);u16(0);u16(0);u16(0x21);
            u32(e.crc);u32(e.size);u32(e.size);u16(e.name.size());u16(0);u16(0);u16(0);u16(0);u32(0);u32(e.offset);
            out.write(e.name.data(),e.name.size());
        }
        const auto size=static_cast<std::uint32_t>(out.tellp())-offset;
        u32(0x06054b50);u16(0);u16(0);u16(entries.size());u16(entries.size());u32(size);u32(offset);u16(0);
        out.flush();if(!out)throw std::runtime_error("capture flush failed");out.close();
        if(std::rename((path+".partial").c_str(),path.c_str()))throw std::runtime_error("capture rename failed");
    }
};
std::unique_ptr<Archive> archive;
void fail() noexcept {archive.reset();status.store(-1,std::memory_order_release);}
}
bool render_capture_request(std::string path) noexcept {
    try{std::lock_guard<std::mutex>lock(request_mutex);int s=status.load(std::memory_order_acquire);
        if(s==1||s==2||path.empty())return false;
        requested_path=std::move(path);status.store(1,std::memory_order_release);return true;
    }catch(...){return false;}
}
int render_capture_status() noexcept{return status.load(std::memory_order_acquire);}
bool render_capture_active() noexcept{return status.load(std::memory_order_relaxed)==2;}
bool render_capture_has_draws() noexcept{return render_capture_active()&&draw_number!=0;}
std::uint32_t render_capture_next_draw() noexcept{return draw_number++;}
void render_capture_incomplete() noexcept{if(render_capture_active())incomplete=true;}
void render_capture_bytes(std::string_view name,std::span<const std::byte>data) noexcept {
    if(!render_capture_active())return;
    try{archive->add(name,data);}catch(...){fail();}
}
void render_capture_text(std::string_view name,std::string_view text) noexcept {
    render_capture_bytes(name,std::as_bytes(std::span(text.data(),text.size())));
}
void render_capture_finish() noexcept {
    if(!render_capture_active()){
        int pending=1;status.compare_exchange_strong(pending,-1,std::memory_order_acq_rel);
        return;
    }
    try{
        const std::string text="{\"format\":\"LCS-GE-capture-1\",\"endian\":\"little\",\"draws\":"+
            std::to_string(draw_number)+",\"truncated\":"+(incomplete?"true":"false")+"}\n";
        archive->add("manifest.json",std::as_bytes(std::span(text.data(),text.size())),true);
        archive->close();archive.reset();status.store(3,std::memory_order_release);
    }catch(...){fail();}
}
void render_capture_frame_boundary() noexcept {
    // Only the native game/GE-owner thread calls this. UI only requests/polls.
    if(render_capture_has_draws())render_capture_finish();
    if(status.load(std::memory_order_acquire)!=1)return;
    try{
        std::lock_guard<std::mutex>lock(request_mutex);
        archive=std::make_unique<Archive>(requested_path);draw_number=0;incomplete=false;
        status.store(2,std::memory_order_release);
        render_capture_text("README.txt","Explicit local diagnostic of ONE GE frame. No network upload. Contains game graphics, not ROM/save/account data. "
            "Commands, palettes, vertex records and float matrices are little-endian. CPU-decoded images are NOT proof of GPU cache contents. "
            "Missing entries in a truncated capture must not be interpreted as missing in gameplay. "
            "Capture can briefly pause rendering; do not benchmark while recording.\n");
    }catch(...){fail();}
}
}
