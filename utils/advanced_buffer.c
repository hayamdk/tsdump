#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#ifdef _MSC_VER
#include <sys/timeb.h>  
#include <time.h>  
#else
#include <sys/time.h>
#endif

#include "advanced_buffer.h"

#define	UNREF_ARG(x) ((void)(x))

#define alignment_floor(x, alignment_size) ( (alignment_size) > 1 ? (x) / (alignment_size) * (alignment_size) : (x) )
#define alignment_ceil(x, alignment_size)  ( (alignment_size) > 1 ? ((x)+(alignment_size)-1) / (alignment_size) * (alignment_size) : (x) )

int ab_next_downstream(ab_buffer_t *gb, int id)
{
	int i;
	for (i = id + 1; i < GB_MAX_DOWNSTREAMS; i++) {
		if (gb->downstreams[i].in_use) {
			return i;
		}
	}
	return -1;
}

int ab_first_downstream(ab_buffer_t *gb)
{
	return ab_next_downstream(gb, -1);
}

static void ab_check_close(ab_buffer_t *gb)
{
	int i, j;
	ab_downstream_t *ds;

	for (i = j = 0; i < gb->n_downstreams; j++) {
		assert(j < GB_MAX_DOWNSTREAMS);
		ds = &gb->downstreams[j];
		if (!ds->in_use) {
			/* do nothing */
		} else if (!ds->busy && ds->close_flg && ds->remain_to_close <= 0) {
			/* close */
			if (ds->handler.close) {
				ds->handler.close(gb, ds->param, NULL, 0);
			}
			ds->in_use = 0;
			gb->n_downstreams--;
		} else {
			i++;
		}
	}
}

void ab_init_buf(ab_buffer_t *gb, int buf_size)
{
	int i;
	gb->buf = malloc(buf_size);
	gb->buf_size = buf_size;
	gb->buf_used = 0;
	gb->n_downstreams = 0;
	gb->input_total = 0;
	for (i = 0; i < GB_MAX_DOWNSTREAMS; i++) {
		gb->downstreams[i].in_use = 0;
	}
}

int ab_connect_downstream_backward(ab_buffer_t *gb, const ab_downstream_handler_t *handler, int alignment_size, int max_size, int realtime, void *param, int backward_size)
{
	int insert, diff, remain;

	if (gb->n_downstreams >= GB_MAX_DOWNSTREAMS) {
		return -1;
	}

	for (insert = 0; insert < GB_MAX_DOWNSTREAMS; insert++) {
		if (!gb->downstreams[insert].in_use) { break; }
	}
	assert(insert < GB_MAX_DOWNSTREAMS);

	gb->downstreams[insert].handler = *handler;
	gb->downstreams[insert].param = param;
	gb->downstreams[insert].alignment_size = alignment_size;
	gb->downstreams[insert].busy = 0;
	gb->downstreams[insert].close_flg = 0;
	gb->downstreams[insert].in_use = 1;
	gb->downstreams[insert].realtime = realtime;

	diff = -backward_size;
	if (alignment_size > 0) {
		assert(gb->input_total + diff >= 0);
		remain = (int)((gb->input_total + diff) % alignment_size);
		if (remain > 0) {
			diff += alignment_size - remain;
		}
	}
	gb->downstreams[insert].pos = gb->buf_used + diff;
	if (gb->downstreams[insert].pos < 0) {
		gb->downstreams[insert].pos = 0;
	}

	if (alignment_size > 0 && max_size > 0) {
		if (max_size < alignment_size) {
			gb->downstreams[insert].max_size = alignment_size;
		} else {
			gb->downstreams[insert].max_size = alignment_floor(max_size, alignment_size);
		}
	} else {
		gb->downstreams[insert].max_size = max_size;
	}

	gb->n_downstreams++;
	return insert;
}

int ab_connect_downstream_history_backward(ab_buffer_t *gb, const ab_downstream_handler_t *handler, int alignment_size, int max_size, int realtime, void *param, ab_history_t *history)
{
	return ab_connect_downstream_backward(gb, handler, alignment_size, max_size, realtime, param,
		ab_get_history_backward_bytes(history)
	);
}

int ab_connect_downstream(ab_buffer_t *gb, const ab_downstream_handler_t *handler, int alignment_size, int max_size, int realtime, void *param)
{
	return ab_connect_downstream_backward(gb, handler, alignment_size, max_size, realtime, param, 0);
}

