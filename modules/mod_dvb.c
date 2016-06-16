#include "core/tsdump_def.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/audio.h>
#include <linux/dvb/version.h>

#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"

#define BLOCK_SIZE		(188*256)
#define TYPE_ISDB_T		0
#define TYPE_ISDB_S_BS		1
#define TYPE_ISDB_S_CS		2

typedef enum {
	DVB_TUNER_OTHER,
	DVB_TUNER_PT,
	DVB_TUNER_PT3,
	DVB_TUNER_FRIIO_W,
} tuner_model_t;

typedef struct {
	int fd_fe;
	int fd_demux;
	int fd;
	uint8_t buf[BLOCK_SIZE];
	uint8_t tmp_buf[BLOCK_SIZE];
	char tuner_name[256];
	int bytes;
	tuner_model_t model;
} dvb_stat_t;

static int enabled = 0;
static int dvb_dev = -1;
static int dvb_type = -1;
static int dvb_ch = -1;
static int dvb_tsnum = -1;

static int dvb_freq = 0;
static int dvb_tsid = 0;

static char channel_str[32];
static char type_str[32];

static int resolve_channel()
{
	switch(dvb_type) {
		case TYPE_ISDB_T:
			dvb_freq = (dvb_ch*6 + 395) * 1000000 + 142857;
			sprintf(channel_str, "%02dch", dvb_ch);
			strcpy(type_str, "ISDB-T");
			break;
		case TYPE_ISDB_S_BS:
			if(dvb_ch % 2 != 1) {
				output_message(MSG_ERROR, "不正なBSチャンネルです(奇数のみ有効)");
				return 0;
			}
			if(dvb_tsnum < 0) {
				dvb_tsnum = 0;
			}
			dvb_freq = (dvb_ch - 1) / 2 * 38360 + 1049480;
			dvb_tsid = dvb_ch * 0x10 + 0x4000 + dvb_tsnum;
			sprintf(channel_str, "%02d_ts%02d", dvb_ch, dvb_tsnum);
			strcpy(type_str, "ISDB-S(BS)");
			break;
		case TYPE_ISDB_S_CS:
			if(dvb_ch % 2 != 0) {
				output_message(MSG_ERROR, "不正なCSチャンネルです(偶数のみ有効)");
				return 0;
			}
			dvb_freq = (dvb_ch-2) / 2 * 40000 + 1613000;
			sprintf(channel_str, "ND%02d", dvb_ch);
			strcpy(type_str, "ISDB-S(CS110)");
			break;
	}
	return 1;
}

static int hook_postconfig()
{
	if(enabled) {
		if(dvb_type < 0) {
			output_message(MSG_ERROR, "DVBのチャンネルタイプが指定されていません");
			return 0;
		} else if(dvb_ch < 0) {
			output_message(MSG_ERROR, "DVBのチャンネルが指定されていません");
			return 0;
		} else if(dvb_type != TYPE_ISDB_S_BS && dvb_tsnum >= 0) {
			output_message(MSG_ERROR, "TS番号を指定できるのはBSのみです");
			return 0;
		} else {
			return resolve_channel();
		}
	}
	return 1;
}

static void dvb_read(dvb_stat_t *pstat)
{
	int ret;
	if(pstat->bytes >= BLOCK_SIZE) {
		return;
	}
	ret = read(pstat->fd, &pstat->buf[pstat->bytes], BLOCK_SIZE - pstat->bytes);
	if( ret < 0 ) {
		if( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ) {
			/* do nothing */
		} else {
			output_message(MSG_SYSERROR, "read(fd=%d)", pstat->fd);
		}
	} else {
		pstat->bytes += ret;
	}
}

