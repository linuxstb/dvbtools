CC=gcc
CFLAGS =  -g -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
LIBS = -lpthread
OBJS=dvbstream dumprtp ts_filter rtpfeed rtp.o 

INCS=-I ../DVB/include

ifdef UK
  CFLAGS += -DUK
endif

ifdef FINLAND
  CFLAGS += -DFINLAND
endif

ifdef FINLAND2
  CFLAGS += -DFINLAND2
endif

all: $(OBJS)

dvbstream: dvbstream.c rtp.o tune.o diseqc.o
	$(CC) $(INCS) $(CFLAGS) $(LIBS) -o dvbstream dvbstream.c rtp.o tune.o diseqc.o

dumprtp: dumprtp.c rtp.o 
	$(CC) $(INCS) $(CFLAGS) -o dumprtp dumprtp.c rtp.o

rtpfeed: rtpfeed.c rtp.o 
	$(CC) $(INCS) $(CFLAGS) -o rtpfeed rtpfeed.c rtp.o

rtp.o: rtp.c rtp.h
	$(CC) $(INCS) $(CFLAGS) -c -o rtp.o rtp.c

tune.o: ../dvbtune/tune.c ../dvbtune/tune.h ../dvbtune/dvb_defaults.h
	$(CC) $(INCS) $(CFLAGS) -c -o tune.o ../dvbtune/tune.c

diseqc.o: ../dvbtune/diseqc.c ../dvbtune/diseqc.h
	$(CC) $(INCS) $(CFLAGS) -c -o diseqc.o ../dvbtune/diseqc.c

ts_filter: ts_filter.c
	$(CC) $(INCS) $(CFLAGS) -o ts_filter ts_filter.c

clean:
	rm -f  *.o *~ $(OBJS)
