#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct video_queue;
struct nv12_scale;
typedef struct video_queue video_queue_t;
typedef struct nv12_scale nv12_scale_t;

enum queue_state {
	SHARED_QUEUE_STATE_INVALID,
	SHARED_QUEUE_STATE_STARTING,
	SHARED_QUEUE_STATE_READY,
	SHARED_QUEUE_STATE_STOPPING,
};

/* Veyra protocol 2 (2026-11-17): a ring of frames in write order, each with its
   place on the camera's time grid, so a reader can hand out every frame once at
   an even pace instead of sampling "the newest" on a clock of its own; readers
   announce the frame interval their consumer negotiated, so the producer can
   follow it. Timestamps are video_queue_now_100ns(): QueryPerformanceCounter in
   100 ns units, the same in every process. */
extern uint64_t video_queue_now_100ns(void);

extern video_queue_t *video_queue_create(uint32_t cx, uint32_t cy, uint64_t interval);
extern video_queue_t *video_queue_open();
extern void video_queue_close(video_queue_t *vq);

extern void video_queue_get_info(video_queue_t *vq, uint32_t *cx, uint32_t *cy, uint64_t *interval);
/* timestamp: the frame's time on the camera grid (video_queue_now_100ns clock). */
extern bool video_queue_write(video_queue_t *vq, uint8_t **data, uint32_t *linesize, uint64_t timestamp);
extern void video_queue_heartbeat(video_queue_t *vq);
extern enum queue_state video_queue_state(video_queue_t *vq);
/* The newest frame. */
extern bool video_queue_read(video_queue_t *vq, nv12_scale_t *scale, void *dst, uint64_t *ts);

/* Reader side, in order: the oldest frame still held whose sequence is above
   `after` (0 = from the oldest held). written = when the producer finished it. */
extern bool video_queue_peek(video_queue_t *vq, uint64_t after, uint64_t *sequence, uint64_t *ts, uint64_t *written);
/* Copies frame `sequence`; false once the producer has reused its slot. */
extern bool video_queue_read_sequence(video_queue_t *vq, nv12_scale_t *scale, void *dst, uint64_t sequence);
/* Announce (and keep alive, call at least once a second) the frame interval
   this reader's consumer negotiated. */
extern void video_queue_set_consumer(video_queue_t *vq, uint64_t interval);

/* Producer side: the shortest interval a live reader announced, 0 if none. */
extern uint64_t video_queue_consumer_interval(video_queue_t *vq);

#ifdef __cplusplus
}
#endif
