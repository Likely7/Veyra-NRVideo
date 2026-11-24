/* Veyra 2026-11-24: sizes and source offsets in 64-bit / size_t arithmetic. */
#include <stddef.h>
#include <string.h>
#include "tiny-nv12-scale.h"

/* TODO: optimize this stuff later, or replace with something better.  it's
 * kind of garbage.  although normally it shouldn't be called that often.  plus
 * it's nearest neighbor so not really a huge deal.  at the very least it
 * should be sse2 at some point. */

void nv12_scale_init(nv12_scale_t *s, enum target_format format, int dst_cx, int dst_cy, int src_cx, int src_cy)
{
	s->format = format;

	s->src_cx = src_cx;
	s->src_cy = src_cy;

	s->dst_cx = dst_cx;
	s->dst_cy = dst_cy;
}

static void nv12_scale_nearest(nv12_scale_t *s, uint8_t *dst_start, const uint8_t *src)
{
	register uint8_t *dst = dst_start;
	const int src_cx = s->src_cx;
	const int src_cy = s->src_cy;
	const int dst_cx = s->dst_cx;
	const int dst_cy = s->dst_cy;

	/* lum */
	for (int y = 0; y < dst_cy; y++) {
		const int64_t src_line = (int64_t)y * src_cy / dst_cy * s->src_cx;

		for (int x = 0; x < dst_cx; x++) {
			const int64_t src_x = (int64_t)x * src_cx / dst_cx;

			*(dst++) = src[src_line + src_x];
		}
	}

	src += (size_t)src_cx * src_cy;

	/* uv */
	const int dst_cx_d2 = dst_cx / 2;
	const int dst_cy_d2 = dst_cy / 2;

	for (int y = 0; y < dst_cy_d2; y++) {
		const int64_t src_line = (int64_t)y * src_cy / dst_cy * src_cx;

		for (int x = 0; x < dst_cx_d2; x++) {
			const int64_t src_x = (int64_t)x * src_cx / dst_cx * 2;
			const int64_t pos = src_line + src_x;

			*(dst++) = src[pos];
			*(dst++) = src[pos + 1];
		}
	}
}

static void nv12_scale_nearest_to_i420(nv12_scale_t *s, uint8_t *dst_start, const uint8_t *src)
{
	register uint8_t *dst = dst_start;
	const int src_cx = s->src_cx;
	const int src_cy = s->src_cy;
	const int dst_cx = s->dst_cx;
	const int dst_cy = s->dst_cy;
	const size_t size = (size_t)src_cx * src_cy;

	/* lum */
	for (int y = 0; y < dst_cy; y++) {
		const int64_t src_line = (int64_t)y * src_cy / dst_cy * s->src_cx;

		for (int x = 0; x < dst_cx; x++) {
			const int64_t src_x = (int64_t)x * src_cx / dst_cx;

			*(dst++) = src[src_line + src_x];
		}
	}

	src += size;

	/* uv */
	const int dst_cx_d2 = dst_cx / 2;
	const int dst_cy_d2 = dst_cy / 2;

	register uint8_t *dst2 = dst + (size_t)dst_cx * dst_cy / 4;

	for (int y = 0; y < dst_cy_d2; y++) {
		const int64_t src_line = (int64_t)y * src_cy / dst_cy * src_cx;

		for (int x = 0; x < dst_cx_d2; x++) {
			const int64_t src_x = (int64_t)x * src_cx / dst_cx * 2;
			const int64_t pos = src_line + src_x;

			*(dst++) = src[pos];
			*(dst2++) = src[pos + 1];
		}
	}
}

static void nv12_convert_to_i420(nv12_scale_t *s, uint8_t *dst_start, const uint8_t *src_start)
{
	const size_t size = (size_t)s->src_cx * s->src_cy;
	const size_t size_d4 = size / 4;

	memcpy(dst_start, src_start, size);

	register uint8_t *dst1 = dst_start + size;
	register uint8_t *dst2 = dst1 + size_d4;
	register uint8_t *dst_end = dst2 + size_d4;
	register const uint8_t *src = src_start + size;

	while (dst2 < dst_end) {
		*(dst1++) = *(src++);
		*(dst2++) = *(src++);
	}
}

static void nv12_scale_nearest_to_yuy2(nv12_scale_t *s, uint8_t *dst_start, const uint8_t *src)
{
	register uint8_t *dst = dst_start;
	const int src_cx = s->src_cx;
	const int src_cy = s->src_cy;
	const int dst_cx = s->dst_cx;
	const int dst_cy = s->dst_cy;
	const int src_cx_d2 = src_cx / 2;
	const int src_cy_d2 = src_cy / 2;
	const int dst_cx_d2 = dst_cx / 2;
	const int dst_cy_d2 = dst_cy / 2;
	const size_t size = (size_t)src_cx * src_cy;

	const uint8_t *src_uv = src + size;

	register int uv_flip = 0;

	for (int y = 0; y < dst_cy; y++) {
		const int64_t src_line = (int64_t)y * src_cy / dst_cy * s->src_cx;
		const int64_t src_line_d2 = (int64_t)(y / 2) * src_cy_d2 / dst_cy_d2 * s->src_cx;

		for (int x = 0; x < dst_cx; x++) {
			const int64_t src_x = (int64_t)x * src_cx / dst_cx;
			const int64_t src_x_d2 = (int64_t)(x / 2) * src_cx_d2 / dst_cx_d2;
			const int64_t pos = src_line + src_x;
			const int64_t pos_uv = src_line_d2 + src_x_d2 * 2 + uv_flip;

			*(dst++) = src[pos];
			*(dst++) = src_uv[pos_uv];

			uv_flip ^= 1;
		}
	}
}

static void nv12_convert_to_yuy2(nv12_scale_t *s, uint8_t *dst_start, const uint8_t *src_start)
{
	const size_t size = (size_t)s->src_cx * s->src_cy;
	const size_t size_dst_line = (size_t)s->src_cx * 2;

	register const uint8_t *src_y = src_start;
	register const uint8_t *src_uv = src_y + size;
	register uint8_t *dst = dst_start;
	register uint8_t *dst_end = dst + size * 2;

	while (dst < dst_end) {
		register uint8_t *dst_line_end = dst + size_dst_line;
		const uint8_t *src_uv_start = src_uv;
		while (dst < dst_line_end) {
			*(dst++) = *(src_y++);
			*(dst++) = *(src_uv++);
			*(dst++) = *(src_y++);
			*(dst++) = *(src_uv++);
		}

		dst_line_end = dst + size_dst_line;
		src_uv = src_uv_start;
		while (dst < dst_line_end) {
			*(dst++) = *(src_y++);
			*(dst++) = *(src_uv++);
			*(dst++) = *(src_y++);
			*(dst++) = *(src_uv++);
		}
	}
}

void nv12_do_scale(nv12_scale_t *s, uint8_t *dst, const uint8_t *src)
{
	if (s->src_cx == s->dst_cx && s->src_cy == s->dst_cy) {
		if (s->format == TARGET_FORMAT_I420)
			nv12_convert_to_i420(s, dst, src);
		else if (s->format == TARGET_FORMAT_YUY2)
			nv12_convert_to_yuy2(s, dst, src);
		else
			memcpy(dst, src, (size_t)s->src_cx * s->src_cy * 3 / 2);
	} else {
		if (s->format == TARGET_FORMAT_I420)
			nv12_scale_nearest_to_i420(s, dst, src);
		else if (s->format == TARGET_FORMAT_YUY2)
			nv12_scale_nearest_to_yuy2(s, dst, src);
		else
			nv12_scale_nearest(s, dst, src);
	}
}
