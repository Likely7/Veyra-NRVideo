#pragma once

#include "veyra/sink/BitstreamAudio.h"

#include <functional>
#include <span>

namespace veyra::sink {

struct HdmiBitstreamSettings {
    bool enabled = false;
    std::wstring endpointId; // Empty follows the default render endpoint.
    bool operator==(const HdmiBitstreamSettings&) const = default;
};

enum class HdmiBitstreamStatus { Disabled, Connecting, Active, Unsupported, Error };

struct HdmiBitstreamState {
    HdmiBitstreamStatus status = HdmiBitstreamStatus::Disabled;
    BitstreamKind codec = BitstreamKind::None;
    unsigned rate = 0;
    std::wstring endpoint;
    long error = 0;
    bool operator==(const HdmiBitstreamState&) const = default;
};

struct HdmiBitstreamEndpoint {
    std::wstring id, name;
    bool ac3 = false, dts = false; // Exclusive-mode support at the probed rates.
};

// Pc and Pd are the unmodified IEC 61937 header words. Payload includes the
// final carrier pad byte for odd lengths, in the original little-endian words.
unsigned iecBurstFrames(uint16_t pc);
std::vector<uint8_t> makeIecBurst(uint16_t pc, uint16_t pd, std::span<const uint8_t> payload);
void writeAc3PauseBursts(std::span<uint8_t> gap);

class HdmiBitstreamOutput {
public:
    using ReserveEndpoint = std::function<void(const std::wstring&)>;
    explicit HdmiBitstreamOutput(ReserveEndpoint reserve = {});
    ~HdmiBitstreamOutput();
    void configure(HdmiBitstreamSettings);
    bool enabled() const;
    HdmiBitstreamState state() const;
    void submit(uint16_t pc, uint16_t pd, unsigned rate, std::span<const uint8_t> payload);
    void reset();
    static std::vector<HdmiBitstreamEndpoint> endpoints(unsigned rate = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::sink
