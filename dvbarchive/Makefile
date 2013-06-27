CC=gcc
CFLAGS =  -g -Wall -O2 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
LIBS = -lpthread
OBJS = dvbarchive

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

dvbarchive: dvbarchive.c tune.o diseqc.o freesat_huffman.o
	$(CC) $(INCS) $(CFLAGS) -o dvbarchive dvbarchive.c tune.o diseqc.o freesat_huffman.o $(LIBS)

tune.o: ../dvbtune/tune.c ../dvbtune/tune.h ../dvbtune/dvb_defaults.h
	$(CC) $(INCS) $(CFLAGS) -c -o tune.o ../dvbtune/tune.c

freesat_huffman.o: freesat_huffman.c freesat_huffman.h freesat_tables.c
	$(CC) $(INCS) $(CFLAGS) -c -o freesat_huffman.o freesat_huffman.c

diseqc.o: ../dvbtune/diseqc.c ../dvbtune/diseqc.h
	$(CC) $(INCS) $(CFLAGS) -c -o diseqc.o ../dvbtune/diseqc.c

clean:
	rm -f  *.o *~ $(OBJS)