static void hook_stream_generator(void *param, uint8_t **buf, int *size)
{
	int bytes188, remain;
	dvb_stat_t *pstat = (dvb_stat_t*)param;
	dvb_read(pstat);
	if(pstat->bytes < 188) {
		*buf = NULL;
		*size = 0;
		return;
	}
	bytes188 = pstat->bytes / 188 * 188;
	remain = pstat->bytes - bytes188;
	if(remain > 0) {
		memcpy(pstat->tmp_buf, pstat->buf, bytes188);
		memmove(&pstat->buf[0], &pstat->buf[bytes188], remain);
		*buf = pstat->tmp_buf;
		*size = bytes188;
		pstat->bytes = remain;
	} else {
		*buf = pstat->buf;
		*size = pstat->bytes;
		pstat->bytes = 0;
	}
}

static int hook_stream_generator_wait(void *param, int timeout_ms)
{
	int ret;
	fd_set set;
	struct timeval tv;
	dvb_stat_t *pstat = (dvb_stat_t*)param;

	if(pstat->bytes >= 188) {
		return 1;
	}

	FD_ZERO(&set);
	FD_SET(pstat->fd, &set);
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = timeout_ms % 1000 * 1000;

	ret = select(pstat->fd+1, &set, NULL, NULL, &tv);
	if( ret == 0 ) {
		return 0;
	} else if (ret == 1) {
		return 1;
	} else {
		if( errno != EINTR ) {
			output_message(MSG_SYSERROR, "select(fd=%d)", pstat->fd);
		}
	}
	return 0;
}

tuner_model_t check_tuner_model(const char *tuner_name)
{
	if(strncmp(tuner_name, "VA1J5JF8007/VA1J5JF8011", strlen("VA1J5JF8007/VA1J5JF8011")) == 0) {
		return DVB_TUNER_PT;
	} else if (strncmp(tuner_name, "Toshiba TC90522", strlen("Toshiba TC90522")) == 0) {
		return DVB_TUNER_PT3;
	} else if (strcmp(tuner_name, "Comtech JDVBT90502 ISDB-T") == 0) {
		return DVB_TUNER_FRIIO_W;
	}
	return DVB_TUNER_OTHER;
}

static int open_dvb_frontend(int dev, struct dvb_frontend_info *info, int output_err)
{
	int fd_fe;
	char file[MAX_PATH_LEN];
	sprintf(file, "/dev/dvb/adapter%d/frontend0", dev);

	if ((fd_fe = open(file, O_RDWR)) < 0) {
		if(output_err) {
			output_message(MSG_SYSERROR, "open: %s", file);
		}
		return -1;
	}

	if (ioctl(fd_fe, FE_GET_INFO, info) < 0) {
		if(output_err) {
			output_message(MSG_SYSERROR, "ioctl(FE_GET_INFO, adapter%d)", dev);
		}
		goto ERROR;
	}

	if (info->type == FE_QPSK) {
		if(dvb_type != TYPE_ISDB_S_BS && dvb_type != TYPE_ISDB_S_CS) {
			if(output_err) {
				output_message(MSG_ERROR, "Invalid tuner type (require: %s, adapter%d: ISDB-S)", type_str, dev);
			}
			goto ERROR;
		}
	} else if (info->type == FE_OFDM) {
		if(dvb_type != TYPE_ISDB_T) {
			if(output_err) {
				output_message(MSG_ERROR, "Invalid tuner type (require: %s, adapter%d: ISDB-T)", type_str, dev);
			}
			goto ERROR;
		}
	} else {
		if(output_err) {
			output_message(MSG_ERROR, "Invalid tuner type (adapter%d)", dev);
		}
		goto ERROR;
	}
	return fd_fe;
ERROR:
	close(fd_fe);
	return -1;
}