void ab_clear_buf(ab_buffer_t *gb, int require_size)
{
	assert(0 <= require_size && require_size <= gb->buf_used);

	int i, j;
	int clear_size, move_size, skip_size;

	clear_size = gb->buf_used;
	for (i = j = 0; i < gb->n_downstreams; j++) {
		assert(j < GB_MAX_DOWNSTREAMS);
		if (!gb->downstreams[j].in_use) {
			continue;
		}
		if (gb->downstreams[j].pos < clear_size) {
			clear_size = gb->downstreams[j].pos;
		}
		i++;
	}

	assert(0 <= clear_size && clear_size <= gb->buf_used);
	if (gb->buf_used + require_size - clear_size > gb->buf_size) {
		clear_size = gb->buf_used + require_size - gb->buf_size;
	}
	move_size = gb->buf_used - clear_size;
	assert(0 <= move_size && move_size <= gb->buf_used);

	if (move_size > 0 && clear_size > 0) {
		memmove(gb->buf, &gb->buf[clear_size], move_size);
	}
	gb->buf_used -= clear_size;

	for (i = j = 0; i < gb->n_downstreams; j++) {
		assert(j < GB_MAX_DOWNSTREAMS);
		if (!gb->downstreams[j].in_use) {
			continue;
		}
		i++;

		skip_size = clear_size - gb->downstreams[j].pos;
		if (skip_size < 0) {
			skip_size = 0;
		}
		skip_size = alignment_ceil(skip_size, gb->downstreams[j].alignment_size);

		gb->downstreams[j].pos -= clear_size - skip_size;
		assert(gb->downstreams[j].pos >= 0);

		if (skip_size > 0 && gb->downstreams[j].handler.notify_skip) {
			gb->downstreams[j].handler.notify_skip(gb, gb->downstreams[j].param, skip_size);
		}
	}
}

void ab_input_buf(ab_buffer_t *gb, const uint8_t *buf, int size/*, int timenum*/)
{
	if (gb->buf_used + size > gb->buf_size) {
		ab_clear_buf(gb, size);
	}
	memcpy(&gb->buf[gb->buf_used], buf, size);
	gb->buf_used += size;
	gb->input_total += size;
}

void ab_output_buf(ab_buffer_t *gb)
{
	int i, j, max_write_size, write_size;
	ab_downstream_t *ds;

	for (i = j = 0; i < gb->n_downstreams; j++) {
		assert(j < GB_MAX_DOWNSTREAMS);
		ds = &gb->downstreams[j];
		max_write_size = 0;

		if (!ds->in_use) {
			continue;
		}
		i++;

		write_size = gb->buf_used - ds->pos;
		if ( !ds->realtime && ds->max_size > 0 && write_size < ds->max_size / 2 ) {
			continue;
		}

		if (ds->handler.start_hook) {
			ds->handler.start_hook(gb, ds->param);
		}
		if (ds->busy && ds->handler.pre_output) {
			ds->busy = ds->handler.pre_output(gb, ds->param, &max_write_size);
			max_write_size = alignment_floor(max_write_size, ds->alignment_size);
		}
		if (ds->busy) {
			continue;
		}

		if (ds->max_size > 0 && (max_write_size > ds->max_size || max_write_size == 0)) {
			max_write_size = ds->max_size;
		}

		if (max_write_size/*is aligned*/ > 0 && write_size > max_write_size) {
			write_size = max_write_size;
		} else if(ds->alignment_size > 0) {
			write_size = alignment_floor(write_size, ds->alignment_size);
		}

		if (ds->close_flg && write_size > ds->remain_to_close/*is aligned*/) {
			write_size = ds->remain_to_close;
		}

		if (write_size > 0) {
			if (ds->handler.output) {
				ds->handler.output(gb, ds->param, &gb->buf[ds->pos], write_size);
			}
			if (ds->handler.pre_output) {
				ds->busy = 1;
			}
			ds->pos += write_size;
			if (ds->close_flg) {
				ds->remain_to_close -= write_size;
			}
		}
	}

	ab_check_close(gb);
}

void ab_close_buf(ab_buffer_t *gb)
{
	int i, j, write_size;
	ab_downstream_t *ds;

	for (i = j = 0; i < gb->n_downstreams; j++) {
		assert(j < GB_MAX_DOWNSTREAMS);
		ds = &gb->downstreams[j];
		if (!ds->in_use) {
			continue;
		}
		i++;
		write_size = gb->buf_used - ds->pos;
		if (ds->handler.close) {
			ds->handler.close(gb, ds->param, &gb->buf[ds->pos], write_size);
		}
	}
}

