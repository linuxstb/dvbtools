#ifndef _SI_H
#define _SI_H

#define SECA_CA_SYSTEM          0x0100
#define VIACCESS_CA_SYSTEM      0x0500
#define IRDETO_CA_SYSTEM        0x0600
#define VIDEOGUARD_CA_SYSTEM    0x0900
#define BETA_CA_SYSTEM          0x1700
#define NAGRA_CA_SYSTEM         0x1800
#define CONAX_CA_SYSTEM         0x0b00

typedef struct _transponder_t {
  int id;
  int onid;
  unsigned int freq;
  int srate;
  int pos;
  int we_flag;
  char pol;
  int mod;

  int scanned;
  struct _transponder_t* next;
} transponder_t;


void add_transponder(transponder_t* transponder, transponder_t** transponders);
void parse_descriptors(int info_len,unsigned char *buf, transponder_t* transponder, transponder_t** transponders);
transponder_t*  get_unscanned(transponder_t* transponders);

#endif
