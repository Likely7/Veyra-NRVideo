// Presentation scaler check (2026-11-18). Drives the real PresentBlit shader
// offscreen and compares its fine path (now the default for every source)
// with a CPU model of the intended filter, the old bilinear blit, and a
// Lanczos-3 reference, then measures what one blit costs on the GPU.
//
//   veyra_present_scaling_tests [--shader-dir DIR] [--adapter N]
//   veyra_present_scaling_tests --image IN.raw --out DIR --size WxH [--size WxH ...]
//
// .raw: "VRAW", int32 width, int32 height, then width*height RGBA float32
// (linear light). Image mode renders the old and the new blit at each size.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace pv = DirectX::PackedVector;

namespace {

constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr double kPi = 3.14159265358979323846;
constexpr float kOldFlags = 0.0f;   // bilinear (what every non-PS5 source got)
constexpr float kFineFlags = 2.0f;  // fine sampling (now the default)

int failures = 0;
void check(bool ok, const std::string& label)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

struct Image {
    int w = 0, h = 0;
    std::vector<float> px; // RGBA rows
    Image() = default;
    Image(int width, int height) : w(width), h(height), px(size_t(width) * size_t(height) * 4, 0.0f) {}
    float& at(int x, int y, int c) { return px[(size_t(y) * size_t(w) + size_t(x)) * 4 + size_t(c)]; }
    float at(int x, int y, int c) const { return px[(size_t(y) * size_t(w) + size_t(x)) * 4 + size_t(c)]; }
    float clamped(int x, int y, int c) const { return at(std::clamp(x, 0, w - 1), std::clamp(y, 0, h - 1), c); }
};

// The GPU sees half floats; the CPU model must start from the same values.
void quantize(Image& img)
{
    for (auto& v : img.px) v = pv::XMConvertHalfToFloat(pv::XMConvertFloatToHalf(v));
}

Image noise(int w, int h, uint32_t seed)
{
    Image img(w, h);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) img.at(x, y, c) = u(rng);
            img.at(x, y, 3) = 1.0f;
        }
    quantize(img);
    return img;
}

// Radial chirp whose local frequency reaches the source Nyquist at r = w/2:
// shrinking it shows aliasing as false rings, enlarging it shows softness.
Image zonePlate(int w, int h)
{
    Image img(w, h);
    const double k = kPi / w;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double dx = x + 0.5 - w * 0.5, dy = y + 0.5 - h * 0.5;
            const float v = float(0.5 + 0.5 * std::cos(k * (dx * dx + dy * dy)));
            for (int c = 0; c < 3; ++c) img.at(x, y, c) = v;
            img.at(x, y, 3) = 1.0f;
        }
    quantize(img);
    return img;
}

// Clockwise quarter turn as PresentationSourceUv(turns = 1) samples it:
// R(i, j) = S(j, S.h - 1 - i), so filtering R unrotated equals the shader's
// filtering of S along the swapped axes.
Image quarterTurn(const Image& s)
{
    Image r(s.h, s.w);
    for (int j = 0; j < r.h; ++j)
        for (int i = 0; i < r.w; ++i)
            for (int c = 0; c < 4; ++c) r.at(i, j, c) = s.at(j, s.h - 1 - i, c);
    return r;
}

// PresentBlit's display mapping with fit = 1 (VideoPresenter cancels the
// contain fit with srcDims.w, so render* is the displayed size in pixels).
struct View {
    int dstW = 0, dstH = 0;
    double renderW = 0, renderH = 0;
    double cx = 0.5, cy = 0.5;
};
View fitView(int srcW, int srcH, int dstW, int dstH)
{
    const double s = std::min(double(dstW) / srcW, double(dstH) / srcH);
    return View{dstW, dstH, srcW * s, srcH * s};
}
double viewUv(int i, int dst, double render, double center) { return ((i + 0.5) / dst - 0.5) * dst / render + center; }

