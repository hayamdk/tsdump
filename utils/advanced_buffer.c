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

struct ab_downstream_struct {
	ab_downstream_handler_t handler;
	void *param;
	unsigned int busy : 1;
	unsigned int close_flg : 1;
	unsigned int in_use : 1;
	unsigned int realtime : 1;
	int pos;
	int remain_to_close;
	int alignment_size;
	int max_size;
};

struct ab_buffer_struct {
	ab_downstream_t downstreams[AB_MAX_DOWNSTREAMS];
	uint64_t input_total;
	int n_downstreams;
	int buf_used;
	int buf_size;
	uint8_t *buf;
	ab_history_t *history;
};

typedef struct {
	int64_t timenum;
	int bytes;
} ab_history_record_t;

struct ab_history_struct {
	unsigned int close_flg : 1;
	ab_buffer_t *buffer;
	ab_history_record_t *records;
	int resolution;
	int num;
	int used;
	int backward_ms;
	int buf_remain_bytes;
	int backward_bytes;
	int stream_id0;
	int stream_id;
};

#define	UNREF_ARG(x) ((void)(x))

#define alignment_floor(x, alignment_size) ( (alignment_size) > 1 ? (x) / (alignment_size) * (alignment_size) : (x) )
#define alignment_ceil(x, alignment_size)  ( (alignment_size) > 1 ? ((x)+(alignment_size)-1) / (alignment_size) * (alignment_size) : (x) )

static int ab_next_downstream_internal(ab_buffer_t *ab, int id, int ignore_history)
{
	int i;
	const ab_downstream_t *ds;
	for (i = id + 1; i < AB_MAX_DOWNSTREAMS; i++) {
		ds = &ab->downstreams[i];
		if (!ds->in_use) {
			continue;
		}
		if (ab->history && ignore_history &&
				(i == ab->history->stream_id0 || i == ab->history->stream_id)) {
			continue;
		}
		return i;
	}
	return -1;
}

int ab_next_downstream(ab_buffer_t *ab, int id)
{
	return ab_next_downstream_internal(ab, id, 1);
}

int ab_first_downstream(ab_buffer_t *ab)
{
	return ab_next_downstream(ab, -1);
}

static void ab_check_close(ab_buffer_t *ab)
{
	int i, j;
	ab_downstream_t *ds;

	for (i = j = 0; i < ab->n_downstreams; j++) {
		assert(j < AB_MAX_DOWNSTREAMS);
		ds = &ab->downstreams[j];
		if (!ds->in_use) {
			/* do nothing */
		} else if (!ds->busy && ds->close_flg && ds->remain_to_close <= 0) {
			/* close */
			if (ds->handler.close) {
				ds->handler.close(ab, ds->param, NULL, 0);
			}
			ds->in_use = 0;
			ab->n_downstreams--;
		} else {
			i++;
		}
	}
}

void ab_init(ab_buffer_t *ab, int buf_size)
{
	int i;
	ab->buf = (uint8_t*)malloc(buf_size);
	ab->buf_size = buf_size;
	ab->buf_used = 0;
	ab->n_downstreams = 0;
	ab->input_total = 0;
	ab->history = NULL;
	for (i = 0; i < AB_MAX_DOWNSTREAMS; i++) {
		ab->downstreams[i].in_use = 0;
	}
}

ab_buffer_t* ab_create(int buf_size)
{
	ab_buffer_t *ab = (ab_buffer_t*)malloc(sizeof(ab_buffer_t));
	ab_init(ab, buf_size);
	return ab;
}

void ab_delete(ab_buffer_t *ab)
{
	ab_close_buf(ab);
	free(ab->buf);
	free(ab);
}

int ab_connect_downstream_backward(ab_buffer_t *ab, const ab_downstream_handler_t *handler, int alignment_size, int max_size, int realtime, void *param, int backward_size)
{
	int insert, diff, remain;

	if (ab->n_downstreams >= AB_MAX_DOWNSTREAMS) {
		return -1;
	}

	for (insert = 0; insert < AB_MAX_DOWNSTREAMS; insert++) {
		if (!ab->downstreams[insert].in_use) { break; }
	}
	assert(insert < AB_MAX_DOWNSTREAMS);

	ab->downstreams[insert].handler = *handler;
	ab->downstreams[insert].param = param;
	ab->downstreams[insert].alignment_size = alignment_size;
	ab->downstreams[insert].busy = 0;
	ab->downstreams[insert].close_flg = 0;
	ab->downstreams[insert].in_use = 1;
	ab->downstreams[insert].realtime = realtime;

	diff = -backward_size;
	if (alignment_size > 0) {
		assert(ab->input_total + diff >= 0);
		remain = (int)((ab->input_total + diff) % alignment_size);
		if (remain > 0) {
			diff += alignment_size - remain;
		}
	}
	ab->downstreams[insert].pos = ab->buf_used + diff;
	if (ab->downstreams[insert].pos < 0) {
		ab->downstreams[insert].pos = 0;
	}

	if (alignment_size > 0 && max_size > 0) {
		if (max_size < alignment_size) {
			ab->downstreams[insert].max_size = alignment_size;
		} else {
			ab->downstreams[insert].max_size = alignment_floor(max_size, alignment_size);
		}
	} else {
		ab->downstreams[insert].max_size = max_size;
	}

	ab->n_downstreams++;
	return insert;
}

