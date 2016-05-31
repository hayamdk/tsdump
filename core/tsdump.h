#define VERSION_STR				TSD_TEXT("1.5.0-dev")
#define DATE_STR				TSD_TEXT("2016/xx/xx")

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
#ifdef TSD_PLATFORM_MSVC
	struct _timeb tv;
	_ftime64_s(&tv);
#else
	struct timeb tv;
	ftime(&tv);
#endif
	result = (int64_t)tv.time * 1000;
	result += tv.millitm;

	return result;
}

static inline int64_t timenum_timemjd(const time_mjd_t timemjd)
{
	int64_t tn;
	tn = timemjd.year;
	tn *= 100;
	tn += timemjd.mon;
	tn *= 100;
	tn += timemjd.day;
	tn *= 100;
	tn += timemjd.hour;
	tn *= 100;
	tn += timemjd.min;
	//tn *= 100;
	//tn += timemjd.sec;
	return tn;
}

static inline int64_t timenum_start(const proginfo_t *pi)
{
	if (pi->status & PGINFO_UNKNOWN_STARTTIME) {
		return 0;
	}
	return timenum_timemjd(pi->start);
}

static inline int64_t timenumtt(time_t t)
{
	int64_t tn;
	struct tm lt;
	
#ifdef TSD_PLATFORM_MSVC
	localtime_s(&lt, &t);
#else
	lt = *(localtime(&t));
#endif

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

#ifdef TSD_PLATFORM_MSVC
	localtime_s(&lt, &tt);
#else
	lt = *(localtime(&tt));
#endif

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

void ghook_message(const TSDCHAR *modname, message_type_t msgtype, tsd_syserr_t *err, const TSDCHAR *msg);