double catmullRom(double x)
{
    x = std::abs(x);
    if (x < 1.0) return (1.5 * x - 2.5) * x * x + 1.0;
    if (x < 2.0) return ((-0.5 * x + 2.5) * x - 4.0) * x + 2.0;
    return 0.0;
}
double lanczos3(double x)
{
    x = std::abs(x);
    if (x < 1e-9) return 1.0;
    if (x >= 3.0) return 0.0;
    const double a = kPi * x;
    return 3.0 * std::sin(a) * std::sin(a / 3.0) / (a * a);
}

using Taps = std::vector<std::pair<int, double>>;
Taps widenedTaps(double p, double scale, double support, double (*kernel)(double), int size)
{
    Taps taps;
    double total = 0;
    for (int i = int(std::ceil(p - support * scale)); i <= int(std::floor(p + support * scale)); ++i) {
        const double w = kernel((i - p) / scale);
        if (w == 0.0) continue;
        taps.emplace_back(std::clamp(i, 0, size - 1), w);
        total += w;
    }
    for (auto& t : taps) t.second /= total;
    return taps;
}

// Separable widened kernel (the shader's minification model, and the Lanczos
// reference), black outside the view.
Image separable(const Image& src, const View& v, double support, double (*kernel)(double), double maxScale)
{
    const double kx = std::clamp(src.w / v.renderW, 1.0, maxScale), ky = std::clamp(src.h / v.renderH, 1.0, maxScale);
    std::vector<Taps> tx(size_t(v.dstW)), ty(size_t(v.dstH));
    std::vector<char> ox(size_t(v.dstW)), oy(size_t(v.dstH));
    for (int X = 0; X < v.dstW; ++X) {
        const double u = viewUv(X, v.dstW, v.renderW, v.cx);
        ox[size_t(X)] = u < 0.0 || u > 1.0;
        tx[size_t(X)] = widenedTaps(u * src.w - 0.5, kx, support, kernel, src.w);
    }
    for (int Y = 0; Y < v.dstH; ++Y) {
        const double u = viewUv(Y, v.dstH, v.renderH, v.cy);
        oy[size_t(Y)] = u < 0.0 || u > 1.0;
        ty[size_t(Y)] = widenedTaps(u * src.h - 0.5, ky, support, kernel, src.h);
    }
    Image rows(v.dstW, src.h);
    for (int y = 0; y < src.h; ++y)
        for (int X = 0; X < v.dstW; ++X)
            for (int c = 0; c < 4; ++c) {
                double s = 0;
                for (const auto& [i, w] : tx[size_t(X)]) s += w * src.at(i, y, c);
                rows.at(X, y, c) = float(s);
            }
    Image out(v.dstW, v.dstH);
    for (int Y = 0; Y < v.dstH; ++Y)
        for (int X = 0; X < v.dstW; ++X) {
            if (ox[size_t(X)] || oy[size_t(Y)]) { out.at(X, Y, 3) = 1.0f; continue; }
            for (int c = 0; c < 4; ++c) {
                double s = 0;
                for (const auto& [i, w] : ty[size_t(Y)]) s += w * rows.at(X, i, c);
                out.at(X, Y, c) = float(s);
            }
        }
    return out;
}

// The old blit: hardware bilinear at the mapped position.
Image bilinearModel(const Image& src, const View& v)
{
    Image out(v.dstW, v.dstH);
    for (int Y = 0; Y < v.dstH; ++Y)
        for (int X = 0; X < v.dstW; ++X) {
            const double u = viewUv(X, v.dstW, v.renderW, v.cx), t = viewUv(Y, v.dstH, v.renderH, v.cy);
            if (u < 0.0 || u > 1.0 || t < 0.0 || t > 1.0) { out.at(X, Y, 3) = 1.0f; continue; }
            const double px = u * src.w - 0.5, py = t * src.h - 0.5;
            const int bx = int(std::floor(px)), by = int(std::floor(py));
            const double fx = px - bx, fy = py - by;
            for (int c = 0; c < 4; ++c)
                out.at(X, Y, c) = float((src.clamped(bx, by, c) * (1 - fx) + src.clamped(bx + 1, by, c) * fx) * (1 - fy) +
                                        (src.clamped(bx, by + 1, c) * (1 - fx) + src.clamped(bx + 1, by + 1, c) * fx) * fy);
        }
    return out;
}

