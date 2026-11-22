#include "veyra/media/FfmpegLog.h"

#include "veyra/Log.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>

extern "C" {
#include <libavutil/log.h>
}

namespace veyra::media::ffmpeglog {
namespace {

enum class Mode : int { None, App, Worker };
std::atomic<Mode> g_mode{Mode::None};

// Worker mode: unchanged from the original ExportStreams callback.
void workerLog(void* context, int level, const char* format, va_list args)
{
    if (level > av_log_get_level()) return;
    char line[4096]{};
    int prefix = 1;
    av_log_format_line2(context, level, format, args, line, sizeof(line), &prefix);
    if (level <= AV_LOG_ERROR) log::error("ffmpeg-export", line);
    else if (level <= AV_LOG_WARNING) log::warn("ffmpeg-export", line);
    else log::info("ffmpeg-export", line);
}

constexpr size_t kLineBytes = 1024;       // formatted message; longer lines are truncated
constexpr int kMaxLinesPerSecond = 20;
constexpr auto kWindow = std::chrono::seconds(1);

struct Limiter {
    std::mutex mutex;
    std::chrono::steady_clock::time_point windowStart{};
    int emitted = 0;
    unsigned suppressed = 0;
    unsigned repeats = 0;
    std::string last;
};
Limiter& limiter()
{
    static Limiter value; // intentionally never destroyed before late decoder threads exit
    return value;
}

struct Output {
    int level;
    std::string text;
};

const char* itemName(void* context)
{
    if (!context) return nullptr;
    const AVClass* cls = *static_cast<AVClass**>(context);
    if (!cls) return nullptr;
    if (cls->item_name) {
        if (const char* name = cls->item_name(context); name && *name) return name;
    }
    return cls->class_name;
}

void appLog(void* context, int level, const char* format, va_list args)
{
    if (level > AV_LOG_WARNING || level > av_log_get_level()) return;

    // print_prefix=0: no "[item @ 0xptr]" prefix, so identical messages compare
    // equal; the item name is added below without the pointer.
    char text[kLineBytes]{};
    int prefix = 0;
    const int needed = av_log_format_line2(context, level, format, args, text, sizeof(text), &prefix);
    if (needed < 0) return;
    size_t length = std::strlen(text);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' || text[length - 1] == ' ')) text[--length] = 0;
    if (length == 0) return;
    if (size_t(needed) >= sizeof(text)) std::memcpy(text + sizeof(text) - 4, "...", 4);

    std::string line;
    if (const char* item = itemName(context)) {
        line.append(item).append(": ");
    }
    line.append(text);

    std::array<Output, 3> out;
    size_t count = 0;
    {
        auto& state = limiter();
        std::lock_guard lock(state.mutex);
        const auto now = std::chrono::steady_clock::now();
        if (now - state.windowStart >= kWindow) {
            state.windowStart = now;
            state.emitted = 0;
            if (state.suppressed > 0) {
                out[count++] = {AV_LOG_WARNING, "suppressed " + std::to_string(state.suppressed) + " ffmpeg log lines (rate limit)"};
                state.suppressed = 0;
            }
        }
        if (!state.last.empty() && line == state.last) {
            ++state.repeats;
        } else {
            if (state.repeats > 0) {
                out[count++] = {AV_LOG_WARNING, "last ffmpeg line repeated " + std::to_string(state.repeats) + " more times"};
                state.repeats = 0;
            }
            if (state.emitted >= kMaxLinesPerSecond) {
                ++state.suppressed;
                state.last.clear();
            } else {
                ++state.emitted;
                state.last = line;
                out[count++] = {level, std::move(line)};
            }
        }
    }
    // The logger takes its own lock; call it outside ours.
    for (size_t i = 0; i < count; ++i) {
        if (out[i].level <= AV_LOG_ERROR) log::error("ffmpeg", out[i].text);
        else log::warn("ffmpeg", out[i].text);
    }
}

void callback(void* context, int level, const char* format, va_list args)
{
    if (g_mode.load(std::memory_order_relaxed) == Mode::Worker) workerLog(context, level, format, args);
    else appLog(context, level, format, args);
}

} // namespace

void installApp()
{
    Mode expected = Mode::None;
    if (!g_mode.compare_exchange_strong(expected, Mode::App)) return; // already installed (either mode)
    av_log_set_level(AV_LOG_WARNING);
    av_log_set_callback(callback);
}

void installWorker(bool detailed)
{
    g_mode.store(Mode::Worker);
    av_log_set_callback(callback);
    av_log_set_level(detailed ? AV_LOG_DEBUG : AV_LOG_WARNING);
}

} // namespace veyra::media::ffmpeglog
