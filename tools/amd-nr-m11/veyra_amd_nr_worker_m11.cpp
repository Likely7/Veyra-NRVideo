#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "veyra/pipeline/Rdna3NrProtocol.h"
#include "d3d12_host_shim.hpp"
#include "nr_d3d12_net_plan.hpp"
#include "nr_d3d12_passthrough.hpp"
#include "nr_d3d12_passthrough_backend.hpp"
#include "nr_d3d12_passthrough_net.hpp"
#include "nr_ini_network.hpp"
#include "nr_path_u8.hpp"
#include "nr_pe_log.hpp"

using Microsoft::WRL::ComPtr;
using namespace veyra::pipeline::rdna3;

namespace {

void ck(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s failed: HRESULT=0x%08x", what, unsigned(hr));
        throw std::runtime_error(buf);
    }
}

bool pipe_read(HANDLE p, void* buf, DWORD sz) {
    DWORD off = 0, got = 0;
    while (off < sz) {
        if (!ReadFile(p, static_cast<char*>(buf) + off, sz - off, &got, nullptr) || !got) return false;
        off += got;
    }
    return true;
}

void pipe_write(HANDLE p, const void* buf, DWORD sz) {
    DWORD off = 0, put = 0;
    while (off < sz) {
        if (!WriteFile(p, static_cast<const char*>(buf) + off, sz - off, &put, nullptr) || !put) {
            throw std::runtime_error("IPC pipe write failed");
        }
        off += put;
    }
}

void send_reply(HANDLE outW, uint64_t seq, int32_t code, const std::string& msg) {
    Reply r{};
    r.h.sequence = seq;
    r.result = code;
    if (!msg.empty()) {
        std::snprintf(r.message.data(), r.message.size(), "%s", msg.c_str());
    }
    pipe_write(outW, &r, sizeof r);
}

void barrier_tr(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (before == after) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {res, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    cmd->ResourceBarrier(1, &b);
}

void barrier_uav(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res = nullptr) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = res;
    cmd->ResourceBarrier(1, &b);
}

std::string to_lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

int parse_model_token(const std::string& raw) {
    const std::string t = to_lower(trim(raw));
    if (t.empty()) return -1;
    if (t == "0" || t == "small" || t == "cc_small_fp8" || t == "small_fp8") return nr::kSmallFp8;
    if (t == "1" || t == "original" || t == "aardwold" || t == "cc_silver_aardwold") return nr::kSilverAardwold;
    if (t == "2" || t == "optimized" || t == "balanced" || t == "int4" || t == "cc_silver_aardwold_optimized")
        return nr::kSilverAardwoldOptimized;
    if (t == "3" || t == "experimental" || t == "small_exp" || t == "cc_small_fp8_experimental")
        return nr::kSmallFp8Experimental;
    if (t == "4" || t == "quality" || t == "optimized_fp8" || t == "cc_silver_aardwold_optimized_fp8")
        return nr::kSilverAardwoldOptimizedFp8;
    if (t == "5" || t == "performance" || t == "int4_performance" || t == "cc_silver_aardwold_optimized_performance")
        return nr::kSilverAardwoldOptimizedPerformance;
    if (t == "6" || t == "ultra" || t == "int4_ultra" || t == "cc_silver_aardwold_optimized_ultra")
        return nr::kSilverAardwoldOptimizedUltra;
    return nr::network_value(t);
}

struct WorkerConfig {
    int model_choice = nr::kSmallFp8;
    bool timings_csv = false;
};

void apply_ini_file(const std::filesystem::path& ini_path, uint32_t preset, WorkerConfig& cfg) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(ini_path, ec)) return;
    FILE* f = nullptr;
    _wfopen_s(&f, ini_path.wstring().c_str(), L"r");
    if (!f) return;
    bool in_section = false;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        std::string s = line;
        const auto cpos = s.find_first_of(";#");
        if (cpos != std::string::npos) s = s.substr(0, cpos);
        s = trim(s);
        if (s.empty()) continue;
        if (s.front() == '[') {
            const std::string sec = to_lower(s);
            in_section = (sec == "[mochi]" || sec == "[network]");
            continue;
        }
        const auto eq = s.find('=');
        if (!in_section || eq == std::string::npos) continue;
        const std::string k = to_lower(trim(s.substr(0, eq)));
        const std::string v = trim(s.substr(eq + 1));
        if (k == "model") {
            const int m = parse_model_token(v);
            if (m >= 0) cfg.model_choice = m;
        } else if (k == "preset1model" && preset == 1) {
            const int m = parse_model_token(v);
            if (m >= 0) cfg.model_choice = m;
        } else if (k == "preset2model" && preset == 2) {
            const int m = parse_model_token(v);
            if (m >= 0) cfg.model_choice = m;
        } else if (k == "timingscsv") {
            const std::string lv = to_lower(v);
            cfg.timings_csv = (lv == "1" || lv == "true" || lv == "on" || lv == "yes");
        }
    }
    std::fclose(f);
}

