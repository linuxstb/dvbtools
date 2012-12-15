#ifndef _DVBTSCUT_H
#define _DVBTSCUT_H

typedef struct {
  unsigned short pid;
  uint64_t first_PTS;
  int payload_start;
  uint64_t PTS;
  int is_pes;
  int synced;
  int ts_missing;
  int packet;
  int stream_packets;
  int counter;
  int displayed_header;
} stream_info_t;

#endif