// The shader's magnification: exact texel when aligned, else Catmull-Rom 4x4
// bounded to the local 2x2.
Image magnifyModel(const Image& src, const View& v)
{
    Image out(v.dstW, v.dstH);
    const auto weights = [](double t, double* w) {
        const double t2 = t * t, t3 = t2 * t;
        w[0] = -0.5 * t + t2 - 0.5 * t3;
        w[1] = 1.0 - 2.5 * t2 + 1.5 * t3;
        w[2] = 0.5 * t + 2.0 * t2 - 1.5 * t3;
        w[3] = -0.5 * t2 + 0.5 * t3;
    };
    for (int Y = 0; Y < v.dstH; ++Y)
        for (int X = 0; X < v.dstW; ++X) {
            const double u = viewUv(X, v.dstW, v.renderW, v.cx), t = viewUv(Y, v.dstH, v.renderH, v.cy);
            if (u < 0.0 || u > 1.0 || t < 0.0 || t > 1.0) { out.at(X, Y, 3) = 1.0f; continue; }
            const double px = u * src.w - 0.5, py = t * src.h - 0.5;
            if (std::abs(px - std::round(px)) < 0.0005 && std::abs(py - std::round(py)) < 0.0005) {
                for (int c = 0; c < 4; ++c) out.at(X, Y, c) = src.clamped(int(std::round(px)), int(std::round(py)), c);
                continue;
            }
            const int bx = int(std::floor(px)), by = int(std::floor(py));
            double wx[4], wy[4];
            weights(px - bx, wx);
            weights(py - by, wy);
            for (int c = 0; c < 4; ++c) {
                double s = 0;
                for (int j = 0; j < 4; ++j)
                    for (int i = 0; i < 4; ++i) s += src.clamped(bx + i - 1, by + j - 1, c) * wx[i] * wy[j];
                const double a = src.clamped(bx, by, c), b = src.clamped(bx + 1, by, c);
                const double d = src.clamped(bx, by + 1, c), e = src.clamped(bx + 1, by + 1, c);
                s = std::clamp(s, std::min(std::min(a, b), std::min(d, e)), std::max(std::max(a, b), std::max(d, e)));
                out.at(X, Y, c) = float(s);
            }
        }
    return out;
}

// The shader's minification for SDR-range pictures: Catmull-Rom widened by
// the shrink factor up to 4:1, and held at its 2:1 width past 4:1. Its
// anti-ringing hold only acts where a sample exceeds 1.0; the HDR block check
// covers that.
Image minifyModel(const Image& src, const View& v)
{
    const double widest = std::max(src.w / v.renderW, src.h / v.renderH);
    return separable(src, v, 2.0, catmullRom, widest > 4.001 ? 2.0 : 4.0);
}

// What the fine path should produce.
Image fineModel(const Image& src, const View& v)
{
    const bool magnify = src.w / v.renderW < 1.001 && src.h / v.renderH < 1.001;
    return magnify ? magnifyModel(src, v) : minifyModel(src, v);
}

struct Stats {
    double maxAbs = 0, meanAbs = 0, rms = 0;
};
// RGB inside the view (both images black outside it).
Stats compare(const Image& a, const Image& b)
{
    Stats s;
    double sum = 0, sq = 0;
    size_t n = 0;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x)
            for (int c = 0; c < 3; ++c) {
                const double d = std::abs(double(a.at(x, y, c)) - double(b.at(x, y, c)));
                s.maxAbs = std::max(s.maxAbs, d);
                sum += d;
                sq += d * d;
                ++n;
            }
    s.meanAbs = sum / double(n);
    s.rms = std::sqrt(sq / double(n));
    return s;
}