static int hook_stream_generator_open(void **param, ch_info_t *chinfo)
{
	int i;
	int fd, fd_fe=-1, fd_demux;
	struct dvb_frontend_info info;
	char file[MAX_PATH_LEN], tsid_str[32];
	struct dtv_property prop[3];
	struct dtv_properties props;
	fe_status_t status;
	dvb_stat_t *pstat;
	struct dmx_pes_filter_params filter;
	
	if(dvb_dev >= 0) {
		fd_fe = open_dvb_frontend(dvb_dev, &info, 1);
		if(fd_fe < 0) {
			goto END1;
		}
	} else {
		for(i=0; i<128; i++) {
			fd_fe = open_dvb_frontend(i, &info, 0);
			if(fd_fe >= 0) {
				break;
			}
		}
		if(fd_fe < 0) {
			output_message(MSG_ERROR, "開けるチューナーがありませんでした: %s", type_str);
			goto END1;
		}
		dvb_dev = i;
	}

	props.props = prop;
	prop[0].cmd = DTV_FREQUENCY;
	prop[0].u.data = dvb_freq;

	/* 地上波でDTV_STREAM_IDを指定するとエラーになるチューナーがある（friio白） */
	if(dvb_type == TYPE_ISDB_S_BS) {
		props.num = 3;
		prop[1].cmd = DTV_STREAM_ID;
		prop[1].u.data = dvb_tsid;
		prop[2].cmd = DTV_TUNE;
		prop[2].u.data = 0;
		sprintf(tsid_str, ", tsid=0x%x", dvb_tsid);
	} else {
		props.num = 2;
		prop[1].cmd = DTV_TUNE;
		prop[1].u.data = 0;
		strcpy(tsid_str, "");
	}

	if ((ioctl(fd_fe, FE_SET_PROPERTY, &props)) < 0) {
		output_message(MSG_SYSERROR, "ioctl(FE_SET_PROPERTY, adapter%d, freq=%d%s)", dvb_dev, dvb_freq, tsid_str);
		goto END1;
	}

	for (i = 0; i < 32; i++) {
		if (ioctl(fd_fe, FE_READ_STATUS, &status) < 0) {
			output_message(MSG_SYSERROR, "ioctl(FE_READ_STATUS, adapter%d, freq=%d%s)", dvb_dev, dvb_freq, tsid_str);
			goto END2;
		}
		if (status & FE_HAS_LOCK) {
			break;
		}
		usleep(250 * 1000);
	}

	if( i == 32 ) {
		output_message(MSG_ERROR, "Failed to tune (adapter%d, freq=%d%s)", dvb_dev, dvb_freq, tsid_str);
		goto END2;
	}

	sprintf(file, "/dev/dvb/adapter%d/demux0", dvb_dev);
	if ((fd_demux = open(file, O_RDWR)) < 0) {
		output_message(MSG_SYSERROR, "open: %s", file);
		goto END2;
	}

	filter.pid = 0x2000;
	filter.input = DMX_IN_FRONTEND;
	filter.output = DMX_OUT_TS_TAP;
	filter.pes_type =  DMX_PES_VIDEO;
	filter.flags = DMX_IMMEDIATE_START;

	if (ioctl(fd_demux, DMX_SET_PES_FILTER, &filter) < 0) {
		output_message(MSG_SYSERROR, "ioctl(DMX_SET_PES_FILTER, adapter%d)", dvb_dev);
		goto END3;
	}

	sprintf(file, "/dev/dvb/adapter%d/dvr0", dvb_dev);
	if ((fd = open(file, O_RDONLY|O_NONBLOCK)) < 0) {
		output_message(MSG_SYSERROR, "open: %s", file);
		goto END3;
	}

	pstat = (dvb_stat_t*)malloc(sizeof(dvb_stat_t));
	pstat->fd_fe = fd_fe;
	pstat->fd_demux = fd_demux;
	pstat->fd = fd;
	pstat->model = check_tuner_model(info.name);

	strncpy(pstat->tuner_name, info.name, sizeof(pstat->tuner_name)-1);

	chinfo->ch_str = channel_str;
	chinfo->sp_str = type_str;
	chinfo->tuner_name = pstat->tuner_name;
	//ci.ch_num = 0;
	//ci.sp_num = 0;

	output_message(MSG_NONE, "DVB Tuner: adapter%d: %s", dvb_dev, info.name);

	*param = pstat;
	return 1;

END3:
	close(fd_demux);
END2:
	close(fd_fe);
END1:
	return 0;
}

