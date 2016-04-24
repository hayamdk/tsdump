#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <Windows.h>

#include "core/tsdump_def.h"
#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"
#include "utils/ts_parser.h"

int64_t ts_n_drops = 0;
int64_t ts_n_total = 0;
static int ts_counter[0x2000] = {0};

static inline const int64_t ts_drop_counter(uint8_t *packet)
{
	ts_header_t tsh;
	ts_n_total++;

	parse_ts_header(packet, &tsh);
	if (!tsh.valid_sync_byte) {
		ts_n_drops++;
		goto END;
	}

	unsigned int pid = tsh.pid;
	unsigned int counter = tsh.continuity_counter;
	unsigned int counter_should_be;

	if (pid == 0x1fff) { /* null packet */
		goto END;
	}

	if (ts_counter[pid] != 0) { /* == 0 : initialized */
		if (tsh.adaptation_field_control & 0x01) {
			/* have payload */
			counter_should_be = (ts_counter[pid] + 1) % 16;
		} else {
			counter_should_be = ts_counter[pid] % 16;
		}

		if (counter_should_be != counter) {
			ts_n_drops++;
		}
	}

	ts_counter[pid] = counter + 16; /* offset 16 */

END:
	return ts_n_drops;
}

/* 188バイトアラインではないストリームを送ってくるBonDriver（たとえばFriio）に対応するためのダミーデコーダ */
void default_decoder(unsigned char **decbuf, int *n_decbuf, const unsigned char *buf, int n_buf)
{
	static unsigned char *tmpbuf = NULL;
	static int n_tmpbuf = 0;
	static unsigned char rem[188];
	static int n_rem = 0;

	//*decbuf = buf;
	//*n_decbuf = n_buf;
	//return 1;

	//n_rem = 0;

	if (n_buf == 0) {
		*decbuf = NULL;
		*n_decbuf = 0;
		return;
	}

	int n = n_rem + n_buf;

	if (n > n_tmpbuf) {
		if (tmpbuf != NULL) {
			free(tmpbuf);
		}
		tmpbuf = (unsigned char*)malloc(n);
		n_tmpbuf = n;
	}

	memcpy(tmpbuf, rem, n_rem);
	memcpy(&tmpbuf[n_rem], buf, n_buf);

	int i = 0;
	while (tmpbuf[i] != 0x47 && i < n && i < 188) {
		i++;
	}

	int n_dec = (n - i) / 188 * 188;

	n_rem = n - i - n_dec;
	memcpy(rem, &tmpbuf[i + n_dec], n_rem);

	*decbuf = &tmpbuf[i];
	*n_decbuf = n_dec;

	if (i != 0) {
		output_message(MSG_WARNING, L"skipped %d bytes", i);
	}

	/* DROP数をカウント */
	int c;
	for (c = 0; c < (int)n_dec; c += 188) {
		ts_drop_counter(&(*decbuf)[c]);
	}

//	if (n_rem != 0) {
		//printf("n=%d rem=%d\n", n, n_rem);
//	}
}