//#include <stdio.h>
//#include <stdint.h>
//#include <inttypes.h>
//#include <time.h>
//#include <string.h>
//#include <stdlib.h>
//#include <sys/types.h>
#include <math.h>

#define __STDC_FORMAT_MACROS

#define TC_MAX_RECS 64
#define TC_MAX_STACKS 32

typedef struct {
    const char *name;
    __int64 sum;
    double sum_x2;
    int count;
    __int64 min;
    __int64 max;
}tcrec_t;

typedef struct {
    int r;
    __int64 lasttime;
}trec_stack_t;

#ifdef HAVE_TIMECALC_DECLARATION
tcrec_t tcrecs[TC_MAX_RECS];
int num_tcrecs = 1;
trec_stack_t tcstacks[TC_MAX_STACKS];
int num_tcstacks = 0;
__int64 freq = 0;
#else
extern tcrec_t tcrecs[TC_MAX_RECS];
extern int num_tcrecs;
extern trec_stack_t tcstacks[TC_MAX_STACKS];
extern int num_tcstacks;
extern __int64 freq;
#endif

static inline int tc_getrec(const char *name)
{
    int i;
    if( name == NULL ) {
        return 0;
    }
    for(i=1; i<num_tcrecs; i++) {
        if( strcmp(tcrecs[i].name, name) == 0 ) {
            return i;
        }
    }
    return -1;
}

static inline unsigned __int64 tc_gettime()
{
	LARGE_INTEGER li = {};
    unsigned __int64 t;
	if (freq == 0) {
		LARGE_INTEGER lt;
		QueryPerformanceFrequency(&lt);
		freq = (unsigned __int64)lt.QuadPart;
	}
	QueryPerformanceCounter(&li);
	t = (unsigned __int64)li.QuadPart * 1000 * 1000 / freq;
    return t;
}

static inline __int64 tc_getsum(const char *name)
{
    int i;
    i = tc_getrec(name);
    if( i < 0 ) {
        return -1;
    } else {
        return tcrecs[i].sum;
    }
}

static inline void tc_start(const char *name)
{
    int i;
    i = tc_getrec(name);
    if( i < 0 ) {
        if( num_tcrecs >= TC_MAX_RECS ) {
            fprintf(stderr,"[ERROR] tc_start(): too much records\n");
            *((char*)NULL) = '\0';
        }
        i = num_tcrecs;
        tcrecs[i].name = name;
        tcrecs[i].sum = 0;
        tcrecs[i].sum_x2 = 0;
        tcrecs[i].count = 0;
        num_tcrecs++;
    }
    
    if( num_tcstacks >= TC_MAX_STACKS ) {
        fprintf(stderr,"[ERROR] tc_start(): stack overflow\n");
        *((char*)NULL) = '\0';
    }
    
    tcstacks[num_tcstacks].lasttime = tc_gettime();
    tcstacks[num_tcstacks].r = i;
    num_tcstacks++;
}

static inline unsigned __int64 tc_end()
{
    int i;
    __int64 tdiff, t = tc_gettime();
    
    if( num_tcstacks <= 0 ) {
        fprintf(stderr,"[ERROR] tc_end(): stack underflow\n");
        *((char*)NULL) = '\0';
    }
    num_tcstacks--;
    
    i = tcstacks[num_tcstacks].r;
    tdiff = t - tcstacks[num_tcstacks].lasttime;
    tcrecs[i].sum += tdiff;
    tcrecs[i].sum_x2 += ((double)tdiff * (double)tdiff);
    
    if( tcrecs[i].count == 0 ) {
        tcrecs[i].min = tcrecs[i].max = tdiff;
    } else if( tcrecs[i].min > tdiff ) {
        tcrecs[i].min = tdiff;
    } else if( tcrecs[i].max < tdiff ) {
        tcrecs[i].max = tdiff;
    }
    
    tcrecs[i].count++;
    
    return tdiff;
}

static void tc_report_fp(FILE *fp)
{
    int i, csum = 0;
    __int64 sum = 0;
    double ave, var;
    fprintf(fp, "%20s  %8s %15s %15s %15s %15s %15s %20s\n",
        "", "called", "usec", "ave.", "min", "max", "sqrt(var.)", "var.");
    for(i=0; i<num_tcrecs; i++) {
        if( i==0 && tcrecs[i].count == 0 ) {
            continue;
        }
        ave = (double)tcrecs[i].sum / (double)tcrecs[i].count;
        var = tcrecs[i].sum_x2 / (double)tcrecs[i].count - ave * ave;
        fprintf(fp, "%20s: %8d %15lld %15.0f %15lld %15lld %15.0f %20.0f\n",
            tcrecs[i].name, tcrecs[i].count, (long long int)tcrecs[i].sum, 
            ave, (long long int)tcrecs[i].min, (long long int)tcrecs[i].max, sqrt(var), var );
        sum += tcrecs[i].sum;
        csum += tcrecs[i].count;
    }
    fprintf(fp, "%20s: %8d %15lld\n","total", csum, (long long int)sum);
}

static void tc_report_fn(const char *fn)
{
    FILE *fp;
    fp = fopen(fn,"w");
    if( fp == NULL ) {
        fprintf(stderr, "can't open report file: '%s'\n", fn);
        return;
    }
    tc_report_fp(fp);
    fclose(fp);
}

static void tc_report_id(int id)
{
    char fn[1024];
    _snprintf(fn, 1022, "tcreport_%d.txt", id);
    tc_report_fn(fn);
}

static void tc_report()
{
    tc_report_fp(stdout);
}
