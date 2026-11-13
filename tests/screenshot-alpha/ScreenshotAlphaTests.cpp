#include "veyra/sink/ImageExportSink.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/Log.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <DirectXPackedVector.h>
#include <wincodec.h>
#include <d3d12sdklayers.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace veyra;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace {
int failures = 0;
void require(bool ok, const char* stage) {
    if (!ok) throw std::runtime_error(stage);
}
void check(bool ok, const char* name) {
    std::cout << name << " pass=" << ok << std::endl;
    failures += !ok;
}
sink::RgbaImage fixture() {
    sink::RgbaImage image{256, 256, std::vector<uint8_t>(256 * 256 * 4)};
    for (unsigned y = 0; y < image.height; ++y) for (unsigned x = 0; x < image.width; ++x) {
        auto* p = image.pixels.data() + (size_t(y) * image.width + x) * 4;
        const auto v = uint8_t(x % 32);
        if (y < 64) p[0] = p[1] = p[2] = uint8_t(x); // smooth near-black through white
        else if (y < 128) { p[0] = v / 4; p[1] = v; p[2] = uint8_t((x + y) % 17); }
        else if (y < 192) { p[0] = v; p[1] = v / 5; p[2] = uint8_t((x * 3 + y) % 13); }
        else { p[0] = v; p[1] = uint8_t(v * 2 / 3); p[2] = v / 3; }
        p[3] = uint8_t(y); // includes zero, low alpha, and fully opaque
    }
    image.pixels[0] = 33; image.pixels[1] = 33; image.pixels[2] = 29; image.pixels[3] = 4;
    return image;
}
sink::RgbaImage opaque(sink::RgbaImage image) {
    for (size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;
    return image;
}
// Materialize both conversions: a lazy converter chain could bypass the 8-bit
// intermediate and would not exercise the loss occurring in image consumers.
sink::RgbaImage convert(const sink::RgbaImage& image, REFGUID from, REFGUID to) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converter;
    require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))), "WIC factory");
    require(SUCCEEDED(factory->CreateBitmapFromMemory(image.width, image.height, from, image.width * 4,
        UINT(image.pixels.size()), const_cast<BYTE*>(image.pixels.data()), &bitmap)), "WIC bitmap");
    require(SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(bitmap.Get(), to, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)), "WIC converter");
    auto result = image;
    require(SUCCEEDED(converter->CopyPixels(nullptr, image.width * 4, UINT(result.pixels.size()), result.pixels.data())), "WIC pixels");
    return result;
}
sink::RgbaImage premultipliedRoundTrip(const sink::RgbaImage& image) {
    return convert(convert(image, GUID_WICPixelFormat32bppRGBA, GUID_WICPixelFormat32bppPRGBA),
        GUID_WICPixelFormat32bppPRGBA, GUID_WICPixelFormat32bppRGBA);
}
void dump(const fs::path& path, const sink::RgbaImage& image) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(image.pixels.data()), std::streamsize(image.pixels.size()));
    require(bool(file), "dump RGBA");
}
void codecs(const fs::path& directory, const sink::RgbaImage& source) {
    sink::RgbaImage decoded;
    require(sink::saveImage((directory / "transparent.png").wstring(), source), "save transparent PNG");
    require(sink::loadImage((directory / "transparent.png").wstring(), decoded), "decode transparent PNG");
    check(decoded.width == source.width && decoded.height == source.height && decoded.pixels == source.pixels,
        "PNG_straight_RGBA_exact_including_hidden_RGB");
    auto consumer = premultipliedRoundTrip(decoded);
    // WIC on the reproduction host truncates 255/4 to 63; allow a consumer
    // rounding to nearest (64) as well. Either loses the original dark RGB.
    check(consumer.pixels != source.pixels && consumer.pixels[0] >= 63 && consumer.pixels[0] <= 64 &&
        consumer.pixels[1] == consumer.pixels[0] && consumer.pixels[2] == 0,
        "reproduce_low_alpha_quantization_in_WIC_consumer");

    const auto expected = opaque(source);
    const auto originalPixels = source.pixels;
    // This is the SDR screenshot call, separately from generic image export.
    require(sink::saveImage((directory / "screenshot.png").wstring(), source, false, sink::ImageAlpha::Opaque), "save screenshot PNG");
    require(sink::loadImage((directory / "screenshot.png").wstring(), decoded), "decode screenshot PNG");
    check(decoded.width == expected.width && decoded.height == expected.height && decoded.pixels == expected.pixels,
        "SDR_PNG_opaque_and_RGB_exact_all_dark_patterns");
    check(premultipliedRoundTrip(decoded).pixels == expected.pixels, "SDR_PNG_consumer_roundtrip_exact");
    dump(directory / "screenshot.rgba", decoded);
    check(source.pixels == originalPixels, "save_does_not_mutate_input");

    sink::RgbaImage jpegAlpha, jpegOpaque;
    require(sink::saveImage((directory / "alpha.jpg").wstring(), source, true) &&
        sink::saveImage((directory / "opaque.jpg").wstring(), expected, true, sink::ImageAlpha::Opaque), "save JPEG");
    require(sink::loadImage((directory / "alpha.jpg").wstring(), jpegAlpha) &&
        sink::loadImage((directory / "opaque.jpg").wstring(), jpegOpaque), "decode JPEG");
    check(jpegAlpha.width == source.width && jpegAlpha.height == source.height && jpegAlpha.pixels == jpegOpaque.pixels &&
        opaque(jpegAlpha).pixels == jpegAlpha.pixels, "JPEG_independent_of_source_alpha");
    check(!sink::saveImage((directory / "screenshot.png").wstring(), source), "refuse_existing_destination");
}
void bands(const fs::path& directory) {
    // Cross the 8 MiB staging boundary in both the 3- and 4-channel writers.
    sink::RgbaImage source{1024, 3073, std::vector<uint8_t>(size_t(1024) * 3073 * 4)};
    for (unsigned y = 0; y < source.height; ++y) for (unsigned x = 0; x < source.width; ++x) {
        auto* p = source.pixels.data() + (size_t(y) * source.width + x) * 4;
        p[0] = uint8_t((x + y) % 32); p[1] = uint8_t(y % 32); p[2] = uint8_t(x % 32); p[3] = uint8_t(y % 256);
    }
    for (const auto mode : {sink::ImageAlpha::Preserve, sink::ImageAlpha::Opaque}) {
        const auto path = directory / (mode == sink::ImageAlpha::Opaque ? "banded-opaque.png" : "banded-alpha.png");
        sink::RgbaImage decoded;
        require(sink::saveImage(path.wstring(), source, false, mode) && sink::loadImage(path.wstring(), decoded), "banded PNG roundtrip");
        check(decoded.width == source.width && decoded.height == source.height &&
            decoded.pixels == (mode == sink::ImageAlpha::Opaque ? opaque(source).pixels : source.pixels),
            mode == sink::ImageAlpha::Opaque ? "multi_band_opaque_PNG_exact" : "multi_band_transparent_PNG_exact");
    }
}
ComPtr<ID3D12Resource> upload(gfx::D3D12DeviceContext& ctx, gfx::CommandSlotRing& ring,
    unsigned width, unsigned height, DXGI_FORMAT format, const void* data, unsigned bytesPerPixel) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height;
    desc.DepthOrArraySize = desc.MipLevels = 1; desc.Format = format; desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture, buffer;
    require(SUCCEEDED(ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))), "GPU texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 total = 0;
    ctx.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    require(SUCCEEDED(ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer))), "GPU upload buffer");
    void* mapped = nullptr; D3D12_RANGE empty{};
    require(SUCCEEDED(buffer->Map(0, &empty, &mapped)), "GPU upload map");
    for (unsigned y = 0; y < height; ++y) std::memcpy(static_cast<uint8_t*>(mapped) + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch,
        static_cast<const uint8_t*>(data) + size_t(y) * width * bytesPerPixel, size_t(width) * bytesPerPixel);
    buffer->Unmap(0, nullptr);
    Status status; uint32_t slot = 0; auto* list = ring.acquireNext(slot, status);
    require(list != nullptr, "GPU command list");
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = buffer.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprint;
    dst.pResource = texture.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {texture.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON};
    list->ResourceBarrier(1, &barrier);
    require(ring.submitAndSignal(slot) && ring.waitIdle(), "GPU upload completion");
    return texture;
}
void scaleBlit(gfx::D3D12DeviceContext& ctx, gfx::CommandSlotRing& ring) {
    // The graph's final output shader preserves alpha while encoding RGB.
    // Exercise that actual shader, rather than assuming low alpha premultiplies
    // the video texture's RGB. No vendor enhancement runtime is involved.
    constexpr unsigned w = 256, h = 16;
    std::vector<uint16_t> linear(w * h * 4);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const float coded = float(x) / 255;
        const float value = coded <= 0.04045f ? coded / 12.92f : std::pow((coded + 0.055f) / 1.055f, 2.4f);
        for (unsigned c = 0; c < 3; ++c) linear[(y * w + x) * 4 + c] = DirectX::PackedVector::XMConvertFloatToHalf(value);
        linear[(y * w + x) * 4 + 3] = DirectX::PackedVector::XMConvertFloatToHalf(float(y) / 255);
    }
    auto input = upload(ctx, ring, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, linear.data(), 8);
    auto output = pipeline::makeTexture(ctx.device(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    require(bool(output), "ScaleBlit output");
    std::ifstream shader(fs::path(reinterpret_cast<const char8_t*>(VEYRA_TEST_SCALE_BLIT)), std::ios::binary);
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(shader), std::istreambuf_iterator<char>()};
    require(!bytes.empty(), "ScaleBlit shader file");
    pipeline::ComputePass pass;
    require(pass.create(ctx.device(), bytes, 2, 1, 1, 8), "ScaleBlit compute pass");
    pipeline::DescriptorStager stager;
    require(stager.initialize(ctx.device(), 1), "ScaleBlit descriptor staging");
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
    stager.stageSrv(input.Get(), &srv, pass.heap.Get(), 0);
    auto uav = pass.heap->GetCPUDescriptorHandleForHeapStart(); uav.ptr += pass.increment;
    pipeline::makeUav(ctx.device(), output.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, uav);
    Status status; uint32_t slot = 0; auto* list = ring.acquireNext(slot, status);
    require(list != nullptr, "ScaleBlit list");
    pipeline::StateTracker states;
    states.transition(list, input.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    states.transition(list, output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const float constants[] = {std::bit_cast<float>(w), std::bit_cast<float>(h), std::bit_cast<float>(w), std::bit_cast<float>(h), 1, 0, 0, 0};
    const auto handle = pass.heap->GetGPUDescriptorHandleForHeapStart();
    pass.bind(list, constants, handle.ptr, handle.ptr + pass.increment);
    list->Dispatch(w / 16, h / 16, 1);
    states.uavBarrier(list, output.Get());
    states.transition(list, output.Get(), D3D12_RESOURCE_STATE_COMMON);
    require(ring.submitAndSignal(slot) && ring.waitIdle(), "ScaleBlit completion");
    sink::RgbaImage actual;
    require(sink::readRgba8(ctx, ring, output.Get(), actual), "ScaleBlit readback");
    bool rgb = true, alpha = true;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        for (unsigned c = 0; c < 3; ++c) rgb &= actual.pixels[(y * w + x) * 4 + c] == x;
        alpha &= actual.pixels[(y * w + x) * 4 + 3] == y;
    }
    check(rgb && alpha, "ScaleBlit_straight_RGB_exact_with_low_alpha_0_to_15");
}
void gpu(const fs::path& directory) {
    gfx::D3D12DeviceContext ctx; gfx::CommandSlotRing ring; Status status;
    gfx::DeviceContextDesc desc; desc.enableDebugLayer = true;
    require(ctx.initialize(desc, status) && ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 4, status), "D3D12 initialization");
    scaleBlit(ctx, ring);
    auto source = fixture(); // Unaligned rows exercise GetCopyableFootprints padding.
    source.width = 257; source.height = 255;
    source.pixels.resize(size_t(source.width) * source.height * 4);
    auto texture = upload(ctx, ring, source.width, source.height, DXGI_FORMAT_R8G8B8A8_UNORM, source.pixels.data(), 4);
    sink::RgbaImage readback;
    require(sink::readRgba8(ctx, ring, texture.Get(), readback), "GPU readRgba8");
    check(readback.width == source.width && readback.height == source.height && readback.pixels == source.pixels,
        "GPU_UNORM_readback_preserves_straight_RGBA");
    codecs(directory / "gpu", readback);

    // Exercise the existing HDR writer on both formats accepted by production.
    for (bool pq : {false, true}) {
        constexpr unsigned w = 33, h = 16;
        std::vector<uint16_t> half(w * h * 4);
        std::vector<uint32_t> packed(w * h);
        for (unsigned i = 0; i < w * h; ++i) {
            const float values[] = {-0.125f, 0, 0.125f, 1, 4, 12.5f};
            for (unsigned c = 0; c < 4; ++c) half[i * 4 + c] = DirectX::PackedVector::XMConvertFloatToHalf(c == 3 ? 1 : values[(i + c) % 6]);
            const unsigned code = (i % 4) * 256;
            packed[i] = code | (code << 10) | (code << 20) | (3u << 30);
        }
        auto hdr = upload(ctx, ring, w, h, pq ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT,
            pq ? static_cast<const void*>(packed.data()) : half.data(), pq ? 4 : 8);
        const auto path = directory / (pq ? "hdr-pq.jxr" : "hdr-scrgb.jxr");
        require(sink::saveHdrScreenshot(path.wstring(), ctx, ring, hdr.Get()), "HDR save JXR");
        ComPtr<IWICImagingFactory> factory; ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
        require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)), "HDR decode JXR");
        WICPixelFormatGUID format{}; UINT width = 0, height = 0;
        require(SUCCEEDED(frame->GetPixelFormat(&format)) && SUCCEEDED(frame->GetSize(&width, &height)), "HDR format");
        std::vector<uint16_t> decoded(half.size());
        require(SUCCEEDED(frame->CopyPixels(nullptr, w * 8, UINT(decoded.size() * 2), reinterpret_cast<BYTE*>(decoded.data()))), "HDR pixels");
        bool ok = format == GUID_WICPixelFormat64bppRGBAHalf && width == w && height == h;
        double maxError = 0;
        for (unsigned i = 0; i < w * h; ++i) for (unsigned c = 0; c < 4; ++c) {
            double expected = c == 3 ? 1 : DirectX::PackedVector::XMConvertHalfToFloat(half[i * 4 + c]);
            if (pq && c != 3) {
                const double p = std::pow(double(packed[i] & 1023) / 1023, 32.0 / 2523);
                expected = 125 * std::pow(std::max(p - 3424.0 / 4096, 0.0) / (2413.0 / 128 - 2392.0 / 128 * p), 16384.0 / 2610);
            }
            const double actual = DirectX::PackedVector::XMConvertHalfToFloat(decoded[i * 4 + c]);
            maxError = std::max(maxError, std::abs(actual - expected));
            ok = ok && std::isfinite(actual) && std::abs(actual - expected) <= 0.002 * std::max(1.0, std::abs(expected));
        }
        std::cout << "HDR_max_abs_error=" << maxError << std::endl;
        check(ok, pq ? "HDR_PQ_to_FP16_JXR" : "HDR_scRGB_FP16_JXR_negative_and_above_one");
    }
    require(ring.drainQueue(), "GPU drain");
    ComPtr<ID3D12InfoQueue> info;
    require(SUCCEEDED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info))), "D3D12 info queue");
    unsigned errors = 0;
    for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
        SIZE_T size = 0; info->GetMessage(i, nullptr, &size);
        std::vector<uint8_t> storage(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        require(SUCCEEDED(info->GetMessage(i, message, &size)), "D3D12 debug message");
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
            std::cerr << message->pDescription << std::endl; ++errors;
        }
    }
    check(errors == 0, "D3D12_debug_layer_no_errors");
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    const bool inspect = argc == 4 && std::wstring_view(argv[1]) == L"--inspect";
    fs::path directory = inspect ? argv[3] : argv[1];
    if (!inspect) directory /= "run-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    fs::create_directories(directory / "gpu");
    std::cout << "results=" << directory.filename().string() << std::endl;
    Logger::instance().openFile((directory / "test.log").wstring());
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    try {
        if (inspect) {
            sink::RgbaImage image; require(sink::loadImage(argv[2], image), "inspect loadImage");
            dump(directory / "straight.rgba", image);
            dump(directory / "premultiplied.rgba", convert(image, GUID_WICPixelFormat32bppRGBA, GUID_WICPixelFormat32bppPRGBA));
            dump(directory / "consumer.rgba", premultipliedRoundTrip(image));
            require(sink::saveImage((directory / "opaque.png").wstring(), image, false, sink::ImageAlpha::Opaque), "inspect opaque save");
            sink::RgbaImage fixed; require(sink::loadImage((directory / "opaque.png").wstring(), fixed), "inspect opaque decode");
            check(fixed.pixels == opaque(image).pixels, "original_reexport_RGB_exact_and_opaque");
            check(premultipliedRoundTrip(fixed).pixels == fixed.pixels, "original_reexport_consumer_exact");
            std::cout << "INSPECT width=" << image.width << " height=" << image.height << std::endl;
        } else {
            codecs(directory, fixture());
            bands(directory);
            gpu(directory);
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR " << e.what() << std::endl; ++failures;
    }
    CoUninitialize();
    std::cout << "failures=" << failures << std::endl;
    return failures ? 1 : 0;
}