static double decode_toshiba_cnr_t(uint16_t s)
{
	double p;
	p = log10(5505024/(double)s) * 10;
	return (0.000024 * p * p * p * p) - (0.0016 * p * p * p) +
		(0.0398 * p * p) + (0.5491 * p) + 3.0965;
}

static double decode_toshiba_cnr_s(uint16_t s)
{
	double p;
	p = sqrt((double)s) / 64;
	return -1.6346*p*p*p*p*p + 14.341*p*p*p*p - 50.259*p*p*p +
		88.977*p*p - 89.565*p + 58.857;
}

static int get_cnr_pt(int fd, double *cnr)
{
	uint16_t s = 0;
	if( ioctl(fd, FE_READ_SNR, &s) != 0 ) {
		output_message(MSG_SYSERROR, "ioctl(FE_READ_SNR, adapter%d)", dvb_dev);
		return 0;
	}
	/* 最新(2016年時点)のPT1/2ドライバ(drivers/media/pci/pt1/va1j5jf8007[ts].c)によれば
	   すでにdBに換算した値が返されている。ただし8ビットの固定小数点になっていると思われるので256で割る。 */
	*cnr = (double)s / 256.0;
	return 1;
}

static int get_cnr_friio_w(int fd, double *cnr)
{
	uint16_t s = 0;
	/* friioはSNRをFE_READ_SIGNAL_STRENGTHで返している */
	if( ioctl(fd, FE_READ_SIGNAL_STRENGTH, &s) != 0 ) {
		output_message(MSG_SYSERROR, "ioctl(FE_READ_SIGNAL_STRENGTH, adapter%d)", dvb_dev);
		return 0;
	}
	*cnr = decode_toshiba_cnr_t(s);
	return 1;
}

static int get_cnr_siglevel(int fd, double *cnr, uint32_t cmd, const char *cmdname, int ignore_err)
{
	int64_t sval;
	int scale;
	struct dtv_property prop[1];
	struct dtv_properties props;

	prop[0].cmd = cmd;
	props.num = 1;
	props.props = prop;

	if( ioctl(fd, FE_GET_PROPERTY, &props) == 0 ) {
		sval = props.props[0].u.st.stat[0].svalue;
		scale = props.props[0].u.st.stat[0].scale;
		if(scale == FE_SCALE_DECIBEL) {
			*cnr = (double)sval / 1000;
		} else if(scale == FE_SCALE_RELATIVE) {
			*cnr = (double)sval / 65535 * 100;
		} else if(scale == FE_SCALE_COUNTER) {
			*cnr = (double)sval;
		} else {
			return 0;
		}
		return 1;
	}
	if(!ignore_err) {
		output_message(MSG_SYSERROR, "ioctl(FE_GET_PROPERTY->%s, adapter%d)",
			cmdname, dvb_dev);
	}
	return 0;
}

static int get_cnr(int fd, double *cnr, int ignore_err)
{
	return get_cnr_siglevel(fd, cnr, DTV_STAT_CNR, "DTV_STAT_CNR", ignore_err);
}

static int get_siglevel(int fd, double *cnr, int ignore_err)
{
	return get_cnr_siglevel(fd, cnr, DTV_STAT_SIGNAL_STRENGTH, "DTV_STAT_SIGNAL_STRENGTH", ignore_err);
}

int get_cnr_oldapi(int fd, double *cnr)
{
	int16_t s = 0;
	if( ioctl(fd, FE_READ_SNR, &s) == 0 ) {
		/* 単位やデコード方法が未知なのでひとまず相対値として返す */
		*cnr = (double)s / (1<<16) * 100;
		return 1;
	}
	return 0;
}

int get_siglevel_oldapi(int fd, double *siglevel)
{
	uint16_t s = 0;
	if( ioctl(fd, FE_READ_SIGNAL_STRENGTH, &s) == 0 ) {
		/* 単位やデコード方法が未知なのでひとまず相対値として返す */
		*siglevel = (double)s / (1<<16) * 100;
		return 1;
	}
	return 0;
}

