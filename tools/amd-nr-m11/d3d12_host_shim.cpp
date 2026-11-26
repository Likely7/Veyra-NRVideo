#include "d3d12_host_shim.hpp"

#include <windows.h>
#include <d3d12.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "nr_d3d12_direct_state.hpp"
#include "nr_d3d12_net_plan.hpp"
#include "nr_d3d12_passthrough.hpp"
#include "nr_d3d12_passthrough_net.hpp"
#include "nr_ini_network.hpp"
#include "nr_log.hpp"
#include "nr_path_u8.hpp"
#include "nr_pe_log.hpp"
#include "nr_pipeline_binary.hpp"
#include "nr_vram_ledger.hpp"

namespace {

std::mutex g_log_mutex;
FILE* g_log_fp = nullptr;
bool g_quiet_stdout = true;
std::string g_log_path_str;
std::string g_root_dir;
std::wstring g_aco_dir_w;
std::atomic<int> g_model_choice{nr::kSmallFp8};
std::atomic<nr::arch::GpuArch> g_arch{nr::arch::GpuArch::rdna3};
std::atomic<uint32_t> g_gfx11_mach{0};
std::atomic<ID3D12CommandQueue*> g_last_queue{nullptr};
std::mutex g_net_note_mutex;
std::string g_last_net_name;

std::mutex g_rec_mutex;
std::unordered_map<ID3D12CommandList*, nr::gpu::Ticket> g_open_recs;

std::map<std::string, std::string> load_aliases(const std::string& path) {
    std::map<std::string, std::string> out;
    FILE* f = nullptr;
    std::wstring wp = nr::u8::wide(path);
    _wfopen_s(&f, wp.c_str(), L"r");
    if (!f) return out;
    char a[256] = {}, b[256] = {};
    while (std::fscanf(f, "%255s %255s", a, b) == 2) {
        out[a] = b;
    }
    std::fclose(f);
    return out;
}

}  // namespace

namespace m11_shim {

bool init_runtime(const std::string& root_dir, const std::string& log_file, int model_choice,
                  bool quiet_stdout, nr::arch::GpuArch arch) {
    g_root_dir = root_dir;
    while (!g_root_dir.empty() && (g_root_dir.back() == '\\' || g_root_dir.back() == '/'))
        g_root_dir.pop_back();
    g_model_choice.store(model_choice);
    g_arch.store(arch);
    g_quiet_stdout = quiet_stdout;

    {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        if (g_log_fp) {
            std::fclose(g_log_fp);
            g_log_fp = nullptr;
        }
        g_log_path_str = log_file;
        if (!log_file.empty()) {
            std::filesystem::path lp = nr::u8::path(log_file);
            std::error_code ec;
            if (lp.has_parent_path()) std::filesystem::create_directories(lp.parent_path(), ec);
            _wfopen_s(&g_log_fp, lp.wstring().c_str(), L"w");
        }
    }

    const std::string data_dir = g_root_dir + "\\dlssnr-amd";
    const std::string aco_dir = data_dir + "\\aco";
    const std::string cache_dir = data_dir + "\\aco-cache";
    g_aco_dir_w = nr::u8::wide(aco_dir);

    std::error_code ec;
    std::filesystem::create_directories(nr::u8::path(cache_dir), ec);

    auto aliases = load_aliases(aco_dir + "\\shell-aliases.txt");
    nr::binary::configure(
        aco_dir,
        {aco_dir + "\\shell", aco_dir + "\\shaders", data_dir + "\\shaders", data_dir + "\\shaders\\temporal"},
        cache_dir,
        std::move(aliases));

    nr::set_log_sink([](const char* s) { nr::pe::log("%s", s ? s : ""); });

    nr::pe::d3d12_passthrough::set_net_planner(
        [](uint32_t w, uint32_t h, int model, bool experimental, nr::D3d12NetPlan* out, std::string* why) {
            const bool allow_exp = experimental || (model == nr::kSmallFp8Experimental);
            return nr::plan_d3d12_network(
                g_root_dir, w, h, allow_exp, out, why, model, nr::pe::d3d12_passthrough::arch());
        },
        &nr::d3d12_net_controls,
        []() { return g_model_choice.load(); });

    return std::filesystem::is_directory(nr::u8::path(aco_dir + "\\rdna3\\records"), ec);
}

void set_model_choice(int model_choice) { g_model_choice.store(model_choice); }
int model_choice() { return g_model_choice.load(); }

void set_last_direct_queue(ID3D12CommandQueue* queue) { g_last_queue.store(queue); }

void begin_recording(ID3D12CommandList* cmd) {
    std::lock_guard<std::mutex> lk(g_rec_mutex);
    g_open_recs[cmd] = std::make_shared<nr::gpu::Recording>();
}

void end_recording(ID3D12CommandList* cmd, ID3D12Fence* fence, uint64_t signal_value) {
    nr::gpu::Ticket t;
    {
        std::lock_guard<std::mutex> lk(g_rec_mutex);
        auto it = g_open_recs.find(cmd);
        if (it != g_open_recs.end()) {
            t = it->second;
            g_open_recs.erase(it);
        }
    }
    if (!t) return;
    if (fence) {
        fence->AddRef();
        std::shared_ptr<ID3D12Fence> holder(fence, [](ID3D12Fence* f) { f->Release(); });
        t->submitted([holder = std::move(holder), signal_value]() {
            return holder->GetCompletedValue() >= signal_value;
        });
    } else {
        t->submitted(nullptr);
    }
    t->seal();
}

std::string last_network_name() {
    std::lock_guard<std::mutex> lk(g_net_note_mutex);
    return g_last_net_name;
}

}  // namespace m11_shim

