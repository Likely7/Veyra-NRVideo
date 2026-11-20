// Adapted for Veyra from OBS Studio 7e3aa99811798255bd1a13a6da7260ada76a4d95.
// GPL-2.0-or-later; original attribution and exact file hashes: ../UPSTREAM.json.
#include "virtualcam-filter.hpp"
#include "sleepto.h"

#include <shlobj_core.h>
#include <strsafe.h>
#include <inttypes.h>
#include <algorithm>

using namespace DShow;

extern bool initialize_placeholder();
extern const uint8_t *get_placeholder_ptr();
extern const bool get_placeholder_size(int *out_cx, int *out_cy);
extern volatile long locks;

/* Test only (VEYRA_TEST_VIRTUALCAM_FILTER_TRACE=<csv>): per delivered frame, when it
   was due, when this thread looked, copied and handed it on. */
static FILE *filter_trace()
{
	static FILE *file = [] {
		wchar_t path[MAX_PATH];
		const DWORD n = GetEnvironmentVariableW(L"VEYRA_TEST_VIRTUALCAM_FILTER_TRACE", path, MAX_PATH);
		FILE *f = nullptr;
		if (n && n < MAX_PATH && !_wfopen_s(&f, path, L"w") && f)
			fprintf(f, "sequence,due,looked,copied,delivered\n");
		return f;
	}();
	return file;
}

/* ========================================================================= */

VCamFilter::VCamFilter() : OutputFilter()
{
	os_atomic_inc_long(&locks);
	AddRef();

	thread_start = CreateEvent(nullptr, true, false, nullptr);
	thread_stop = CreateEvent(nullptr, true, false, nullptr);

	format = VideoFormat::NV12;

	placeholder.scaled_data = nullptr;

	/* ---------------------------------------- */
	/* detect if this filter is within obs      */

	wchar_t file[MAX_PATH];
	if (!GetModuleFileNameW(nullptr, file, MAX_PATH)) {
		file[0] = 0;
	}

#ifdef _WIN64
	const wchar_t *obs_process = L"obs64.exe";
#else
	const wchar_t *obs_process = L"obs32.exe";
#endif

	in_obs = false; // Veyra is an independent producer; consumers retain their negotiated format.

	/* ---------------------------------------- */
	/* add last/current obs res/interval        */

	uint32_t new_obs_cx = 1920;
	uint32_t new_obs_cy = 1080;
	uint64_t new_obs_interval = 166667;

	vq = video_queue_open();
	if (vq) {
		if (video_queue_state(vq) == SHARED_QUEUE_STATE_READY) {
			video_queue_get_info(vq, &new_obs_cx, &new_obs_cy, &new_obs_interval);
		}

		/* don't keep it open until the filter actually starts */
		video_queue_close(vq);
		vq = nullptr;
	}

	if (new_obs_cx != obs_cx || new_obs_cy != obs_cy || new_obs_interval != obs_interval) {
		AddVideoFormat(VideoFormat::NV12, new_obs_cx, new_obs_cy, new_obs_interval);
		AddVideoFormat(VideoFormat::I420, new_obs_cx, new_obs_cy, new_obs_interval);
		AddVideoFormat(VideoFormat::YUY2, new_obs_cx, new_obs_cy, new_obs_interval);
		SetVideoFormat(VideoFormat::NV12, new_obs_cx, new_obs_cy, new_obs_interval);

		obs_cx = new_obs_cx;
		obs_cy = new_obs_cy;
		obs_interval = new_obs_interval;
	}

	/* ---------------------------------------- */

    // Advertise stable choices even before Veyra has opened media.
    for (auto size : {std::pair<int,int>{640,480},{1280,720},{1920,1080},{2560,1440},{3840,2160},{2160,3840}})
        for (long long interval : {333333LL,166667LL,83333LL})
            for (auto f : {VideoFormat::NV12,VideoFormat::I420,VideoFormat::YUY2})
                AddVideoFormat(f,size.first,size.second,interval);

	th = std::thread([this] { Thread(); });
}