std::string narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::vector<uint8_t> readFile(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    uint64_t fenceValue = 0;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12DescriptorHeap> srvHeap, rtvHeap;
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> queryReadback;
    std::string adapterName;

    ~Gpu()
    {
        if (event) CloseHandle(event);
    }

    bool init(const fs::path& shaderDir, UINT adapterIndex)
    {
        const auto vs = readFile(shaderDir / L"PresentBlit_vs.dxil"), ps = readFile(shaderDir / L"PresentBlit_ps.dxil");
        if (vs.empty() || ps.empty()) {
            std::printf("shaders not found in %s\n", narrow(shaderDir.wstring()).c_str());
            return false;
        }
        ComPtr<IDXGIFactory6> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) return false;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        adapterName = narrow(desc.Description);
        if (GetEnvironmentVariableW(L"VEYRA_PRESENT_TEST_DEBUG", nullptr, 0) > 0) {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        }
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return false;
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return false;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))) return false;
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)))) return false;
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        // Same layout as pipeline::GraphicsPass: 16 root constants at b0, one
        // SRV table at t0, a static linear clamp sampler at s0.
        D3D12_DESCRIPTOR_RANGE1 range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_PARAMETER1 params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[0].Constants.Num32BitValues = 16;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &range;
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rd{};
        rd.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rd.Desc_1_1.NumParameters = 2;
        rd.Desc_1_1.pParameters = params;
        rd.Desc_1_1.NumStaticSamplers = 1;
        rd.Desc_1_1.pStaticSamplers = &sampler;
        ComPtr<ID3DBlob> blob, error;
        if (FAILED(D3D12SerializeVersionedRootSignature(&rd, &blob, &error))) return false;
        if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)))) return false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = root.Get();
        pd.VS = {vs.data(), vs.size()};
        pd.PS = {ps.data(), ps.size()};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = kFormat;
        pd.SampleDesc.Count = 1;
        if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 1;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap)))) return false;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap)))) return false;
        D3D12_QUERY_HEAP_DESC qh{};
        qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qh.Count = 2;
        if (FAILED(device->CreateQueryHeap(&qh, IID_PPV_ARGS(&queries)))) return false;
        queryReadback = buffer(16, D3D12_HEAP_TYPE_READBACK);
        return queryReadback != nullptr;
    }

    void execute()
    {
        list->Close();
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(fence.Get(), ++fenceValue);
        fence->SetEventOnCompletion(fenceValue, event);
        WaitForSingleObject(event, INFINITE);
        alloc->Reset();
        list->Reset(alloc.Get(), nullptr);
    }

    void barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = r;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        list->ResourceBarrier(1, &b);
    }

    ComPtr<ID3D12Resource> buffer(UINT64 size, D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = type;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = size;
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> r;
        const auto state = type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
        return r;
    }

    ComPtr<ID3D12Resource> texture(int w, int h, bool renderTarget)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = UINT64(w);
        d.Height = UINT(h);
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = kFormat;
        d.SampleDesc.Count = 1;
        d.Flags = renderTarget ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
        ComPtr<ID3D12Resource> r;
        const auto state = renderTarget ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COPY_DEST;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
        return r;
    }

    ComPtr<ID3D12Resource> upload(const Image& img)
    {
        auto tex = texture(img.w, img.h, false);
        if (!tex) return nullptr;
        const auto desc = tex->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT rows = 0;
        UINT64 rowBytes = 0, total = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rows, &rowBytes, &total);
        auto staging = buffer(total, D3D12_HEAP_TYPE_UPLOAD);
        if (!staging) return nullptr;
        uint8_t* mapped = nullptr;
        staging->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        for (int y = 0; y < img.h; ++y) {
            auto* row = reinterpret_cast<uint16_t*>(mapped + fp.Offset + size_t(y) * fp.Footprint.RowPitch);
            const float* from = &img.px[size_t(y) * size_t(img.w) * 4];
            for (size_t i = 0; i < size_t(img.w) * 4; ++i) row[i] = pv::XMConvertFloatToHalf(from[i]);
        }
        staging->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = tex.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = staging.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fp;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        barrier(tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        execute();
        return tex;
    }

    // Records `draws` blits exactly as VideoPresenter sets them up.
    void record(ID3D12Resource* src, ID3D12Resource* target, const View& v, float flags, int draws, bool timed)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = kFormat;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(src, &sd, srvHeap->GetCPUDescriptorHandleForHeapStart());
        const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target, nullptr, rtv);
        const float black[4] = {0, 0, 0, 1};
        list->ClearRenderTargetView(rtv, black, 0, nullptr);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const D3D12_VIEWPORT viewport{0, 0, float(v.dstW), float(v.dstH), 0, 1};
        const D3D12_RECT scissor{0, 0, LONG(v.dstW), LONG(v.dstH)};
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
        list->SetDescriptorHeaps(1, heaps);
        list->SetGraphicsRootSignature(root.Get());
        list->SetPipelineState(pso.Get());
        const double fit = std::min(v.dstW / v.renderW, v.dstH / v.renderH);
        const float constants[16] = {float(v.renderW), float(v.renderH), flags, float(1.0 / fit),
                                     float(v.dstW), float(v.dstH), float(v.cx), float(v.cy),
                                     0, 0, 203, 0, 0, 0, 0, 0};
        list->SetGraphicsRoot32BitConstants(0, 16, constants, 0);
        list->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (timed) list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        for (int i = 0; i < draws; ++i) list->DrawInstanced(3, 1, 0, 0);
        if (timed) {
            list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
            list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, queryReadback.Get(), 0);
        }
    }

    Image render(ID3D12Resource* src, const View& v, float flags)
    {
        auto target = texture(v.dstW, v.dstH, true);
        record(src, target.Get(), v, flags, 1, false);
        const auto desc = target->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT rows = 0;
        UINT64 rowBytes = 0, total = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rows, &rowBytes, &total);
        auto readback = buffer(total, D3D12_HEAP_TYPE_READBACK);
        barrier(target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource = target.Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst, 0, 0, 0, &from, nullptr);
        execute();
        Image out(v.dstW, v.dstH);
        uint8_t* mapped = nullptr;
        readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        for (int y = 0; y < v.dstH; ++y) {
            const auto* row = reinterpret_cast<const uint16_t*>(mapped + fp.Offset + size_t(y) * fp.Footprint.RowPitch);
            for (size_t i = 0; i < size_t(v.dstW) * 4; ++i) out.px[size_t(y) * size_t(v.dstW) * 4 + i] = pv::XMConvertHalfToFloat(row[i]);
        }
        readback->Unmap(0, nullptr);
        return out;
    }

    // Milliseconds per blit, averaged over back-to-back draws.
    double time(ID3D12Resource* src, const View& v, float flags, int draws)
    {
        auto target = texture(v.dstW, v.dstH, true);
        record(src, target.Get(), v, flags, 4, false);
        execute();
        record(src, target.Get(), v, flags, draws, true);
        execute();
        uint64_t* stamps = nullptr;
        queryReadback->Map(0, nullptr, reinterpret_cast<void**>(&stamps));
        const uint64_t elapsed = stamps[1] - stamps[0];
        queryReadback->Unmap(0, nullptr);
        UINT64 frequency = 1;
        queue->GetTimestampFrequency(&frequency);
        return double(elapsed) * 1000.0 / double(frequency) / draws;
    }
};

