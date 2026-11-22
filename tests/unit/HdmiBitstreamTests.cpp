#include "veyra/sink/HdmiBitstreamOutput.h"
#include "../../src/sink/HdmiCarrierQueue.h"

#include <Windows.h>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace veyra::sink;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void framing(std::string_view test) {
    // Includes an odd payload length and the pad byte in its original word.
    constexpr std::array<uint8_t, 6> words{0x77, 0x0b, 0xa1, 0x02, 0, 0x35};
    constexpr uint16_t bits = 40;
    const auto verify = [&](uint16_t pc, size_t frames) {
        const auto burst = makeIecBurst(pc, bits, words);
        require(iecBurstFrames(pc) == frames && burst.size() == frames * 4, "carrier frame period");
        const std::array<uint8_t, 8> header{0x72, 0xf8, 0x1f, 0x4e,
                                          uint8_t(pc), uint8_t(pc >> 8), uint8_t(bits), 0};
        require(std::ranges::equal(header, std::span(burst).first(8)), "original header words");
        require(std::ranges::equal(words, std::span(burst).subspan(8, words.size())), "payload preservation");
        require(std::ranges::all_of(std::span(burst).subspan(8 + words.size()),
                                    [](auto byte) { return byte == 0; }), "padding");
        require(makeIecBurst(pc, bits, std::span(words).first(5)).empty(), "reject missing carrier pad");
    };
    if (test == "ac3") verify(0x501, 1536); // Preserve AC-3 bsmod in Pc.
    if (test == "dts") {
        verify(0x0b, 512);
        verify(0x0c, 1024);
        verify(0x0d, 2048);
    }
    require(makeIecBurst(0x81, bits, words).empty(), "reject error-marked burst");
    require(makeIecBurst(0x15, bits, words).empty(), "reject E-AC-3");
}

void underrun() {
    std::vector<uint8_t> gap(1536 * 4, 0xff);
    writeAc3PauseBursts(gap);
    const std::array<uint8_t, 12> first{0x72, 0xf8, 0x1f, 0x4e, 3, 0, 32, 0, 0, 6, 0, 0};
    const std::array<uint8_t, 12> subsequent{0x72, 0xf8, 0x1f, 0x4e, 3, 0, 32, 0, 0, 0, 0, 0};
    require(std::ranges::equal(std::span(gap).first(12), first), "AC-3 pause announces the gap");
    for (size_t n = 12; n < gap.size(); n += 12)
        require(std::ranges::equal(std::span(gap).subspan(n, 12), subsequent), "repeat pause every three carrier frames");

    detail::HdmiCarrierQueue queue;
    detail::HdmiCarrierQueue::Cursor cursor;
    std::array<uint8_t, 8> packet{};
    auto fill = [&](std::array<uint8_t, 8> expected) {
        queue.fill(cursor, packet, 16);
        require(packet == expected, "exact packet, padding, and re-prime behavior");
    };
    queue.push(std::vector<uint8_t>(12, 1), 24);
    fill({}); // Waiting for enough data to prime.
    queue.push(std::vector<uint8_t>(12, 2), 24);
    fill({1,1,1,1,1,1,1,1});
    require(queue.push(std::vector<uint8_t>(12, 3), 24) == 0, "fits without dropping a burst");
    require(queue.push(std::vector<uint8_t>(12, 4), 24) == 1, "reports one dropped burst");
    require(queue.bytes() == 24, "bounded queue drops only a whole queued burst");
    fill({1,1,1,1,3,3,3,3}); // First burst survives producer overflow.
    fill({3,3,3,3,3,3,3,3});
    fill({4,4,4,4,4,4,4,4});
    fill({4,4,4,4,0,0,0,0});
    fill({});
    queue.push(std::vector<uint8_t>(12, 5), 24);
    fill({});
    queue.push(std::vector<uint8_t>(12, 6), 24);
    fill({5,5,5,5,5,5,5,5});
}

void endpoint() {
    for (const auto& device : HdmiBitstreamOutput::endpoints()) {
        require(!device.id.empty() && !device.name.empty(), "bitstream endpoint identity");
        require(device.ac3 || device.dts, "only endpoints accepting an encoded format are listed");
        std::wcout << device.name << L"  AC-3=" << device.ac3 << L" DTS=" << device.dts << L'\n';
    }
}

void worker(bool formatChange) {
    HdmiBitstreamOutput output;
    constexpr std::array<uint8_t, 4> payload{1, 2, 3, 4};
    output.submit(1, 32, 48000, payload);
    require(output.state().status == HdmiBitstreamStatus::Disabled, "default disabled");
    output.configure({true, {}});
    require(output.state().status == HdmiBitstreamStatus::Connecting, "empty selection follows Windows default");
    const std::wstring missing = L"{0.0.0.00000000}.{e4c6d785-02b3-41d7-ab02-fce86693bd9e}";
    output.configure({true, missing});
    output.submit(0x15, 4, 48000, payload);
    require(output.state().status == HdmiBitstreamStatus::Unsupported, "unsupported codec is detected");
    const auto waitFor = [&](BitstreamKind kind, unsigned type, unsigned rate) {
        const auto end = std::chrono::steady_clock::now() + 2s;
        do {
            output.submit(uint16_t(type), 32, rate, payload);
            if (output.state().status == HdmiBitstreamStatus::Error) break;
            std::this_thread::sleep_for(10ms);
        } while (std::chrono::steady_clock::now() < end);
        const auto state = output.state();
        require(state.codec == kind && state.rate == rate, "current detected codec and carrier rate");
        require(state.status == HdmiBitstreamStatus::Error &&
                state.error == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED),
                "selected missing endpoint cannot fall back to another device");
    };
    waitFor(BitstreamKind::Ac3, 1, 48000);
    if (formatChange) waitFor(BitstreamKind::Dts, 0x0b, 44100);
    output.reset();
    require(output.state().status == HdmiBitstreamStatus::Connecting &&
            output.state().codec == BitstreamKind::None, "reset clears previous stream");
    output.configure({});
    require(output.state().status == HdmiBitstreamStatus::Disabled, "disable invalidates worker state");
}
}

int main(int argc, char* argv[]) {
    try {
        const std::string_view test = argc == 2 ? argv[1] : "";
        if (test == "ac3" || test == "dts") framing(test);
        else if (test == "underrun") underrun();
        else if (test == "endpoint") endpoint();
        else if (test == "format-change" || test == "missing-device") worker(test == "format-change");
        else throw std::runtime_error("specify ac3, dts, underrun, endpoint, format-change, or missing-device");
        std::cout << "PASS " << test << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