int ab_connect_downstream_history_backward(ab_buffer_t *ab, const ab_downstream_handler_t *handler, int alignment_size, int max_size, int realtime, void *param, ab_history_t *history)
{
	return ab_connect_downstream_backward(ab, handler, alignment_size, max_size, realtime, param,
		ab_get_history_backward_bytes(history)
	);
}

int ab_connect_downstream(ab_buffer_t *ab, const ab_downstream_handler_t *handler, int alignment_size, int max_size, int realtime, void *param)
{
	return ab_connect_downstream_backward(ab, handler, alignment_size, max_size, realtime, param, 0);
}

void ab_clear_buf(ab_buffer_t *ab, int require_size)
{
	assert(0 <= require_size && require_size <= ab->buf_used);

	int i, j;
	int clear_size, move_size, skip_size;

	clear_size = ab->buf_used;
	for (i = j = 0; i < ab->n_downstreams; j++) {
		assert(j < AB_MAX_DOWNSTREAMS);
		if (!ab->downstreams[j].in_use) {
			continue;
		}
		if (ab->downstreams[j].pos < clear_size) {
			clear_size = ab->downstreams[j].pos;
		}
		i++;
	}

	assert(0 <= clear_size && clear_size <= ab->buf_used);
	if (ab->buf_used + require_size - clear_size > ab->buf_size) {
		clear_size = ab->buf_used + require_size - ab->buf_size;
	}
	move_size = ab->buf_used - clear_size;
	assert(0 <= move_size && move_size <= ab->buf_used);

	if (move_size > 0 && clear_size > 0) {
		memmove(ab->buf, &ab->buf[clear_size], move_size);
	}
	ab->buf_used -= clear_size;

	for (i = j = 0; i < ab->n_downstreams; j++) {
		assert(j < AB_MAX_DOWNSTREAMS);
		if (!ab->downstreams[j].in_use) {
			continue;
		}
		i++;

		skip_size = clear_size - ab->downstreams[j].pos;
		if (skip_size < 0) {
			skip_size = 0;
		}
		skip_size = alignment_ceil(skip_size, ab->downstreams[j].alignment_size);

		ab->downstreams[j].pos -= clear_size - skip_size;
		assert(ab->downstreams[j].pos >= 0);

		if (skip_size > 0 && ab->downstreams[j].handler.notify_skip) {
			ab->downstreams[j].handler.notify_skip(ab, ab->downstreams[j].param, skip_size);
		}
	}
}

void ab_input_buf(ab_buffer_t *ab, const uint8_t *buf, int size/*, int timenum*/)
{
	if (ab->buf_used + size > ab->buf_size) {
		ab_clear_buf(ab, size);
	}
	memcpy(&ab->buf[ab->buf_used], buf, size);
	ab->buf_used += size;
	ab->input_total += size;
}