int get_cnr_default(int fd, double *cnr)
{
	if(get_cnr(fd, cnr, 0)) {
		return 1;
	}
	if(get_cnr_oldapi(fd, cnr)) {
		return 1;
	}
	return 0;
}

int get_siglevel_default(int fd, double *cnr)
{
	if(get_siglevel(fd, cnr, 1)) {
		return 1;
	}
	if(get_siglevel_oldapi(fd, cnr)) {
		return 1;
	}
	return 0;
}

static int hook_stream_generator_siglevel(void *param, double *siglevel)
{
	dvb_stat_t *pstat = (dvb_stat_t*)param;
	
	switch(pstat->model) {
		case DVB_TUNER_PT:
			return 0; /* 非対応 */
		case DVB_TUNER_PT3:
			return get_siglevel(pstat->fd_fe, siglevel, 0);
		case DVB_TUNER_FRIIO_W:
			return 0; /* 非対応 */
		default: /* do nothing */ break;
	}
	return get_siglevel_default(pstat->fd_fe, siglevel);
}

static int hook_stream_generator_cnr(void *param, double *cnr)
{
	dvb_stat_t *pstat = (dvb_stat_t*)param;

	switch(pstat->model) {
		case DVB_TUNER_PT:
			return get_cnr_pt(pstat->fd_fe, cnr);
		case DVB_TUNER_PT3:
			return get_cnr(pstat->fd_fe, cnr, 0);
		case DVB_TUNER_FRIIO_W:
			return get_cnr_friio_w(pstat->fd_fe, cnr);
		default: /* do nothing */ break;
	}
	return get_cnr_default(pstat->fd_fe, cnr);
}

static void hook_stream_generator_close(void *param)
{
	dvb_stat_t *pstat = (dvb_stat_t*)param;
	close(pstat->fd);
	close(pstat->fd_demux);
	close(pstat->fd_fe);
	free(pstat);
}

static hooks_stream_generator_t hooks_stream_generator = {
	hook_stream_generator_open,
	hook_stream_generator,
	hook_stream_generator_wait,
	hook_stream_generator_siglevel,
	hook_stream_generator_cnr,
	hook_stream_generator_close
};

static void register_hooks()
{
	if(enabled) {
		register_hooks_stream_generator(&hooks_stream_generator);
	}
	register_hook_postconfig(hook_postconfig);
}

static const char *set_dev(const char *param)
{
	dvb_dev = atoi(param);
	enabled = 1;
	return NULL;
}

static const char* set_type(const char *param)
{
	enabled = 1;
	if(strcmp("isdb-t", param) == 0) {
		dvb_type = TYPE_ISDB_T;
	} else if(strcmp("bs", param) == 0) {
		dvb_type = TYPE_ISDB_S_BS;
	} else if(strcmp("cs", param) == 0) {
		dvb_type = TYPE_ISDB_S_CS;
	} else {
		return "不明なチャンネルタイプが指定されました";
	}
	return NULL;
}

static const char* set_ch(const char *param)
{
	dvb_ch = atoi(param);
	enabled = 1;
	return NULL;
}

static const char* set_tsnum(const char *param)
{
	dvb_tsnum = atoi(param);
	enabled = 1;
	return NULL;
}

static cmd_def_t cmds[] = {
	{ "--dev", "DVBのデバイス番号", 1, set_dev },
	{ "--type", "DVBのチャンネルタイプ *", 1, set_type },
	{ "--ch", "DVBのチャンネル *", 1, set_ch },
	{ "--tsnum", "DVBのTSナンバー", 1, set_tsnum },
	{ NULL },
};

MODULE_DEF module_def_t mod_dvb = {
	TSDUMP_MODULE_V4,
	"mod_dvb",
	register_hooks,
	cmds
};