VCamFilter::~VCamFilter()
{
	Stop();
	SetEvent(thread_stop);
	if (th.joinable()) {
		th.join();
	}
	video_queue_close(vq);

	if (placeholder.scaled_data) {
		free(placeholder.scaled_data);
	}

	os_atomic_dec_long(&locks);
}

const wchar_t *VCamFilter::FilterName() const
{
	return L"Veyra Virtual Camera";
}

STDMETHODIMP VCamFilter::Pause()
{
	HRESULT hr;
	running.store(false);
	os_atomic_set_bool(&active, false);
	ResetEvent(thread_start);
	FILTER_STATE previous;
	GetState(0, &previous);
	if (previous == State_Stopped) {
		std::lock_guard<std::mutex> guard(frame_mutex);
		stream_origin.store(GetTime());
		last_sample_time = -1;
	}

	hr = OutputFilter::Pause();
	if (FAILED(hr)) {
		return hr;
	}

	return S_OK;
}

STDMETHODIMP VCamFilter::GetState(DWORD timeout, FILTER_STATE *result)
{
	if (!result) return E_POINTER;
	const auto hr = OutputFilter::GetState(timeout, result);
	return SUCCEEDED(hr) && *result == State_Paused ? VFW_S_CANT_CUE : hr;
}

STDMETHODIMP VCamFilter::Run(REFERENCE_TIME tStart)
{
	const HRESULT hr = OutputFilter::Run(tStart);
	if (FAILED(hr)) return hr;
	// DirectShow sample times are relative to the graph's Run origin. An
	// absolute reference-clock timestamp can block clocked renderers for days.
	stream_origin.store(clock ? uint64_t(tStart) : GetTime());
	running.store(true);
	os_atomic_set_bool(&active, true);
    SetEvent(thread_start);
	return hr;
}

STDMETHODIMP VCamFilter::Stop()
{
	running.store(false);
	os_atomic_set_bool(&active, false);
    ResetEvent(thread_start);
    const HRESULT hr = OutputFilter::Stop(); // Flush releases a blocked downstream Receive.
    std::lock_guard<std::mutex> guard(frame_mutex);
    video_queue_close(vq); vq = nullptr; prev_state = SHARED_QUEUE_STATE_INVALID;
    last_sample_time = -1;
    return hr;
}

inline uint64_t VCamFilter::GetTime()
{
	if (!!clock) {
		REFERENCE_TIME rt;
		HRESULT hr = clock->GetTime(&rt);
		if (SUCCEEDED(hr)) {
			return (uint64_t)rt;
		}
	}

	return gettime_100ns();
}

