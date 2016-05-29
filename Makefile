PROGRAM = tsdump

SOURCES_CP932 = core/tsdump.c
SOURCES = core/mod_fileoutput_unix.c

CC := gcc

OBJS1 = $(SOURCES_CP932:.c=.o)
OBJS2 = $(SOURCES:.c=.o)

CFLAGS = -O -I$(CURDIR)
LDFLAGS = 

$(PROGRAM): $(OBJS1)
	$(CC) $(OBJS1) $(SPECS) $(LDFLAGS) -o $(PROGRAM)

SUFFIXES: .o .c

.c.o:
ifeq ($(findstring $<, $(SOURCES_CP932)), $<)
	$(CC) $(CFLAGS) -finput-charset=cp932 -c $<
else
	$(CC) $(CFLAGS) -c $<
endif

.PHONY: clean

clean:
	rm -f $(PROGRAM) $(OBJS)
