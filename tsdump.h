#define VERSION_STR				"1.2.7-dev"
#define DATE_STR				"2015/x/x"

#define BUFSIZE_DEFAULT			96
#define OVERLAP_SEC_DEFAULT		15
#define CHECK_INTERVAL_DEFAULT	500
#define MAX_PGOVERLAP_DEFAULT	4

#define MAX_SERVICES			32

extern int CHECK_INTERVAL;
extern int OVERLAP_SEC;
extern int BUFSIZE;
extern int MAX_PGOVERLAP;

#define MAX_WRITE_SIZE			188*16*1024
#define MIN_CLEAR_RATIO			0.05

static inline int64_t gettime()
{
	int64_t result;
	_timeb tv;

	_ftime64_s(&tv);
	result = (int64_t)tv.time * 1000;
	result += tv.millitm;

	return result;
}

static inline int64_t timenum_end(ProgInfo *pi)
{
	int64_t tn;
	struct tm t, te;
	time_t tt;
	int sec, min, hour, day_diff;

	sec = pi->recsec + pi->dursec;
	min = pi->recmin + pi->durmin;
	hour = pi->rechour + pi->durhour;
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
	t.tm_mday = pi->recday;
	t.tm_mon = pi->recmonth - 1;
	t.tm_year = pi->recyear - 1900;

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
	return tn;
}

static inline int64_t timenum_start(ProgInfo *pi)
{
	int64_t tn;
	tn = pi->recyear;
	tn *= 100;
	tn += pi->recmonth;
	tn *= 100;
	tn += pi->recday;
	tn *= 100;
	tn += pi->rechour;
	tn *= 100;
	tn += pi->recmin;
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

static inline int64_t timenumnow()
{
	time_t t = time(NULL);
	return timenumtt(t);
}

extern WCHAR param_base_dir[MAX_PATH_LEN];
extern int param_all_services;
extern int param_services[MAX_SERVICES];
extern int param_n_services;
extern int param_nodec;
extern int param_nowait;