void VCamFilter::Thread()
{
	HANDLE h[2] = {thread_start, thread_stop};
	DWORD ret = WaitForMultipleObjects(2, h, false, INFINITE);
	if (ret != WAIT_OBJECT_0) {
		return;
	}

	obs_cx = (uint32_t)GetCX();
	obs_cy = (uint32_t)GetCY();
	obs_interval = (uint64_t)GetInterval();
	filter_cx = obs_cx;
	filter_cy = obs_cy;

	/* ---------------------------------------- */
	/* load placeholder image                   */

	if (initialize_placeholder()) {
		placeholder.source_data = get_placeholder_ptr();
		get_placeholder_size(&placeholder.cx, &placeholder.cy);
	} else {
		placeholder.source_data = nullptr;
	}

	/* Created dynamically based on output resolution changes */
	placeholder.scaled_data = nullptr;

	nv12_scale_init(&scaler, TARGET_FORMAT_NV12, obs_cx, obs_cy, obs_cx, obs_cy);
	nv12_scale_init(&placeholder.scaler, TARGET_FORMAT_NV12, obs_cx, obs_cy, placeholder.cx, placeholder.cy);

	UpdatePlaceholder();

	/* Veyra: OBS takes the newest picture of an unbuffered source (its default for
	   this device) once per render tick; libobs, already in this process when OBS
	   is the consumer, tells where those ticks fall (see Align). */
	if (HMODULE libobs = GetModuleHandleW(L"obs.dll")) {
		obs_frame_time = reinterpret_cast<uint64_t (*)(void)>(GetProcAddress(libobs, "obs_get_video_frame_time"));
		obs_frame_interval = reinterpret_cast<uint64_t (*)(void)>(GetProcAddress(libobs, "obs_get_frame_interval_ns"));
		if (!obs_frame_time || !obs_frame_interval)
			obs_frame_time = obs_frame_interval = nullptr;
	}

	/* Veyra: wait on a high-resolution timer between frames. Sleep() follows the
	   consumer process's timer resolution, which can be 15.6 ms. This thread only
	   waits and copies one frame per tick; above the consumer's busy threads it
	   leaves on time. */
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	while (!stopped()) {
		if (!os_atomic_load_bool(&active)) {
			/* Veyra: a paused graph still lets go of a queue its producer has closed,
			   as Refresh does while running. The producer recreates the queue under the
			   same name on a size, rate or source change and cannot while any consumer
			   maps the old one. A live queue stays open, so Run continues in order.
			   Not in Pause(): Deliver may hold frame_mutex inside a downstream Receive. */
			{
				std::lock_guard<std::mutex> guard(frame_mutex);
				if (vq && video_queue_state(vq) == SHARED_QUEUE_STATE_STOPPING) {
					video_queue_close(vq);
					vq = nullptr;
					prev_state = SHARED_QUEUE_STATE_INVALID;
					ResetPacing();
				}
			}
			const DWORD woke = WaitForMultipleObjects(2, h, false, 100);
			if (woke != WAIT_OBJECT_0 && woke != WAIT_TIMEOUT)
				break;
			continue;
		}
		const uint64_t wake = Pace();
		const uint64_t now = video_queue_now_100ns();
		if (wake <= now)
			continue;
		if (timer) {
			LARGE_INTEGER due;
			due.QuadPart = -(LONGLONG)(wake - now);
			if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
				HANDLE w[2] = {timer, thread_stop};
				WaitForMultipleObjects(2, w, false, INFINITE);
				continue;
			}
		}
		Sleep((DWORD)std::max<uint64_t>(1, (wake - now) / 10000));
	}
	if (timer)
		CloseHandle(timer);
}

/* The producer's queue and the consumer's format: opened, followed, scalers kept
   in step (the first half of upstream's Frame()). */
enum queue_state VCamFilter::Refresh()
{
	uint32_t new_obs_cx = obs_cx;
	uint32_t new_obs_cy = obs_cy;
	uint64_t new_obs_interval = obs_interval;

	/* cx, cy and interval are the resolution and frame rate of the
	   virtual camera _source_, ie OBS' output. Do not confuse cx / cy
	   with GetCX() / GetCY() / GetInterval() which return the virtualcam
	   filter output! */

	if (!vq) {
		vq = video_queue_open();
        prev_state = SHARED_QUEUE_STATE_INVALID;
		ResetPacing(); /* a new producer session numbers its frames from 1 again */
	}

	enum queue_state state = video_queue_state(vq);
	if (state != prev_state) {
		if (state == SHARED_QUEUE_STATE_READY) {
			/* The virtualcam output from OBS has started, get
			   the actual cx / cy of the data stream */
			video_queue_get_info(vq, &new_obs_cx, &new_obs_cy, &new_obs_interval);
		} else if (state == SHARED_QUEUE_STATE_STOPPING) {
			video_queue_close(vq);
			vq = nullptr;
			ResetPacing();
		}

		prev_state = state;
	}

	uint32_t new_filter_cx = (uint32_t)GetCX();
	uint32_t new_filter_cy = (uint32_t)GetCY();

	if (state != SHARED_QUEUE_STATE_READY) {
		/* Virtualcam output not yet started, assume it's
		   the same resolution as the filter output */
		new_obs_cx = new_filter_cx;
		new_obs_cy = new_filter_cy;
		new_obs_interval = GetInterval();
	}