void ab_output_buf(ab_buffer_t *ab)
{
	int i, j, max_write_size, write_size;
	ab_downstream_t *ds;

	for (i = j = 0; i < ab->n_downstreams; j++) {
		assert(j < AB_MAX_DOWNSTREAMS);
		ds = &ab->downstreams[j];
		max_write_size = 0;

		if (!ds->in_use) {
			continue;
		}
		i++;

		write_size = ab->buf_used - ds->pos;
		if ( !ds->realtime && !ds->close_flg && ds->max_size > 0 && write_size < ds->max_size / 2 ) {
			continue;
		}

		if (ds->handler.start_hook) {
			ds->handler.start_hook(ab, ds->param);
		}
		if (ds->busy && ds->handler.pre_output) {
			ds->busy = ds->handler.pre_output(ab, ds->param, &max_write_size);
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
				ds->handler.output(ab, ds->param, &ab->buf[ds->pos], write_size);
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

	ab_check_close(ab);
}

void ab_close_buf(ab_buffer_t *ab)
{
	int i, j, write_size;
	ab_downstream_t *ds;

	for (i = j = 0; i < ab->n_downstreams; j++) {
		assert(j < AB_MAX_DOWNSTREAMS);
		ds = &ab->downstreams[j];
		if (!ds->in_use) {
			continue;
		}
		i++;
		write_size = ab->buf_used - ds->pos;
		if (ds->handler.close) {
			ds->handler.close(ab, ds->param, &ab->buf[ds->pos], write_size);
		}
		ds->in_use = 0;
	}
}

void ab_get_status(ab_buffer_t *ab, int *buf_used)
{
	if (buf_used) {
		*buf_used = ab->buf_used;
	}
}

int ab_get_downstream_status(ab_buffer_t *ab, int id, int *buf_pos, int *remain_size)
{
	assert(id < AB_MAX_DOWNSTREAMS && ab->downstreams[id].in_use);
	if (buf_pos) {
		*buf_pos = ab->downstreams[id].pos;
	}
	if (remain_size) {
		*remain_size = alignment_floor(
			ab->buf_used - ab->downstreams[id].pos, ab->downstreams[id].alignment_size
		);
	}
	return ab->downstreams[id].busy;
}

void ab_disconnect_downstream(ab_buffer_t *ab, int id, int immediate)
{
	ab_downstream_t *ds = &ab->downstreams[id];
	assert(ds->in_use);
	if (immediate) {
		ds->close_flg = 1;
		ds->remain_to_close = 0;
	} else {
		ds->close_flg = 1;
		ds->remain_to_close = alignment_floor(ab->buf_used - ds->pos, ds->alignment_size);
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

static void history_handler_output(ab_buffer_t *ab, void *param, const uint8_t *buf, int size)
{
	UNREF_ARG(ab);
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

static void history_handler_close(ab_buffer_t *ab, void *param, const uint8_t *buf, int size)
{
	UNREF_ARG(ab);
	UNREF_ARG(buf);
	UNREF_ARG(size);
	ab_history_t *history = (ab_history_t*)param;
	if (!history->close_flg) {
		/* first: set close_flg */
		history->close_flg = 1;
	} else {
		/* second: actually close */
		free(history->records);
		free(history);
	}
}

static void history_handler_start(ab_buffer_t *ab, void *param)
{
	UNREF_ARG(ab);
	int buf_used, buf_pos;
	ab_history_t *history = (ab_history_t*)param;
	ab_get_status(history->buffer, &buf_used);
	ab_get_downstream_status(history->buffer, history->stream_id, &buf_pos, NULL);
	history->buf_remain_bytes = buf_used - buf_pos;
}

static int history_handler_pre_output(ab_buffer_t *ab, void *param, int *outbytes)
{
	UNREF_ARG(ab);
	ab_history_t *history = (ab_history_t*)param;
	if (history->close_flg) {
		return 0;
	}
	if (history->buf_remain_bytes > history->backward_bytes) {
		*outbytes = history->buf_remain_bytes - history->backward_bytes;
		return 0;
	}
	return 1;
}

int ab_set_history(ab_buffer_t *ab, ab_history_t **history_in, int resolution_ms, int backward_ms)
{
	ab_history_t *history;
	const ab_downstream_handler_t history_handler1 = { history_handler_output, NULL, history_handler_close, NULL, NULL };
	const ab_downstream_handler_t history_handler2 = { NULL, NULL, history_handler_close, history_handler_start, history_handler_pre_output };
	int num = (backward_ms + resolution_ms - 1) / resolution_ms;

	if (num <= 0) {
		return 1;
	}

	history = (ab_history_t*)malloc(sizeof(ab_history_t));
	if ((history->stream_id0 = ab_connect_downstream(ab, &history_handler1, 0, 0, 1, history)) < 0) {
		free(history);
		return 1;
	}

	history->buffer = ab;
	ab->history = history;
	history->resolution = resolution_ms;
	history->num = num;
	history->used = 0;
	history->records = malloc(sizeof(ab_history_record_t) * num);
	history->backward_ms = backward_ms;
	history->buf_remain_bytes = 0;
	history->backward_bytes = 0;
	history->close_flg = 1;

	if ((history->stream_id = ab_connect_downstream(ab, &history_handler2, 0, 0, 1, history)) < 0) {
		ab_disconnect_downstream(ab, history->stream_id0, 1);
		return 1;
	}
	history->close_flg = 0;

	if (history_in) {
		*history_in = history;
	}
	return 0;
}

int ab_get_history_backward_bytes(ab_history_t *history)
{
	return history->backward_bytes;
}

int ab_get_history_bytes(ab_history_t *history, int n)
{
	if (history->used == 0 && n == 0) {
		return 0;
	}
	assert(n < history->used);
	return history->records[n].bytes;
}
