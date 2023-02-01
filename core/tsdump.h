#define VERSION_STR				TSD_TEXT("1.6.2")
#define DATE_STR				TSD_TEXT("2018/09/08")

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

static inline void replace_sets_proginfo_vars(tsdstr_replace_set_t *sets, size_t *n_sets, const proginfo_t *pi, const ch_info_t *ch_info)
{
	time_mjd_t time_mjd;
	int v_pid, a_pid;

	static TSDCHAR pname[256], chname[256], chname2[256];
	static TSDCHAR year_str[5], mon_str[3], day_str[3], hour_str[3], min_str[3], sec_str[3];
	static TSDCHAR mon_str0[3], day_str0[3], hour_str0[3], min_str0[3], sec_str0[3];
	static TSDCHAR v_pid_str[8], a_pid_str[8], eid_str[8], tsid_str[8], nid_str[8], sid_str[8];

	pname[0] = TSD_NULLCHAR;

	year_str[0] = TSD_NULLCHAR;
	mon_str[0] = TSD_NULLCHAR;
	day_str[0] = TSD_NULLCHAR;
	hour_str[0] = TSD_NULLCHAR;
	min_str[0] = TSD_NULLCHAR;
	sec_str[0] = TSD_NULLCHAR;
	mon_str0[0] = TSD_NULLCHAR;
	day_str0[0] = TSD_NULLCHAR;
	hour_str0[0] = TSD_NULLCHAR;
	min_str0[0] = TSD_NULLCHAR;
	sec_str0[0] = TSD_NULLCHAR;

	eid_str[0] = TSD_NULLCHAR;
	tsid_str[0] = TSD_NULLCHAR;
	nid_str[0] = TSD_NULLCHAR;
	sid_str[0] = TSD_NULLCHAR;

	v_pid_str[0] = TSD_NULLCHAR;
	a_pid_str[0] = TSD_NULLCHAR;

	if (PGINFO_READY(pi->status)) {
		tsd_snprintf(year_str, 5, TSD_TEXT("%d"), pi->start.year);
		tsd_snprintf(mon_str, 3, TSD_TEXT("%d"), pi->start.mon);
		tsd_snprintf(day_str, 3, TSD_TEXT("%d"), pi->start.day);
		tsd_snprintf(hour_str, 3, TSD_TEXT("%d"), pi->start.hour);
		tsd_snprintf(min_str, 3, TSD_TEXT("%d"), pi->start.min);
		tsd_snprintf(sec_str, 3, TSD_TEXT("%d"), pi->start.sec);
		tsd_snprintf(mon_str0, 3, TSD_TEXT("%02d"), pi->start.mon);
		tsd_snprintf(day_str0, 3, TSD_TEXT("%02d"), pi->start.day);
		tsd_snprintf(hour_str0, 3, TSD_TEXT("%02d"), pi->start.hour);
		tsd_snprintf(min_str0, 3, TSD_TEXT("%02d"), pi->start.min);
		tsd_snprintf(sec_str0, 3, TSD_TEXT("%02d"), pi->start.sec);

		tsd_snprintf(eid_str, tsd_sizeof(eid_str), TSD_TEXT("%d"), pi->event_id);
		tsd_strlcpy(pname, pi->event_name.str, 256-1);
	} else if (get_stream_timestamp_rough(pi, &time_mjd)) {
		tsd_snprintf(year_str, 5, TSD_TEXT("%d"), time_mjd.year);
		tsd_snprintf(mon_str, 3, TSD_TEXT("%d"), time_mjd.mon);
		tsd_snprintf(day_str, 3, TSD_TEXT("%d"), time_mjd.day);
		tsd_snprintf(hour_str, 3, TSD_TEXT("%d"), time_mjd.hour);
		tsd_snprintf(min_str, 3, TSD_TEXT("%d"), time_mjd.min);
		tsd_snprintf(sec_str, 3, TSD_TEXT("%d"), time_mjd.sec);
		tsd_snprintf(mon_str0, 3, TSD_TEXT("%02d"), time_mjd.mon);
		tsd_snprintf(day_str0, 3, TSD_TEXT("%02d"), time_mjd.day);
		tsd_snprintf(hour_str0, 3, TSD_TEXT("%02d"), time_mjd.hour);
		tsd_snprintf(min_str0, 3, TSD_TEXT("%02d"), time_mjd.min);
		tsd_snprintf(sec_str0, 3, TSD_TEXT("%02d"), time_mjd.sec);
	}

	if ((pi->status & PGINFO_GET_SERVICE_INFO)) {
		tsd_strlcpy(chname, pi->service_name.str, 256-1);
		if (ch_info) {
			tsd_strlcpy(chname2, pi->service_name.str, 256-1);
		}
		tsd_snprintf(tsid_str, tsd_sizeof(tsid_str), TSD_TEXT("%d"), pi->ts_id);
		tsd_snprintf(nid_str, tsd_sizeof(nid_str), TSD_TEXT("%d"), pi->network_id);
		tsd_snprintf(sid_str, tsd_sizeof(sid_str), TSD_TEXT("%d"), pi->service_id);
	} else if (ch_info) {
		if (ch_info->n_services > 1) {
			tsd_snprintf(chname2, 256, TSD_TEXT("%s(sv=%d)"), ch_info->ch_str, pi->service_id);
		} else {
			tsd_strlcpy(chname2, ch_info->ch_str, 256-1);
		}
	}
	
	v_pid = get_primary_video_pid(pi);
	if (v_pid >= 0) {
		tsd_snprintf(v_pid_str, 8, TSD_TEXT("%d"), v_pid);
	}

	a_pid = get_primary_audio_pid(pi);
	if (a_pid >= 0) {
		tsd_snprintf(a_pid_str, 8, TSD_TEXT("%d"), a_pid);
	}

	if (ch_info) {
		TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{CH2}"), chname2);
	}

	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{CH}"), chname);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{PROG}"), pname);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{Y}"), year_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{M}"), mon_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{D}"), day_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{h}"), hour_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{m}"), min_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{s}"), sec_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{MM}"), mon_str0);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{DD}"), day_str0);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{hh}"), hour_str0);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{mm}"), min_str0);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{ss}"), sec_str0);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{TSID}"), tsid_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{NID}"), nid_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{SID}"), sid_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{EID}"), eid_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{PID_V}"), v_pid_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{PID_A}"), a_pid_str);
}

