#pragma once

namespace veyra::media::ffmpeglog {

// FFmpeg's av_log callback is process-global, so every consumer shares this one
// callback; the mode only selects how a message is routed into veyra::log.
//
// installApp(): idempotent. Forwards AV_LOG_WARNING and more severe messages to
// the app logger as component "ffmpeg", "<item>: <message>", with a bounded
// line buffer, a per-second line budget (plus a "suppressed N" summary) and
// collapsing of identical consecutive lines. Never overrides worker mode.
void installApp();

// Export worker process: verbatim forwarding as component "ffmpeg-export" with
// the level chosen by the caller (this is the historical ExportStreams behavior).
void installWorker(bool detailed);

} // namespace veyra::media::ffmpeglog