WorkerConfig resolve_config(const std::filesystem::path& runtime_dir, const std::filesystem::path& exe_dir,
                            const std::filesystem::path& data_dir, uint32_t preset) {
    WorkerConfig cfg;
    if (preset >= 10 && preset <= 16) {
        cfg.model_choice = int(preset - 10);
    } else if (preset == 1) {
        cfg.model_choice = nr::kSilverAardwold;
    } else {
        cfg.model_choice = nr::kSmallFp8;
    }

    // Apply ini overrides in increasing precedence: runtime -> exe -> data.
    apply_ini_file(runtime_dir / L"veyra-mochi.ini", preset, cfg);
    if (exe_dir != runtime_dir) apply_ini_file(exe_dir / L"veyra-mochi.ini", preset, cfg);
    if (!data_dir.empty() && data_dir != runtime_dir && data_dir != exe_dir) {
        apply_ini_file(data_dir / L"veyra-mochi.ini", preset, cfg);
    }

    // Environment overrides take top precedence (for benchmarks / A/B tests).
    for (const char* env_name : {"VEYRA_NR_M11_MODEL", "DLSSNR_MODEL"}) {
        char buf[128]{};
        const DWORD n = GetEnvironmentVariableA(env_name, buf, sizeof buf);
        if (n > 0 && n < sizeof buf) {
            const int m = parse_model_token(buf);
            if (m >= 0) {
                cfg.model_choice = m;
                break;
            }
        }
    }
    char tbuf[16]{};
    if (GetEnvironmentVariableA("VEYRA_NR_TIMINGS", tbuf, sizeof tbuf) > 0) {
        const std::string tv = to_lower(tbuf);
        if (tv == "1" || tv == "true") cfg.timings_csv = true;
    }
    return cfg;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    HANDLE inR = reinterpret_cast<HANDLE>(uintptr_t(_wcstoui64(argv[1], nullptr, 10)));
    HANDLE outW = reinterpret_cast<HANDLE>(uintptr_t(_wcstoui64(argv[2], nullptr, 10)));

    uint64_t init_seq = 0;
    try {
        Init init{};
        if (!pipe_read(inR, &init, sizeof init)) return 3;
        init_seq = init.h.sequence;
        if (!validHeader(init.h, Op::Init, sizeof init)) {
            send_reply(outW, init_seq, -1, "Invalid Rdna3NrProtocol Init header");
            return 4;
        }
        const uint32_t w = init.width, h = init.height;
        if (!w || !h || w > 2560u || h > 1440u || uint64_t(w) * h > 2560ull * 1440ull) {
            send_reply(outW, init_seq, -2, "Unsupported RDNA3 NR extent (max 2560x1440)");
            return 5;
        }

        wchar_t exe_buf[MAX_PATH * 4]{};
        GetModuleFileNameW(nullptr, exe_buf, sizeof exe_buf / sizeof(wchar_t));
        const std::filesystem::path exe_dir = std::filesystem::path(exe_buf).parent_path();
        std::filesystem::path runtime_dir;
        std::error_code ec;
        std::vector<std::filesystem::path> candidates;
        if (init.runtime[0]) {
            const std::filesystem::path ir(init.runtime.data());
            candidates.push_back(ir);
            candidates.push_back(ir / L"mochi");
        }
        candidates.push_back(exe_dir);
        candidates.push_back(exe_dir / L"mochi");
        for (const auto& cand : candidates) {
            if (std::filesystem::is_directory(cand / L"dlssnr-amd" / L"aco" / L"rdna3" / L"records", ec)) {
                runtime_dir = cand;
                break;
            }
        }
        if (runtime_dir.empty()) runtime_dir = init.runtime[0] ? std::filesystem::path(init.runtime.data()) : exe_dir;
        const std::filesystem::path data_dir = init.data[0] ? std::filesystem::path(init.data.data()) : std::filesystem::path{};
        std::filesystem::path log_path = init.log[0]
            ? std::filesystem::path(init.log.data())
            : (exe_dir / L"veyra_amd_nr_worker_m11.log");

        const WorkerConfig cfg = resolve_config(runtime_dir, exe_dir, data_dir, init.preset);
        const std::string root_u8 = nr::u8::narrow(runtime_dir.wstring());
        const std::string log_u8 = nr::u8::narrow(log_path.wstring());

        if (!m11_shim::init_runtime(root_u8, log_u8, cfg.model_choice, true)) {
            send_reply(outW, init_seq, -3, "Missing dlssnr-amd\\aco\\rdna3\\records runtime under " + root_u8);
            return 6;
        }
        nr::pe::log("[worker-m11] Init %ux%u preset=%u model_choice=%d root=%s",
                    w, h, init.preset, cfg.model_choice, root_u8.c_str());

        // Select matching DXGI adapter by LUID (fallback to discrete 0x1002).
        ComPtr<IDXGIFactory4> factory;
        ck(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        ComPtr<IDXGIAdapter1> adapter;
        LUID target_luid{};
        std::memcpy(&target_luid, &init.adapterLuid, sizeof target_luid);
        if (init.adapterLuid != 0) {
            if (FAILED(factory->EnumAdapterByLuid(target_luid, IID_PPV_ARGS(&adapter)))) {
                adapter.Reset();
            }
        }
        if (!adapter) {
            DXGI_ADAPTER_DESC1 best_desc{};
            for (UINT i = 0;; ++i) {
                ComPtr<IDXGIAdapter1> cand;
                if (factory->EnumAdapters1(i, &cand) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_ADAPTER_DESC1 desc{};
                cand->GetDesc1(&desc);
                if (desc.VendorId != 0x1002 || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
                if (!adapter || desc.DedicatedVideoMemory > best_desc.DedicatedVideoMemory) {
                    adapter = cand;
                    best_desc = desc;
                }
            }
        }
        if (!adapter) throw std::runtime_error("No matching AMD DXGI adapter found");

        ComPtr<ID3D12Device> device;
        ck(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue;
        ck(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        m11_shim::set_last_direct_queue(queue.Get());

        // Open shared IPC resources and fences (including unused depth/motion guides to validate contract).
        ComPtr<ID3D12Resource> tex_in, tex_out, tex_depth, tex_motion, valid_buf;
        ComPtr<ID3D12Fence> producer_fence, done_fence;
        ck(device->OpenSharedHandle(reinterpret_cast<HANDLE>(uintptr_t(init.handles[0])), IID_PPV_ARGS(&tex_in)), "OpenSharedHandle image[0]");
        ck(device->OpenSharedHandle(reinterpret_cast<HANDLE>(uintptr_t(init.handles[1])), IID_PPV_ARGS(&tex_out)), "OpenSharedHandle image[1]");
        if (init.handles[2]) {
            ck(device->OpenSharedHandle(reinterpret_cast<HANDLE>(uintptr_t(init.handles[2])), IID_PPV_ARGS(&tex_depth)), "OpenSharedHandle image[2]");
        }
        if (init.handles[3]) {
            ck(device->OpenSharedHandle(reinterpret_cast<HANDLE>(uintptr_t(init.handles[3])), IID_PPV_ARGS(&tex_motion)), "OpenSharedHandle image[3]");
        }
        ck(device->OpenSharedHandle(reinterpret_cast<HANDLE>(uintptr_t(init.handles[4])), IID_PPV_ARGS(&producer_fence)), "OpenSharedHandle producer_fence");
        ck(device->OpenSharedHandle(reinterpret_cast<HANDLE>(uintptr_t(init.handles[5])), IID_PPV_ARGS(&done_fence)), "OpenSharedHandle done_fence");
        ck(device->OpenSharedHandle(reinterpret_cast<HANDLE>(uintptr_t(init.handles[6])), IID_PPV_ARGS(&valid_buf)), "OpenSharedHandle valid_buf");
        for (auto hval : init.handles) {
            if (hval) CloseHandle(reinterpret_cast<HANDLE>(uintptr_t(hval)));
        }

        // Build Direct ELF passthrough network synchronously so frame 1 runs NR immediately.
        auto t_b0 = std::chrono::steady_clock::now();
        auto net = nr::pe::d3d12_passthrough::make_aco_network();
        std::string why;
        const char* cwhy = nullptr;
        if (!net->create(device.Get(), &cwhy)) {
            throw std::runtime_error(std::string("make_aco_network create failed: ") + (cwhy ? cwhy : ""));
        }
        while (!net->ready(device.Get(), w, h, &why)) {
            if (!net->building()) {
                throw std::runtime_error("net->ready failed: " + why);
            }
            Sleep(5);
        }
        const double build_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_b0).count();
        nr::pe::log("[worker-m11] Network READY in %.1f ms: %s (gfx11_mach=0x%02x)",
                    build_ms, net->name() ? net->name() : "", nr::pe::d3d12_passthrough::gfx11_mach());

        const uint32_t pitch_texels = nr::pe::d3d12_passthrough::lin_pitch(w, 8);
        const UINT row_pitch_bytes = UINT(pitch_texels) * 8u;
        const UINT64 lin_bytes = UINT64(row_pitch_bytes) * UINT64(h);

        auto make_linear_uav = [&](ComPtr<ID3D12Resource>& out_res) {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = lin_bytes;
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ck(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON,
                                               nullptr, IID_PPV_ARGS(&out_res)),
               "CreateCommittedResource linear NR buffer");
        };
        ComPtr<ID3D12Resource> lin_in, lin_out;
        make_linear_uav(lin_in);
        make_linear_uav(lin_out);

        const nr::pe::d3d12_passthrough::LinearFrame in_frame{
            lin_in.Get(), lin_in->GetGPUVirtualAddress(), w, h, lin_bytes};
        const nr::pe::d3d12_passthrough::LinearFrame out_frame{
            lin_out.Get(), lin_out->GetGPUVirtualAddress(), w, h, lin_bytes};

        D3D12_TEXTURE_COPY_LOCATION lin_loc{};
        lin_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        lin_loc.PlacedFootprint.Offset = 0;
        lin_loc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        lin_loc.PlacedFootprint.Footprint.Width = w;
        lin_loc.PlacedFootprint.Footprint.Height = h;
        lin_loc.PlacedFootprint.Footprint.Depth = 1;
        lin_loc.PlacedFootprint.Footprint.RowPitch = row_pitch_bytes;

        constexpr UINT kSlots = 8;
        struct CmdSlot {
            ComPtr<ID3D12CommandAllocator> alloc;
            ComPtr<ID3D12GraphicsCommandList2> cmd;
            uint64_t fence_val = 0;
            uint64_t seq = 0;
            double cpu_submit_ms = 0.0;
            bool has_ts = false;
        } slots[kSlots];

        for (UINT i = 0; i < kSlots; ++i) {
            ck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slots[i].alloc)),
               "CreateCommandAllocator");
            ComPtr<ID3D12GraphicsCommandList> base;
            ck(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slots[i].alloc.Get(), nullptr,
                                         IID_PPV_ARGS(&base)),
               "CreateCommandList");
            ck(base.As(&slots[i].cmd), "QueryInterface ID3D12GraphicsCommandList2");
            ck(slots[i].cmd->Close(), "Close initial slot cmd");
        }

        ComPtr<ID3D12Fence> local_fence;
        ck(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&local_fence)), "CreateFence local");
        HANDLE fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        uint64_t local_seq = 0;
        auto wait_local = [&](uint64_t target) {
            if (!target || local_fence->GetCompletedValue() >= target) return;
            ck(local_fence->SetEventOnCompletion(target, fence_event), "SetEventOnCompletion");
            if (WaitForSingleObject(fence_event, 30000) != WAIT_OBJECT_0 || local_fence->GetCompletedValue() == UINT64_MAX) {
                throw std::runtime_error("Worker local GPU timeout or device removed");
            }
        };

        // Optional per-frame GPU timestamp logging.
        ComPtr<ID3D12QueryHeap> ts_heap;
        ComPtr<ID3D12Resource> ts_rb;
        uint64_t ts_freq = 0;
        std::string model_label = net->name() ? net->name() : "D3D12_M11";
        std::string model_csv = model_label;
        for (char& ch : model_csv) if (ch == ',') ch = ';';
        std::ofstream timing_csv;
        if (cfg.timings_csv) {
            D3D12_QUERY_HEAP_DESC qhd{};
            qhd.Count = kSlots * 4;
            qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            if (SUCCEEDED(device->CreateQueryHeap(&qhd, IID_PPV_ARGS(&ts_heap))) &&
                SUCCEEDED(queue->GetTimestampFrequency(&ts_freq)) && ts_freq > 0) {
                D3D12_HEAP_PROPERTIES hp{};
                hp.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC rd{};
                rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                rd.Width = kSlots * 4 * sizeof(uint64_t);
                rd.Height = 1;
                rd.DepthOrArraySize = 1;
                rd.MipLevels = 1;
                rd.SampleDesc.Count = 1;
                rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if (SUCCEEDED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                              IID_PPV_ARGS(&ts_rb)))) {
                    std::filesystem::path csv_path = log_path;
                    csv_path += L".timings.csv";
                    timing_csv.open(csv_path, std::ios::trunc);
                    if (timing_csv) timing_csv << "sequence,model,gpu_network_ms,gpu_total_ms,cpu_record_submit_ms,applied\n";
                }
            }
        }

        // Send Init OK reply.
        send_reply(outW, init_seq, 0, model_label);

        uint32_t slot_idx = 0;
        uint64_t last_seq = 0;
        Header hdr{};
        while (pipe_read(inR, &hdr, sizeof hdr)) {
            if (hdr.op == Op::Stop) {
                wait_local(local_seq);
                if (timing_csv && ts_rb) {
                    uint64_t* qdata = nullptr;
                    D3D12_RANGE rr{0, kSlots * 4 * sizeof(uint64_t)};
                    if (SUCCEEDED(ts_rb->Map(0, &rr, reinterpret_cast<void**>(&qdata)))) {
                        for (UINT i = 0; i < kSlots; ++i) {
                            if (slots[i].has_ts) {
                                const double t2m = 1000.0 / double(ts_freq);
                                const double net_ms = double(qdata[i * 4 + 2] - qdata[i * 4 + 1]) * t2m;
                                const double tot_ms = double(qdata[i * 4 + 3] - qdata[i * 4 + 0]) * t2m;
                                timing_csv << slots[i].seq << "," << model_csv << ","
                                           << net_ms << "," << tot_ms << ","
                                           << slots[i].cpu_submit_ms << ",1\n";
                            }
                        }
                        D3D12_RANGE wr{0, 0};
                        ts_rb->Unmap(0, &wr);
                    }
                    timing_csv.flush();
                }
                nr::pe::log("[worker-m11] Stop drained (frames_seq=%llu)", static_cast<unsigned long long>(local_seq));
                _exit(0);
            }

            if (hdr.op != Op::Evaluate || hdr.bytes != sizeof(Evaluate)) {
                send_reply(outW, hdr.sequence, -10, "Unexpected protocol op/size");
                _exit(10);
            }

            Evaluate ev{};
            ev.h = hdr;
            if (!pipe_read(inR, reinterpret_cast<char*>(&ev) + sizeof(Header), sizeof(Evaluate) - sizeof(Header))) {
                break;
            }
            if (ev.h.sequence != last_seq + 1 || !std::isfinite(ev.intensity) ||
                ev.intensity < 0.0f || ev.intensity > 1.0f) {
                send_reply(outW, ev.h.sequence, -12, "Invalid/stale Evaluate sequence or intensity");
                _exit(12);
            }
            const auto t_sub0 = std::chrono::steady_clock::now();

            CmdSlot& s = slots[slot_idx];
            slot_idx = (slot_idx + 1) % kSlots;
            wait_local(s.fence_val);

            if (s.has_ts && timing_csv && ts_rb) {
                uint64_t* qdata = nullptr;
                const size_t byte_off = size_t(&s - slots) * 4 * sizeof(uint64_t);
                D3D12_RANGE rr{byte_off, byte_off + 4 * sizeof(uint64_t)};
                if (SUCCEEDED(ts_rb->Map(0, &rr, reinterpret_cast<void**>(&qdata)))) {
                    const size_t base = size_t(&s - slots) * 4;
                    const double t2m = 1000.0 / double(ts_freq);
                    const double net_ms = double(qdata[base + 2] - qdata[base + 1]) * t2m;
                    const double tot_ms = double(qdata[base + 3] - qdata[base + 0]) * t2m;
                    timing_csv << s.seq << "," << model_csv << ","
                               << net_ms << "," << tot_ms << ","
                               << s.cpu_submit_ms << ",1\n";
                    D3D12_RANGE wr{0, 0};
                    ts_rb->Unmap(0, &wr);
                }
                s.has_ts = false;
            }

            // Ensure GPU waits for producer before reading image[0] or writing image[1]/valid_buf.
            ck(queue->Wait(producer_fence.Get(), ev.h.sequence), "Queue Wait producer_fence");

            // Host Shader (NrResidualComposite.hlsl) applies user intensity; NR itself runs at full strength + auto mask.
            nr::D3d12NetControls nc{};
            nc.intensity = 1.0f;
            nc.local_tone = 1.0f;
            nc.local_structure = 1.0f;
            nc.skin_structure = -1.0f;
            nc.automatic_mask = true;
            net->controls(nc);

            nr::pe::d3d12_passthrough::NetTemporal nt{};
            nt.have_motion = false;
            nt.have_depth = false;
            nt.feature = 1;
            nt.reset = (ev.reset != 0);
            net->temporal(nt);

            ck(s.alloc->Reset(), "Reset slot allocator");
            ck(s.cmd->Reset(s.alloc.Get(), nullptr), "Reset slot cmd");
            m11_shim::begin_recording(s.cmd.Get());

            const UINT qbase = UINT(&s - slots) * 4;
            if (ts_heap) s.cmd->EndQuery(ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qbase + 0);

            {
                D3D12_RESOURCE_BARRIER b[3]{};
                b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[0].Transition = {tex_in.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE};
                b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[1].Transition = {lin_in.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST};
                b[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[2].Transition = {lin_out.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
                s.cmd->ResourceBarrier(3, b);

                D3D12_TEXTURE_COPY_LOCATION src_tex{}, dst_buf = lin_loc;
                src_tex.pResource = tex_in.Get();
                src_tex.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst_buf.pResource = lin_in.Get();
                s.cmd->CopyTextureRegion(&dst_buf, 0, 0, 0, &src_tex, nullptr);

                D3D12_RESOURCE_BARRIER b2[2]{};
                b2[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b2[0].Transition = {lin_in.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
                b2[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b2[1].Transition = {tex_in.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON};
                s.cmd->ResourceBarrier(2, b2);
            }

            if (ts_heap) s.cmd->EndQuery(ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qbase + 1);
            const char* rec_why = nullptr;
            if (!net->record(s.cmd.Get(), in_frame, out_frame, &rec_why)) {
                send_reply(outW, ev.h.sequence, -11, std::string("net->record failed: ") + (rec_why ? rec_why : ""));
                _exit(11);
            }
            barrier_uav(s.cmd.Get(), lin_out.Get());
            if (ts_heap) s.cmd->EndQuery(ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qbase + 2);

            {
                D3D12_RESOURCE_BARRIER b[4]{};
                b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[0].Transition = {lin_out.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
                b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[1].Transition = {lin_in.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON};
                b[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[2].Transition = {tex_out.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST};
                b[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b[3].Transition = {valid_buf.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST};
                s.cmd->ResourceBarrier(4, b);

                D3D12_TEXTURE_COPY_LOCATION dst_tex{}, src_buf = lin_loc;
                dst_tex.pResource = tex_out.Get();
                dst_tex.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src_buf.pResource = lin_out.Get();
                s.cmd->CopyTextureRegion(&dst_tex, 0, 0, 0, &src_buf, nullptr);

                D3D12_WRITEBUFFERIMMEDIATE_PARAMETER wb{valid_buf->GetGPUVirtualAddress(), 1u};
                s.cmd->WriteBufferImmediate(1, &wb, nullptr);

                D3D12_RESOURCE_BARRIER b2[3]{};
                b2[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b2[0].Transition = {lin_out.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON};
                b2[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b2[1].Transition = {tex_out.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON};
                b2[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b2[2].Transition = {valid_buf.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON};
                s.cmd->ResourceBarrier(3, b2);
            }

            if (ts_heap && ts_rb) {
                s.cmd->EndQuery(ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qbase + 3);
                s.cmd->ResolveQueryData(ts_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, qbase, 4,
                                        ts_rb.Get(), qbase * sizeof(uint64_t));
                s.has_ts = true;
            }
            ck(s.cmd->Close(), "Close slot cmd");

            ID3D12CommandList* lists[] = {s.cmd.Get()};
            queue->ExecuteCommandLists(1, lists);
            s.fence_val = ++local_seq;
            s.seq = ev.h.sequence;
            ck(queue->Signal(local_fence.Get(), s.fence_val), "Signal local_fence");
            ck(queue->Signal(done_fence.Get(), ev.h.sequence), "Signal done_fence");
            m11_shim::end_recording(s.cmd.Get(), local_fence.Get(), s.fence_val);
            s.cpu_submit_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_sub0).count();
            last_seq = ev.h.sequence;

            send_reply(outW, ev.h.sequence, 0, "");
        }
        wait_local(local_seq);
        _exit(0);
    } catch (const std::exception& e) {
        nr::pe::log("[worker-m11] FATAL: %s", e.what());
        send_reply(outW, init_seq, -99, e.what());
        _exit(1);
    }
}