std::string dims(int w, int h) { return std::to_string(w) + "x" + std::to_string(h); }

// GPU fine path against the CPU model of the intended filter, plus the old
// bilinear path against its own model (proves the harness maps like the app).
void exactness(Gpu& gpu, const Image& src, const View& v, const std::string& label, double maxTolerance, double meanTolerance,
               const Image* modelSource = nullptr, float extraFlags = 0.0f)
{
    auto tex = gpu.upload(src);
    if (!tex) { check(false, label + " upload"); return; }
    const Image& model = modelSource ? *modelSource : src;
    const auto fine = gpu.render(tex.Get(), v, kFineFlags + extraFlags);
    const auto expect = fineModel(model, v);
    const auto s = compare(fine, expect);
    std::printf("  %s fine vs model: max %.6f mean %.7f\n", label.c_str(), s.maxAbs, s.meanAbs);
    check(s.maxAbs <= maxTolerance && s.meanAbs <= meanTolerance, label + " fine path matches the model");
    if (extraFlags == 0.0f) {
        const auto old = gpu.render(tex.Get(), v, kOldFlags);
        const auto so = compare(old, bilinearModel(model, v));
        std::printf("  %s old vs bilinear model: max %.6f mean %.7f\n", label.c_str(), so.maxAbs, so.meanAbs);
        check(so.maxAbs <= 6e-3, label + " old path matches the bilinear model (harness mapping)");
    }
}