	if (new_obs_cx != obs_cx || new_obs_cy != obs_cy || new_obs_interval != obs_interval) {
		/* The res / FPS of the video coming from OBS has
		   changed, update parameters as needed */
		if (in_obs) {
			/* If the vcam is being used inside obs, adjust
			   the format we present to match */
			SetVideoFormat(GetVideoFormat(), new_obs_cx, new_obs_cy, new_obs_interval);

			/* Update the new filter size immediately since we
			   know it just changed above */
			new_filter_cx = new_obs_cx;
			new_filter_cy = new_obs_cy;
		}

		/* Re-initialize the main scaler to use the new resolution */
		nv12_scale_init(&scaler, scaler.format, new_filter_cx, new_filter_cy, new_obs_cx, new_obs_cy);

		obs_cx = new_obs_cx;
		obs_cy = new_obs_cy;
		obs_interval = new_obs_interval;
		filter_cx = new_filter_cx;
		filter_cy = new_filter_cy;

		UpdatePlaceholder();

	} else if (new_filter_cx != filter_cx || new_filter_cy != filter_cy) {
		filter_cx = new_filter_cx;
		filter_cy = new_filter_cy;

		/* Re-initialize the main scaler to use the new resolution */
		nv12_scale_init(&scaler, scaler.format, new_filter_cx, new_filter_cy, new_obs_cx, new_obs_cy);

		UpdatePlaceholder();
	}

	VideoFormat current_format = GetVideoFormat();

	if (current_format != format) {
		/* The output format changed, update the scalers */
		if (current_format == VideoFormat::I420) {
			scaler.format = placeholder.scaler.format = TARGET_FORMAT_I420;
		} else if (current_format == VideoFormat::YUY2) {
			scaler.format = placeholder.scaler.format = TARGET_FORMAT_YUY2;
		} else {
			scaler.format = placeholder.scaler.format = TARGET_FORMAT_NV12;
		}

		format = current_format;

		UpdatePlaceholder();
	}

	return state;
}

void VCamFilter::ResetPacing()
{
	last_sequence = latency = next_grid = next_repeat = next_announce = adapted_sequence = 0;
	window_peak = 0;
	window_frames = 0;
}

/* Veyra (2026-11-17): frames leave in write order, each once, at their place on
   the producer's camera grid plus a small adaptive delay. Upstream sampled "the
   newest frame" on a clock of its own; against Veyra's frame timing that showed
   repeats and skips, worst above 60 fps (3X/4X frame generation). Returns when to
   look again, on the video_queue_now_100ns clock. */
uint64_t VCamFilter::Pace()
{
	std::lock_guard<std::mutex> guard(frame_mutex);
	const uint64_t now = video_queue_now_100ns();
	trace_looked = now;
	if (!running.load()) /* before Rate(): no calls into libobs outside a running graph */
		return now + std::max<uint64_t>(10000, (uint64_t)GetInterval());
	const uint64_t interval = Rate();
	if (GetTime() < stream_origin.load())
		return now + 10000; // Graph Run may deliberately start in the future.

	const enum queue_state state = Refresh();
	if (state != SHARED_QUEUE_STATE_READY) {
		/* No producer: the placeholder at the consumer's own rate. */
		if (now >= next_placeholder) {
			Deliver(0);
			next_placeholder = next_placeholder && now - next_placeholder < interval ? next_placeholder + interval
												 : now + interval;
		}
		return next_placeholder;
	}
	next_placeholder = 0;
	if (now >= next_announce) {
		video_queue_set_consumer(vq, interval); /* the producer follows the fastest consumer */
		next_announce = now + 2500000;
	}

	uint64_t sequence, ts, written;
	while (video_queue_peek(vq, last_sequence, &sequence, &ts, &written)) {
		/* A consumer slower than the producer's grid keeps the frames nearest its own. */
		if (next_grid && ts + interval / 4 < next_grid) {
			last_sequence = sequence;
			continue;
		}
		Adapt(sequence, ts, written, interval);
		const uint64_t due = Align(ts + latency, interval);
		if (due > now)
			return std::min(due, now + interval);
		/* Behind: a newer frame that is due as well replaces this one. */
		last_sequence = sequence;
		uint64_t newer, newer_ts, newer_written;
		if (video_queue_peek(vq, sequence, &newer, &newer_ts, &newer_written) && Align(newer_ts + latency, interval) <= now)
			continue;
		if (Deliver(sequence, due)) {
			next_grid = next_grid && ts < next_grid + interval ? next_grid + interval : ts + interval;
			next_repeat = now + std::max<uint64_t>(interval * 4, 1000000);
		}
		return now;
	}
	/* Nothing new: after a stall (paused, slow producer) the last picture again,
	   at the consumer's rate, so its stream does not stop. */
	if (next_repeat && now >= next_repeat) {
		if (!Deliver(last_sequence))
			Deliver(0);
		next_repeat = now + interval;
	}
	return now + 10000; /* look for the producer's next frame in 1 ms */
}

