#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "ts_parser.h"

int64_t ts_n_drops = 0;
int64_t ts_n_total = 0;
static int ts_counter[0x2000] = {};

static inline const int64_t ts_drop_counter(unsigned char *packet)
{
	ts_n_total++;

	if (packet[0] != 0x47) {
		ts_n_drops++;
		goto END;
	}

	unsigned int pid = ts_get_pid(packet);
	unsigned int counter = ts_get_continuity_counter(packet);
	unsigned int counter_should_be;

	if (pid == 0x1fff) { /* null packet */
		goto END;
	}

	if (ts_counter[pid] != 0) { /* == 0 : initialized */
		if (ts_have_payload(packet)) {
			counter_should_be = (ts_counter[pid] + 1) % 16;
		}
		else {
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

int default_decoder(unsigned char **decbuf, int *n_decbuf, const unsigned char *buf, int n_buf)
{
	static unsigned char *tmpbuf = NULL;
	static int n_tmpbuf = 0;
	static unsigned char rem[188];
	static int n_rem = 0;

	//*decbuf = buf;
	//*n_decbuf = n_buf;
	//return 1;

	//n_rem = 0;

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
		printf("[WARN] skipped %d bytes\n", i);
	}

	/* DROP”‚ðƒJƒEƒ“ƒg */
	int c;
	for (c = 0; c < (int)n_dec; c += 188) {
		ts_drop_counter(&(*decbuf)[c]);
	}

//	if (n_rem != 0) {
		//printf("n=%d rem=%d\n", n, n_rem);
//	}

	return 1;
}