namespace nr::pe {

LocalTime::LocalTime() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::snprintf(text, sizeof text, "%02u:%02u:%02u.%03u",
                  unsigned(now.wHour), unsigned(now.wMinute), unsigned(now.wSecond), unsigned(now.wMilliseconds));
}

void set_module(void*) {}
const char* module_folder() { return g_root_dir.c_str(); }
const char* log_path() { return g_log_path_str.c_str(); }

void log(const char* format, ...) {
    char msg[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(msg, sizeof msg, format ? format : "", args);
    va_end(args);

    LocalTime ts;
    std::lock_guard<std::mutex> lk(g_log_mutex);
    if (g_log_fp) {
        std::fprintf(g_log_fp, "%s | %s\n", ts.text, msg);
        std::fflush(g_log_fp);
    }
    if (!g_quiet_stdout) {
        std::fprintf(stderr, "%s | %s\n", ts.text, msg);
        std::fflush(stderr);
    }
}

void trace_arm() {}
int trace_execute() { return 0; }
int trace_present() { return 0; }
int trace_bridge() { return 0; }
int trace_exception() { return 0; }

}  // namespace nr::pe

namespace nr::pe::d3d12_passthrough {

nr::arch::GpuArch arch() { return g_arch.load(); }
uint32_t gfx11_mach() { return g_gfx11_mach.load(); }
void set_gfx11_mach(uint32_t mach) { g_gfx11_mach.store(mach); }
std::wstring aco_folder() { return g_aco_dir_w; }

void note_network(const char* name) {
    std::lock_guard<std::mutex> lk(g_net_note_mutex);
    g_last_net_name = name ? name : "";
}

bool enabled() { return true; }
void start() {}
void set_enabled(bool) {}
void on_device(ID3D12Device*) {}
Canary canary() { return Canary::kHit; }
const char* canary_name(Canary c) {
    switch (c) {
    case Canary::kHit: return "ok";
    case Canary::kFail: return "failed";
    case Canary::kRunning: return "running";
    case Canary::kNotRun: return "not run";
    }
    return "unknown";
}

}  // namespace nr::pe::d3d12_passthrough

namespace nr::pe::d3d12_direct {

bool install_hooks(ID3D12Device*, ID3D12GraphicsCommandList*, std::string*) { return true; }
void note_command_signature(ID3D12CommandSignature*, const D3D12_COMMAND_SIGNATURE_DESC*) {}
bool ready(ID3D12Device*, ID3D12GraphicsCommandList*, std::string*, bool* waiting_reset) {
    if (waiting_reset) *waiting_reset = false;
    return true;
}

Scope::Scope(ID3D12GraphicsCommandList* cl, bool restore) : cl_(cl), restore_(restore), entered_(true) {}
Scope::~Scope() { release(); }
void Scope::release() {
    cl_ = nullptr;
    restore_ = false;
    entered_ = false;
}

nr::gpu::Ticket book(ID3D12CommandList* list) {
    std::lock_guard<std::mutex> lk(g_rec_mutex);
    auto& t = g_open_recs[list];
    if (!t) t = std::make_shared<nr::gpu::Recording>();
    return t;
}

bool completed(const nr::gpu::Ticket& ticket) { return ticket && ticket->strict_ready(); }
void note_execution(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) {}
void begin_split_execution() {}
void end_split_execution(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) {}
void seal(const nr::gpu::Ticket& ticket) { if (ticket) ticket->seal(); }
void invalidate(const nr::gpu::Ticket& ticket) { if (ticket) { ticket->submitted(nullptr); ticket->seal(); } }
ID3D12CommandQueue* last_direct_queue() { return g_last_queue.load(); }
bool hooks_installed() { return true; }
uint64_t host_table_replays() { return 0; }
uint64_t nr_set_heap_count() { return 0; }
uint64_t nr_set_table_count() { return 0; }
void reset_counters() {}
size_t tracked_list_count() { return 0; }
size_t tracked_recording_count() { return 0; }
size_t tracked_queue_fence_count() { return 0; }

}  // namespace nr::pe::d3d12_direct

namespace nr::vram {

void install_vulkan() {}
void memory_types(const VkPhysicalDeviceMemoryProperties&) {}
void track_d3d12(ID3D12Device*, ID3D12Resource*, Kind) {}
void track_d3d11(ID3D11Resource*, Kind) {}
void track_d3d10(ID3D10Resource*, Kind) {}
void add_external(uint64_t, uint64_t, Kind) {}
void forget_external(uint64_t) {}
void forget_device(uint64_t) {}

}  // namespace nr::vram