void ab_get_status(ab_buffer_t *gb, int *buf_size, int *buf_used)
{
	if (buf_size) {
		*buf_size = gb->buf_size;
	}
	if (buf_used) {
		*buf_used = gb->buf_used;
	}
}

int ab_get_downstream_status(ab_buffer_t *gb, int id, int *buf_pos, int *remain_size)
{
	assert(id < GB_MAX_DOWNSTREAMS && gb->downstreams[id].in_use);
	if (buf_pos) {
		*buf_pos = gb->downstreams[id].pos;
	}
	if (remain_size) {
		*remain_size = alignment_floor(
			gb->buf_used - gb->downstreams[id].pos, gb->downstreams[id].alignment_size
		);
	}
	return gb->downstreams[id].busy;
}

void ab_disconnect_downstream(ab_buffer_t *gb, int id, int immediate)
{
	ab_downstream_t *ds = &gb->downstreams[id];
	assert(ds->in_use);
	if (immediate) {
		ds->close_flg = 1;
		ds->remain_to_close = 0;
	} else {
		ds->close_flg = 1;
		ds->remain_to_close = alignment_floor(gb->buf_used - ds->pos, ds->alignment_size);
	}
}

static int64_t gettime()
{
	int64_t result;

#ifdef _MSC_VER
	struct _timeb tv;
	_ftime64_s(&tv);
	result = (int64_t)tv.time * 1000;
	result += tv.millitm;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	result = (int64_t)tv.tv_sec * 1000;
	result += tv.tv_usec / 1000;
#endif

	return result;
}

static void history_handler_output(ab_buffer_t *gb, void *param, const uint8_t *buf, int size)
{
	UNREF_ARG(gb);
	UNREF_ARG(buf);
	ab_history_t *history = (ab_history_t*)param;
	int movesize, i;
	int64_t timenum = gettime() / history->resolution;

	if (history->used == 0 || history->records[0].timenum != timenum) {
		movesize = history->used;
		if (movesize >= history->num) {
			movesize--;
		}
		if (movesize > 0) {
			memmove(&history->records[1], &history->records[0], sizeof(ab_history_record_t) * movesize);
		}
		history->used = movesize + 1;
		history->records[0].timenum = timenum;
		history->records[0].bytes = 0;

		for (history->backward_bytes = i = 0; i < history->used; i++) {
			if (history->records[i].timenum < timenum - history->backward_ms / history->resolution) {
				continue;
			}
			history->backward_bytes += history->records[i].bytes;
		}
	}
	history->records[0].bytes += size;
	history->backward_bytes += size;
}

static void history_handler_start_hook(ab_buffer_t *gb, void *param)
{
	UNREF_ARG(gb);
	int buf_used, buf_pos;
	ab_history_t *history = (ab_history_t*)param;
	ab_get_status(history->buffer, NULL, &buf_used);
	ab_get_downstream_status(history->buffer, history->stream_id, &buf_pos, NULL);
	history->buf_remain_bytes = buf_used - buf_pos;
}

static int history_handler_pre_output(ab_buffer_t *gb, void *param, int *outbytes)
{
	UNREF_ARG(gb);
	ab_history_t *history = (ab_history_t*)param;
	if (history->buf_remain_bytes > history->backward_bytes) {
		*outbytes = history->buf_remain_bytes - history->backward_bytes;
		return 0;
	}
	return 1;
}

int ab_set_history(ab_buffer_t *gb, ab_history_t *history, int resolution_ms, int backward_ms)
{
	const ab_downstream_handler_t history_handler1 = { history_handler_output };
	const ab_downstream_handler_t history_handler2 = { NULL, NULL, NULL, history_handler_start_hook, history_handler_pre_output };
	int num = (backward_ms + resolution_ms - 1) / resolution_ms;

	if (num <= 0) {
		return 1;
	}

	history->buffer = gb;
	history->resolution = resolution_ms;
	history->num = num;
	history->used = 0;
	history->records = malloc(sizeof(ab_history_record_t) * num);
	history->backward_ms = backward_ms;
	history->buf_remain_bytes = 0;
	history->backward_bytes = 0;

	if (ab_connect_downstream(gb, &history_handler1, 0, 0, 1, history) < 0) {
		return 1;
	}
	if ((history->stream_id = ab_connect_downstream(gb, &history_handler2, 0, 0, 1, history)) < 0) {
		return 1;
	}
	return 0;
}

int ab_get_history_backward_bytes(ab_history_t *history)
{
	return history->backward_bytes;
}