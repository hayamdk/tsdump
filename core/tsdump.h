#define VERSION_STR				TSD_TEXT("1.6.1")
#define DATE_STR				TSD_TEXT("2017/06/15")

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
extern int MAX_OUTPUT_DELAY_SEC;
extern int MAX_CLOSE_DELAY_SEC;

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

static inline int64_t make_timenum(int year, int mon, int day, int hour, int min)
{
	int64_t tn;
	tn = year;
	tn *= 100;
	tn += mon;
	tn *= 100;
	tn += day;
	tn *= 100;
	tn += hour;
	tn *= 100;
	tn += min;
	return tn;
}

static inline int64_t timenum_timemjd(const time_mjd_t *timemjd)
{
	return make_timenum(timemjd->year, timemjd->mon, timemjd->day, timemjd->hour, timemjd->min);
}

static inline int64_t timenum_start(const proginfo_t *pi)
{
	if (pi->status & PGINFO_UNKNOWN_STARTTIME) {
		return 0;
	}
	return timenum_timemjd(&pi->start);
}

static inline int64_t timenumtt(time_t t)
{
	struct tm lt;
	
#ifdef TSD_PLATFORM_MSVC
	localtime_s(&lt, &t);
#else
	lt = *(localtime(&t));
#endif
	return make_timenum(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
}

static inline int64_t timenum64(int64_t ms)
{
	struct tm lt;
	time_t tt = ms / 1000;

#ifdef TSD_PLATFORM_MSVC
	localtime_s(&lt, &tt);
#else
	lt = *(localtime(&tt));
#endif
	return make_timenum(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
}

static inline int64_t timenumnow()
{
	time_t t = time(NULL);
	return timenumtt(t);
}

void ghook_message(const TSDCHAR *modname, message_type_t msgtype, tsd_syserr_t *err, const TSDCHAR *msg);