PROGRAM = tsdump

SOURCES_CP932 = core/tsdump.c core/ts_output.c core/load_modules.c core/default_decoder.c utils/tsdstr.c utils/ts_parser.c utils/path.c
MODULES_CP932 = modules/mod_path_resolver.c modules/mod_log.c modules/mod_filein.c modules/mod_fileout.c
SOURCES = utils/aribstr.c
MODULES = modules/mod_dvb.c modules/mod_arib25.c

CC := gcc

OBJS1 = $(SOURCES_CP932:.c=.o) $(MODULES_CP932:.c=.o)
OBJS2 = $(SOURCES:.c=.o) $(MODULES:.c=.o)
OBJS = $(OBJS1) $(OBJS2)

#CFLAGS = -O3 -flto -I$(CURDIR)
CFLAGS = -Ofast -march=native -flto -I$(CURDIR)
#CFLAGS = -O0 -Wall -g -I$(CURDIR)
LDFLAGS = -flto -lm -larib25 #-liconv
#LDFLAGS = #-liconv

$(PROGRAM): $(OBJS)
	$(CC) $(OBJS) $(SPECS) $(LDFLAGS) -o $(PROGRAM)

#SUFFIXES: .o .c

.c.o:
	$(CC) $(CFLAGS) $(CHARSET_FLAG) -c $< -o $@

$(OBJS1): CHARSET_FLAG = -finput-charset=cp932

$(OBJS2): CHARSET_FLAG = 

.PHONY: clean

clean:
	rm -f $(PROGRAM) $(OBJS)
