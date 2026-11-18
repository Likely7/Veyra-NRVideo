// Adapted for Veyra from OBS Studio 7e3aa99811798255bd1a13a6da7260ada76a4d95.
// GPL-2.0-or-later; original attribution and exact file hashes: ../UPSTREAM.json.
#pragma once

#include <windows.h>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>

#ifdef OBS_LEGACY
#include "../shared-memory-queue.h"
#include "../tiny-nv12-scale.h"
#include "../../../deps/libdshowcapture/src/source/output-filter.hpp"
#include "../../../deps/libdshowcapture/src/source/dshow-formats.hpp"
#include "../../../libobs/util/windows/WinHandle.hpp"
#include "../../../libobs/util/threading-windows.h"
#else
#include <shared-memory-queue.h>
#include <tiny-nv12-scale.h>
#include <source/output-filter.hpp>
#include <source/dshow-formats.hpp>
#include <util/windows/WinHandle.hpp>
#include <util/threading-windows.h>
#endif

typedef struct {
	int cx;
	int cy;
	nv12_scale_t scaler;
	const uint8_t *source_data;
	uint8_t *scaled_data;
} placeholder_t;

class VCamFilter : public DShow::OutputFilter {
	std::thread th;
	std::mutex frame_mutex;
	std::atomic<uint64_t> stream_origin{0};
	std::atomic<bool> running{false};
	int64_t last_sample_time = -1;

	video_queue_t *vq = nullptr;
	int queue_mode = 0;
	bool in_obs = false;
	enum queue_state prev_state = SHARED_QUEUE_STATE_INVALID;
	placeholder_t placeholder;
	uint32_t obs_cx = 0;
	uint32_t obs_cy = 0;
	uint64_t obs_interval = 0;
	uint32_t filter_cx = 0;
	uint32_t filter_cy = 0;
	DShow::VideoFormat format;
	WinHandle thread_start;
	WinHandle thread_stop;
	volatile bool active = false;

	nv12_scale_t scaler = {};

	inline bool stopped() const { return WaitForSingleObject(thread_stop, 0) != WAIT_TIMEOUT; }

	inline uint64_t GetTime();

	void Thread();
	/* Veyra paced delivery (virtualcam-filter.cpp, 2026-11-17). Clock: video_queue_now_100ns. */
	uint64_t last_sequence = 0, latency = 0, next_grid = 0, next_repeat = 0, next_placeholder = 0, next_announce = 0;
	uint64_t adapted_sequence = 0, trace_looked = 0;
	int64_t window_peak = 0;
	uint32_t window_frames = 0;
	/* Inside OBS: libobs's own render clock (public API, looked up at run time). */
	uint64_t (*obs_frame_time)(void) = nullptr;
	uint64_t (*obs_frame_interval)(void) = nullptr;
	enum queue_state Refresh();
	void ResetPacing();
	uint64_t Pace();
	void Adapt(uint64_t sequence, uint64_t ts, uint64_t written, uint64_t interval);
	uint64_t Rate();
	uint64_t AlignSlot(uint64_t interval, uint64_t *target);
	uint64_t Align(uint64_t earliest, uint64_t interval);
	bool Deliver(uint64_t sequence, uint64_t due = 0);
	void ShowDefaultFrame(uint8_t *ptr);
	void UpdatePlaceholder(void);
	const int GetOutputBufferSize(void);

protected:
	const wchar_t *FilterName() const override;

public:
	VCamFilter();
	~VCamFilter() override;

	STDMETHODIMP Pause() override;
	STDMETHODIMP GetState(DWORD timeout, FILTER_STATE *result) override;
	STDMETHODIMP Run(REFERENCE_TIME tStart) override;
	STDMETHODIMP Stop() override;
};