static inline void replace_sets_date_vars(tsdstr_replace_set_t *sets, size_t *n_sets)
{
	time_t t;
	struct tm lt;
	int year, mon, day, hour, min, sec;

	static TSDCHAR year_str[5], mon_str[3], day_str[3], hour_str[3], min_str[3], sec_str[3];
	static TSDCHAR mon_str0[3], day_str0[3], hour_str0[3], min_str0[3], sec_str0[3];

	t = time(NULL);
#ifdef TSD_PLATFORM_MSVC
	localtime_s(&lt, &t);
#else
	lt = *(localtime(&t));
#endif

	year = lt.tm_year + 1900;
	mon = lt.tm_mon + 1;
	day = lt.tm_mday;
	hour = lt.tm_hour;
	min = lt.tm_min;
	sec = lt.tm_sec;

	tsd_snprintf(year_str, 5, TSD_TEXT("%d"), year);
	tsd_snprintf(mon_str, 3, TSD_TEXT("%d"), mon);
	tsd_snprintf(day_str, 3, TSD_TEXT("%d"), day);
	tsd_snprintf(hour_str, 3, TSD_TEXT("%d"),hour);
	tsd_snprintf(min_str, 3, TSD_TEXT("%d"), min);
	tsd_snprintf(sec_str, 3, TSD_TEXT("%d"), sec);
	tsd_snprintf(mon_str0, 3, TSD_TEXT("%02d"), mon);
	tsd_snprintf(day_str0, 3, TSD_TEXT("%02d"), day);
	tsd_snprintf(hour_str0, 3, TSD_TEXT("%02d"), hour);
	tsd_snprintf(min_str0, 3, TSD_TEXT("%02d"), min);
	tsd_snprintf(sec_str0, 3, TSD_TEXT("%02d"), sec);

	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{YC}"), year_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{MC}"), mon_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{DC}"), day_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{hC}"), hour_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{mC}"), min_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{sC}"), sec_str);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{MMC}"), mon_str0);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{DDC}"), day_str0);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{hhC}"), hour_str0);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{mmC}"), min_str0);
	TSD_REPLACE_ADD_SET(sets, *n_sets, TSD_TEXT("{ssC}"), sec_str0);
}

void ghook_message(const TSDCHAR *modname, message_type_t msgtype, tsd_syserr_t *err, const TSDCHAR *msg);