/* How long a frame waits after its grid time before it leaves: enough for the
   latest arrivals, so all leave evenly; between 3 ms and 2.5 frames. Counted once
   per frame; starts from the first frame's need. It rises at once but comes down
   only after 600 frames that all needed 2 ms less, in one step: a delay that keeps
   creeping (it used to step down every two seconds) keeps moving the delivery
   instant, and each pass across a consumer's own sampling instant shows as a
   repeat and a skip. Aligned to OBS (Align), it comes down only within the tick
   slot frames leave in now, which keeps their instant and leaves room for later
   rises; dropping into an earlier slot would show as one skipped frame. */
void VCamFilter::Adapt(uint64_t sequence, uint64_t ts, uint64_t written, uint64_t interval)
{
	if (sequence == adapted_sequence)
		return;
	adapted_sequence = sequence;
	const int64_t floor = 30000;
	const int64_t cap = std::max<int64_t>(floor, int64_t(interval) * 5 / 2);
	/* 3 ms past the latest write seen: this thread looks for new frames every
	   millisecond, so a frame finished just before its time would leave late. */
	const int64_t need = std::clamp<int64_t>(int64_t(written) - int64_t(ts) + 30000, floor, cap);
	if (need > int64_t(latency))
		latency = uint64_t(need);
	window_peak = std::max(window_peak, need);
	if (++window_frames >= 600) {
		int64_t lower = window_peak;
		uint64_t target;
		if (const uint64_t lo = AlignSlot(interval, &target)) {
			const uint64_t from = ts + latency;
			const uint64_t due = from + (target + lo - from % lo) % lo;
			lower = std::max<int64_t>(lower, int64_t(due - lo + 1 - ts));
		}
		if (lower + 20000 <= int64_t(latency))
			latency = uint64_t(lower);
		window_peak = 0;
		window_frames = 0;
	}
}

/* How often frames leave: at the consumer's rate, except inside OBS when its
   capture source asks for a whole multiple of OBS's own render rate (120 against a
   60 fps canvas): OBS shows only the newest picture per render tick, so the frames
   in between are never seen, and with them in the stream a render tick a few
   milliseconds late already picks a different frame. */
uint64_t VCamFilter::Rate()
{
	const uint64_t interval = std::max<uint64_t>(10000, (uint64_t)GetInterval());
	if (!obs_frame_interval)
		return interval;
	const uint64_t period = obs_frame_interval() / 100;
	if (period <= interval + interval / 100)
		return interval;
	const uint64_t whole = (period + interval / 2) / interval * interval;
	return (period > whole ? period - whole : whole - period) <= interval / 100 ? period : interval;
}

/* Inside OBS, the spacing of OBS's render ticks against this camera's frames and
   the phase halfway between two ticks (`target`, modulo the returned spacing);
   0 outside OBS, or when the two rates are not whole multiples of each other.
   os_gettime_ns (libobs) and video_queue_now_100ns are both QueryPerformanceCounter time. */
uint64_t VCamFilter::AlignSlot(uint64_t interval, uint64_t *target)
{
	if (!obs_frame_time)
		return 0;
	const uint64_t tick = obs_frame_time() / 100, period = obs_frame_interval() / 100;
	if (!tick || !period || !interval)
		return 0;
	const uint64_t lo = std::min(interval, period), hi = std::max(interval, period);
	const uint64_t whole = (hi + lo / 2) / lo * lo;
	if ((hi > whole ? hi - whole : whole - hi) > lo / 100)
		return 0;
	*target = (tick + lo / 2) % lo;
	return lo;
}