void runChecks(Gpu& gpu)
{
    // 1:1 must stay a texel-exact copy (HDR and colour checks depend on it).
    {
        const auto src = noise(2560, 1440, 1);
        auto tex = gpu.upload(src);
        const View v{2560, 1440, 2560, 1440};
        const auto fine = gpu.render(tex.Get(), v, kFineFlags);
        const auto s = compare(fine, src);
        std::printf("  1:1 2560x1440 fine vs source: max %.9f\n", s.maxAbs);
        check(s.maxAbs == 0.0, "1:1 fine path is a texel-exact copy");
    }
    // Magnification (a 1080p capture on a 1440p or 4K screen).
    exactness(gpu, noise(1920, 1080, 2), View{2560, 1440, 2560, 1440}, "magnify 1920x1080->2560x1440", 1e-3, 2e-4);
    exactness(gpu, noise(1920, 1080, 3), View{3840, 2160, 3840, 2160}, "magnify 1920x1080->3840x2160", 1e-3, 2e-4);
    exactness(gpu, noise(1280, 720, 4), View{2560, 1440, 2560, 1440}, "magnify 1280x720->2560x1440", 1e-3, 2e-4);
    // Minification (a 4K frame on a 1440p/1080p screen or in a window). The
    // fine path pairs texels through the bilinear unit, whose 8-bit weight
    // fraction bounds the difference from the exact model.
    exactness(gpu, noise(3840, 2160, 5), View{2560, 1440, 2560, 1440}, "minify 3840x2160->2560x1440", 8e-3, 1e-3);
    exactness(gpu, noise(3840, 2160, 6), View{1920, 1080, 1920, 1080}, "minify 3840x2160->1920x1080", 8e-3, 1e-3);
    exactness(gpu, noise(3840, 2160, 7), View{1280, 720, 1280, 720}, "minify 3840x2160->1280x720", 8e-3, 1e-3);
    exactness(gpu, noise(3840, 2160, 11), View{640, 360, 640, 360}, "minify 3840x2160->640x360", 8e-3, 1e-3);
    exactness(gpu, noise(2560, 1440, 8), View{1920, 1080, 1920, 1080}, "minify 2560x1440->1920x1080", 8e-3, 1e-3);
    // Letterbox: bars stay exactly black, the picture maps like the app.
    {
        const auto src = noise(1920, 1080, 9);
        const View v{2560, 1600, 2560, 1440};
        exactness(gpu, src, v, "letterbox 1920x1080->2560x1600", 1e-3, 2e-4);
        auto tex = gpu.upload(src);
        const auto fine = gpu.render(tex.Get(), v, kFineFlags);
        bool black = true;
        for (int y : {0, 79, 1520, 1599})
            for (int x = 0; x < v.dstW; ++x)
                black = black && fine.at(x, y, 0) == 0.0f && fine.at(x, y, 1) == 0.0f && fine.at(x, y, 2) == 0.0f && fine.at(x, y, 3) == 1.0f;
        check(black, "letterbox bars stay opaque black");
    }
    // A clockwise quarter turn swaps which screen axis drives each source axis.
    {
        const auto src = noise(1920, 1080, 10);
        const auto turned = quarterTurn(src);
        exactness(gpu, src, View{2560, 1440, 810, 1440}, "quarter turn 1920x1080->810x1440", 8e-3, 1e-3, &turned, 64.0f);
    }
    // Linear-light HDR (scRGB output without frame generation filters linear
    // values): a 100.0 (8000-nit) block on a 0.2 background must not ring
    // below the background or above the block when shrunk or enlarged.
    for (const auto& [dw, dh] : {std::pair{2560, 1440}, std::pair{1920, 1080}, std::pair{1280, 720}, std::pair{5120, 2880}}) {
        Image src(3840, 2160);
        for (int y = 0; y < src.h; ++y)
            for (int x = 0; x < src.w; ++x) {
                const float value = x >= 1800 && x < 2041 && y >= 1000 && y < 1161 ? 100.0f : 0.2f;
                for (int c = 0; c < 3; ++c) src.at(x, y, c) = value;
                src.at(x, y, 3) = 1.0f;
            }
        auto tex = gpu.upload(src);
        const auto fine = gpu.render(tex.Get(), View{dw, dh, double(dw), double(dh)}, kFineFlags);
        float lo = 1e30f, hi = -1e30f;
        for (int y = 0; y < fine.h; ++y)
            for (int x = 0; x < fine.w; ++x)
                for (int c = 0; c < 3; ++c) { lo = std::min(lo, fine.at(x, y, c)); hi = std::max(hi, fine.at(x, y, c)); }
        std::printf("  HDR block 3840x2160->%s: min %.4f max %.4f\n", dims(dw, dh).c_str(), lo, hi);
        check(lo >= 0.2f - 1e-3f && hi <= 100.0f + 0.1f, "HDR block " + dims(dw, dh) + " has no ringing halo");
    }
    // Quality against a Lanczos-3 reference: aliasing when shrinking, softness
    // when enlarging. Lower is closer.
    const auto quality = [&](int sw, int sh, int dw, int dh, double requiredGain) {
        const auto src = zonePlate(sw, sh);
        auto tex = gpu.upload(src);
        const View v{dw, dh, double(dw), double(dh)};
        const auto reference = separable(src, v, 3.0, lanczos3, 8.0);
        const double oldRms = compare(gpu.render(tex.Get(), v, kOldFlags), reference).rms;
        const double newRms = compare(gpu.render(tex.Get(), v, kFineFlags), reference).rms;
        std::printf("  zone plate %s->%s rms vs Lanczos-3: old %.5f new %.5f (%.1fx closer)\n",
                    dims(sw, sh).c_str(), dims(dw, dh).c_str(), oldRms, newRms, oldRms / newRms);
        check(newRms * requiredGain <= oldRms, "zone plate " + dims(sw, sh) + "->" + dims(dw, dh) + " new blit closer to the reference");
    };
    quality(3840, 2160, 2560, 1440, 2.0);
    quality(3840, 2160, 1920, 1080, 2.0);
    quality(2560, 1440, 1920, 1080, 2.0);
    quality(1920, 1080, 2560, 1440, 1.2);
    quality(1920, 1080, 3840, 2160, 1.2);
}

