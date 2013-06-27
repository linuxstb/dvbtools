INCS=-I /usr/include/libxml2
CFLAGS= -Wall $(INCS)
LDFLAGS=-lxml2
CC=gcc
all: dvbtune xml2vdr

INCS += -I ../DVB/include

ifdef UK
  CFLAGS += -DUK
endif

ifdef FINLAND
  CFLAGS += -DFINLAND
endif

ifdef FINLAND2
  CFLAGS += -DFINLAND2
endif

tune.o: tune.c tune.h dvb_defaults.h
si.o: si.c si.h
diseqc.o: diseqc.c diseqc.h

dvbtune.o: dvbtune.c tune.h si.h dvb_defaults.h

dvbtune: dvbtune.o tune.o si.o diseqc.o

xml2vdr: xml2vdr.c

clean:
	rm -f *.o *~ dvbtune xml2vdr xml2newcscam