/* Inside OBS: the first instant from `earliest` that lies halfway between two of
   OBS's render ticks, where it takes the newest picture of an unbuffered source
   (OBS's default for this device). Delivery jitter then never decides which tick a
   frame lands on; a frame landing just after a tick shows a tick late, as a repeat
   and then a skip. Elsewhere `earliest` itself. */
uint64_t VCamFilter::Align(uint64_t earliest, uint64_t interval)
{
	uint64_t target;
	const uint64_t lo = AlignSlot(interval, &target);
	return lo ? earliest + (target + lo - earliest % lo) % lo : earliest;
}

/* One sample: frame `sequence` of the producer, or the placeholder (0). `due`:
   when it was meant to leave; its sample time says that, not the timer's wake-up. */
bool VCamFilter::Deliver(uint64_t sequence, uint64_t due)
{
	uint8_t *ptr;
	if (!LockSampleData(&ptr))
		return false;
	if (sequence) {
		if (!vq || !video_queue_read_sequence(vq, &scaler, ptr, sequence)) {
			DiscardSampleData();
			return false;
		}
	} else {
		ShowDefaultFrame(ptr);
	}
	int64_t sample_time = int64_t(GetTime() - stream_origin.load());
	const uint64_t copied = video_queue_now_100ns();
	if (due && copied > due)
		sample_time -= int64_t(std::min<uint64_t>(copied - due, (uint64_t)GetInterval()));
	/* Preserve monotonicity when clocks have coarse granularity. Veyra: by the
	   least step, not a frame: frames stamped with their due time could follow a
	   later-stamped repeat, and stamps pushed a frame ahead stay ahead, so a clocked
	   renderer downstream held every Receive (and this thread) for a frame. */
	if (sample_time <= last_sample_time)
		sample_time = last_sample_time + 1;
	last_sample_time = sample_time;
	UnlockSampleData(sample_time, sample_time + GetInterval());
	if (FILE *trace = due ? filter_trace() : nullptr) {
		const uint64_t delivered = video_queue_now_100ns();
		fprintf(trace, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n", sequence, due, trace_looked, copied, delivered);
	}
	return true;
}

void VCamFilter::ShowDefaultFrame(uint8_t *ptr)
{
	if (placeholder.scaled_data) {
		memcpy(ptr, placeholder.scaled_data, GetOutputBufferSize());
	} else {
		if (format == VideoFormat::YUY2) {
            for (int i=0;i<GetOutputBufferSize();i+=2) { ptr[i]=16; ptr[i+1]=128; }
        } else {
            const int y = GetCX()*GetCY(); memset(ptr,16,y); memset(ptr+y,128,GetOutputBufferSize()-y);
        }
	}
}

/* Called when the output resolution or format has changed to re-scale
   the placeholder graphic into the placeholder.scaled_data buffer. */
void VCamFilter::UpdatePlaceholder(void)
{
	if (!placeholder.source_data) {
		return;
	}

	if (placeholder.scaled_data) {
		free(placeholder.scaled_data);
	}

	placeholder.scaled_data = (uint8_t *)malloc(GetOutputBufferSize());
	if (!placeholder.scaled_data) {
		return;
	}

	if (placeholder.cx == GetCX() && placeholder.cy == GetCY() && placeholder.scaler.format == TARGET_FORMAT_NV12) {
		/* No scaling necessary if it matches exactly */
		memcpy(placeholder.scaled_data, placeholder.source_data, GetOutputBufferSize());
	} else {
		nv12_scale_init(&placeholder.scaler, placeholder.scaler.format, GetCX(), GetCY(), placeholder.cx,
				placeholder.cy);
		nv12_do_scale(&placeholder.scaler, placeholder.scaled_data, placeholder.source_data);
	}
}

/* Calculate the size of the output buffer based on the filter's
   resolution and format */
const int VCamFilter::GetOutputBufferSize(void)
{
	int bits = VFormatBits(format);
	return GetCX() * GetCY() * bits / 8;
}
