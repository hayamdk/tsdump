#define VERSION_STR				"1.4.0-dev"
#define DATE_STR				"2016/xx/xx"

#define BUFSIZE_DEFAULT			96
#define OVERLAP_SEC_DEFAULT		15
#define CHECK_INTERVAL_DEFAULT	250
#define MAX_PGOVERLAP_DEFAULT	4

#define MAX_SERVICES			32

#define MAX_WRITE_SIZE			188*16*1024
#define MIN_CLEAR_RATIO			0.05

extern int CHECK_INTERVAL;
extern int OVERLAP_SEC;
extern int BUFSIZE;
extern int MAX_PGOVERLAP;

static inline int64_t gettime()
{
	int64_t result;
	struct _timeb tv;

	_ftime64_s(&tv);
	result = (int64_t)tv.time * 1000;
	result += tv.millitm;

	return result;
}

static inline int64_t timenum_end_sec(const proginfo_t *pi, int *psec)
{
	int64_t tn;
	struct tm t, te;
	time_t tt;
	int sec, min, hour, day_diff;

	if (pi->status & PGINFO_UNKNOWN_STARTTIME || pi->status & PGINFO_UNKNOWN_DURATION) {
		if (psec) {
			*psec = 0;
		}
		return 0;
	}

	sec = pi->start.sec + pi->dur_sec;
	min = pi->start.min + pi->dur_min;
	hour = pi->start.hour + pi->dur_hour;
	day_diff = 0;

	if (sec >= 60) {
		sec -= 60;
		min += 1;
	}
	if (min >= 60) {
		min -= 60;
		hour += 1;
	}
	if (hour >= 24) {
		hour -= 24;
		day_diff += 1;
	}

	memset(&t, 0, sizeof(struct tm));
	t.tm_mday = pi->start.day;
	t.tm_mon = pi->start.mon - 1;
	t.tm_year = pi->start.year - 1900;

	tt = mktime(&t);
	if (tt != -1) {
		tt += day_diff * 60 * 60 * 24;
		localtime_s(&te, &tt);
		tn = te.tm_year + 1900;
		tn *= 100;
		tn += te.tm_mon + 1;
		tn *= 100;
		tn += te.tm_mday;
	} else {
		tn = 99990101;
	}

	tn *= 100;
	tn += hour;
	tn *= 100;
	tn += min;
	//tn *= 100;
	//tn += sec;
	if (psec) {
		*psec = sec;
	}
	return tn;
}

static inline int64_t timenum_end(const proginfo_t *pi)
{
	return timenum_end_sec(pi, NULL);
}

static inline int64_t timenum_end14(const proginfo_t *pi)
{
	int64_t t;
	int sec;
	t = timenum_end_sec(pi, &sec);
	return t * 100 + sec;
}

static inline int64_t timenum_start(const proginfo_t *pi)
{
	int64_t tn;

	if (pi->status & PGINFO_UNKNOWN_STARTTIME) {
		return 0;
	}

	tn = pi->start.year;
	tn *= 100;
	tn += pi->start.mon;
	tn *= 100;
	tn += pi->start.day;
	tn *= 100;
	tn += pi->start.hour;
	tn *= 100;
	tn += pi->start.min;
	//tn *= 100;
	//tn += pi->recsec;
	return tn;
}

static inline int64_t timenumtt(time_t t)
{
	int64_t tn;
	struct tm lt;
	
	localtime_s(&lt, &t);

	tn = lt.tm_year + 1900;
	tn *= 100;
	tn += (lt.tm_mon + 1);
	tn *= 100;
	tn += lt.tm_mday;
	tn *= 100;
	tn += lt.tm_hour;
	tn *= 100;
	tn += lt.tm_min;
	return tn;
}

static inline int64_t timenum64(int64_t ms)
{
	int64_t tn;
	struct tm lt;

	time_t tt = ms / 1000;

	localtime_s(&lt, &tt);

	tn = lt.tm_year + 1900;
	tn *= 100;
	tn += (lt.tm_mon + 1);
	tn *= 100;
	tn += lt.tm_mday;
	tn *= 100;
	tn += lt.tm_hour;
	tn *= 100;
	tn += lt.tm_min;
	return tn;
}

static inline int64_t timenumnow()
{
	time_t t = time(NULL);
	return timenumtt(t);
}

extern int param_nowait;

void ghook_message(const WCHAR *modname, message_type_t msgtype, DWORD *err, const WCHAR *msg);