void runTiming(Gpu& gpu)
{
    struct Case { int sw, sh, dw, dh; };
    const Case cases[] = {
        {3840, 2160, 3840, 2160}, {2560, 1440, 2560, 1440}, {1920, 1080, 2560, 1440}, {1920, 1080, 3840, 2160},
        {2560, 1440, 3840, 2160}, {1280, 720, 2560, 1440}, {3840, 2160, 2560, 1440}, {3840, 2160, 1920, 1080},
        {2560, 1440, 1920, 1080}, {3840, 2160, 1280, 720}, {3840, 2160, 640, 360},
    };
    double worst = 0;
    for (const auto& c : cases) {
        auto tex = gpu.upload(noise(c.sw, c.sh, 99));
        const View v{c.dw, c.dh, double(c.dw), double(c.dh)};
        const double oldMs = gpu.time(tex.Get(), v, kOldFlags, 200), newMs = gpu.time(tex.Get(), v, kFineFlags, 200);
        worst = std::max(worst, newMs);
        std::printf("  time %s->%s old %.3f ms new %.3f ms\n", dims(c.sw, c.sh).c_str(), dims(c.dw, c.dh).c_str(), oldMs, newMs);
    }
    check(worst <= 1.0, "every fine blit up to 4K stays under 1 ms on this GPU");
}

bool readRaw(const fs::path& path, Image& img)
{
    std::ifstream f(path, std::ios::binary);
    char magic[4]{};
    int32_t w = 0, h = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&w), 4);
    f.read(reinterpret_cast<char*>(&h), 4);
    if (!f || std::memcmp(magic, "VRAW", 4) != 0 || w <= 0 || h <= 0) return false;
    img = Image(w, h);
    f.read(reinterpret_cast<char*>(img.px.data()), std::streamsize(img.px.size() * sizeof(float)));
    return bool(f);
}

bool writeRaw(const fs::path& path, const Image& img)
{
    std::ofstream f(path, std::ios::binary);
    const int32_t w = img.w, h = img.h;
    f.write("VRAW", 4);
    f.write(reinterpret_cast<const char*>(&w), 4);
    f.write(reinterpret_cast<const char*>(&h), 4);
    f.write(reinterpret_cast<const char*>(img.px.data()), std::streamsize(img.px.size() * sizeof(float)));
    return bool(f);
}

int imageMode(Gpu& gpu, const fs::path& in, const fs::path& outDir, const std::vector<std::pair<int, int>>& sizes)
{
    Image src;
    if (!readRaw(in, src)) { std::printf("cannot read %s\n", narrow(in.wstring()).c_str()); return 2; }
    quantize(src);
    auto tex = gpu.upload(src);
    fs::create_directories(outDir);
    const auto stem = in.stem().wstring();
    for (const auto& [w, h] : sizes) {
        const auto v = fitView(src.w, src.h, w, h);
        for (const bool fine : {false, true}) {
            const float flags = fine ? kFineFlags : kOldFlags;
            const auto out = gpu.render(tex.Get(), v, flags);
            const auto name = stem + L"-" + std::to_wstring(w) + L"x" + std::to_wstring(h) + (fine ? L"-new.raw" : L"-old.raw");
            if (!writeRaw(outDir / name, out)) return 2;
            std::printf("  %s %s->%s %.3f ms\n", fine ? "new" : "old", dims(src.w, src.h).c_str(), dims(w, h).c_str(),
                        gpu.time(tex.Get(), v, flags, 100));
        }
    }
    return 0;
}

fs::path executableDirectory()
{
    wchar_t buffer[MAX_PATH * 4]{};
    GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    return fs::path(buffer).parent_path();
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    fs::path shaderDir = executableDirectory() / L"shaders", image, outDir;
    UINT adapter = 0;
    std::vector<std::pair<int, int>> sizes;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == L"--shader-dir" && hasValue) shaderDir = argv[++i];
        else if (arg == L"--adapter" && hasValue) adapter = UINT(std::wcstoul(argv[++i], nullptr, 10));
        else if (arg == L"--image" && hasValue) image = argv[++i];
        else if (arg == L"--out" && hasValue) outDir = argv[++i];
        else if (arg == L"--size" && hasValue) {
            int w = 0, h = 0;
            if (swscanf_s(argv[++i], L"%dx%d", &w, &h) == 2 && w > 0 && h > 0) sizes.emplace_back(w, h);
        }
    }
    Gpu gpu;
    if (!gpu.init(shaderDir, adapter)) {
        std::printf("FAIL GPU setup\n");
        return 2;
    }
    std::printf("adapter: %s\nshaders: %s\n", gpu.adapterName.c_str(), narrow(shaderDir.wstring()).c_str());
    if (!image.empty()) return imageMode(gpu, image, outDir.empty() ? fs::path(L".") : outDir, sizes);
    runChecks(gpu);
    runTiming(gpu);
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
