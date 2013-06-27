/* 

dvbstream - RTP-ize a DVB transport stream.
(C) Dave Chapman <dave@dchapman.com> 2001, 2002.

The latest version can be found at http://www.linuxstb.org/dvbstream

Copyright notice:

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
    
*/


// Linux includes:
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <resolv.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <values.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>

// DVB includes:
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include "rtp.h"

#include "../dvbtune/tune.h"

#define USAGE "\nUSAGE: dvbstream tpid1 tpid2 tpid3 .. tpid8\n\n"
#define PACKET_SIZE 188

// How often (in seconds) to update the "now" variable
#define ALARM_TIME 5

/* Thanks to Giancarlo Baracchino for this fix */
#define MTU 1500
#define IP_HEADER_SIZE 20
#define UDP_HEADER_SIZE 8
#define RTP_HEADER_SIZE 12

#define MAX_RTP_SIZE (MTU-IP_HEADER_SIZE-UDP_HEADER_SIZE-RTP_HEADER_SIZE)

/* Number of separate files in "map" output mode */
#define MAX_CHANNELS 64

#define writes(f,x) write((f),(x),strlen(x))

/* Signal handling code shamelessly copied from VDR by Klaus Schmidinger 
   - see http://www.cadsoft.de/people/kls/vdr/index.htm */

#define MAX_CARDS 8
char* frontenddev[MAX_CARDS]={"/dev/dvb/adapter0/frontend0","/dev/dvb/adapter1/frontend0","/dev/dvb/adapter2/frontend0","/dev/dvb/adapter3/frontend0","/dev/dvb/adapter4/frontend0","/dev/dvb/adapter5/frontend0","/dev/dvb/adapter6/frontend0","/dev/dvb/adapter7/frontend0"};
char* dvrdev[MAX_CARDS]={"/dev/dvb/adapter0/dvr0","/dev/dvb/adapter1/dvr0","/dev/dvb/adapter2/dvr0","/dev/dvb/adapter3/dvr0","/dev/dvb/adapter4/dvr0","/dev/dvb/adapter5/dvr0","/dev/dvb/adapter6/dvr0","/dev/dvb/adapter7/dvr0"};
char* demuxdev[MAX_CARDS]={"/dev/dvb/adapter0/demux0","/dev/dvb/adapter1/demux0","/dev/dvb/adapter2/demux0","/dev/dvb/adapter3/demux0","/dev/dvb/adapter4/demux0","/dev/dvb/adapter5/demux0","/dev/dvb/adapter6/demux0","/dev/dvb/adapter7/demux0"};

int card=0;
long now;
long real_start_time;
int Interrupted=0;
fe_spectral_inversion_t specInv=INVERSION_AUTO;
int tone=-1;
fe_modulation_t modulation=CONSTELLATION_DEFAULT;
fe_transmit_mode_t TransmissionMode=TRANSMISSION_MODE_DEFAULT;
fe_bandwidth_t bandWidth=BANDWIDTH_DEFAULT;
fe_guard_interval_t guardInterval=GUARD_INTERVAL_DEFAULT;
fe_code_rate_t HP_CodeRate=HP_CODERATE_DEFAULT;
fe_delivery_system_t sys = SYS_DVBS;
fe_code_rate_t fec = FEC_AUTO;
unsigned int diseqc=1;
char pol=0;

typedef struct {
  char *filename;
  int fd;
  int pids[MAX_CHANNELS];
  int num;
  int pid_cnt;
  int nCurrentSeq;
  long start_time; // in seconds
  long end_time;   // in seconds
  int socket;
  struct rtpheader hdr;
  struct sockaddr_in sOut;
  unsigned char buf[MTU];
  unsigned char net[20];
  int pos;
  int port;
} pids_map_t;

pids_map_t *pids_map;
int map_cnt;


int nCheckingCont = 0;
int nDropNullPid = 0;
int nTotalContErrors = 0;
int nPacketsSinceLastFileChange = 0;
unsigned long long nTotalPackets = 0ULL;
unsigned long long nFileSizeRotate = 0ULL;
pthread_mutex_t muDataToFile;
pthread_cond_t muconDataToFile;

#define TS_SIZE 188
#define IN_SIZE TS_SIZE

#define MAX_EXCHANGE_ENTRIES 1024000

struct EXCHANGE_BUFFER {
  int nEntries;
  unsigned char pszBuffer[MAX_EXCHANGE_ENTRIES][TS_SIZE];
};

struct EXCHANGE_BUFFER *pExchangeBuffer = NULL;
int nGlobalHighWaterMark = 0;
int nGlobalTEI = 0;

struct MESSAGE {
  char szMessage[128];
  struct MESSAGE *pNext;
};

struct MESSAGE *pMessageExchange = NULL;

void make_nonblock(int f) {
  int oldflags;

  if ((oldflags=fcntl(f,F_GETFL,0)) < 0) {
    perror("F_GETFL");
  }
  oldflags|=O_NONBLOCK;
  if (fcntl(f,F_SETFL,oldflags) < 0) {
    perror("F_SETFL");
  }
}

void *FileWriterFunc(void *pszArg) {
  struct EXCHANGE_BUFFER *pszLocalBuffer;

  unsigned char nCurrentCont[8192];
  int i;

  /* Initialise PID map */
  for (i=0;i<8192;i++) {
    nCurrentCont[i] = 0xff;
  }
  int counter;
  int nHighWaterMark = 0;
  int nMessages = 0;
  int nTEI = 0;
  struct MESSAGE *pNewMessage, *pMessageHead = NULL, *pMessageTail = NULL;
  while (1) {
    pthread_mutex_lock(&muDataToFile);
    pthread_cond_wait(&muconDataToFile, &muDataToFile);
    pszLocalBuffer = pExchangeBuffer;
    pExchangeBuffer = NULL;
    if (NULL == pMessageExchange) {
      pMessageExchange = pMessageHead;
      pMessageHead = NULL;
      pMessageTail = NULL;
      nMessages = 0;
    }
    nGlobalTEI = nTEI;
    nGlobalHighWaterMark = nHighWaterMark;
    pthread_mutex_unlock(&muDataToFile);
    if (pszLocalBuffer != NULL) {
      if (pszLocalBuffer->nEntries > nHighWaterMark) {
        nHighWaterMark = pszLocalBuffer->nEntries;
      }
      for (counter = 0; counter < pszLocalBuffer->nEntries; counter++) {
        unsigned char *buf = pszLocalBuffer->pszBuffer[counter];
        if(buf[0] == 0x47) {
          int pid, i, j, cont, tei, conterror = 0;

          pid = ((buf[1] & 0x1f) << 8) | buf[2];
          cont = buf[3] & 0xf;
          tei = (buf[1] & 0x80) >> 7;
          if (tei) {
            nTEI++;
            continue;
          }
          if (nCurrentCont[pid] == 0xff) {
            nCurrentCont[pid] = cont;
            pNewMessage = malloc(sizeof(struct MESSAGE));
            pNewMessage->pNext = NULL;
            sprintf(pNewMessage->szMessage, "Set first continuity for pid %d to %d\n", pid, cont);
            if (pMessageHead == NULL) {
              pMessageHead = pNewMessage;
              pMessageTail = pNewMessage;
            } else {
              pMessageTail->pNext = pNewMessage;
              pMessageTail = pNewMessage;
            }
            nMessages++;
          } else {
            if (((nCurrentCont[pid] + 1) % 0x10) != cont && ((nCurrentCont[pid]) % 0x10) != cont&& pid != 0x1fff) { // Aparently getting the same cont in a row is OK.
              // Continuity error
              pNewMessage = malloc(sizeof(struct MESSAGE));
              pNewMessage->pNext = NULL;
              sprintf(pNewMessage->szMessage, "Continuity error on PID %d(0x%04x). Jump from %d to %d\n", pid, pid, nCurrentCont[pid], cont);
              if (pMessageHead == NULL) {
                pMessageHead = pNewMessage;
                pMessageTail = pNewMessage;
              } else {
                pMessageTail->pNext = pNewMessage;
                pMessageTail = pNewMessage;
              }
              nMessages++;
              conterror = 1;
              nTotalContErrors++;
            }
            nCurrentCont[pid] = cont;
          }
          nPacketsSinceLastFileChange++;
          if (pid != 0x1fff || (pid == 0x1fff && nDropNullPid == 0)) {
            if (pids_map != NULL) {
              for (i = 0; i < map_cnt; i++) {
                if ( ((pids_map[i].start_time==-1) || (pids_map[i].start_time <= now))
                  && ((pids_map[i].end_time==-1) || (pids_map[i].end_time >= now))) {
                    for (j = 0; j < MAX_CHANNELS; j++) {
                      if (pids_map[i].pids[j] == pid || pids_map[i].pids[j] == 8192) {
                        errno = 0;
                        if ((conterror && nCheckingCont) || (nCheckingCont && nFileSizeRotate != 0 && nPacketsSinceLastFileChange * 188ULL > nFileSizeRotate)) {
                          FILE *f;

                          if (nPacketsSinceLastFileChange > 10000) {
                            pids_map[i].nCurrentSeq++;
                            char *pszTempFilename = malloc(strlen(pids_map[i].filename) + 5);
                            nPacketsSinceLastFileChange = 0;
                            sprintf(pszTempFilename, pids_map[i].filename, pids_map[i].nCurrentSeq);
                            pNewMessage = malloc(sizeof(struct MESSAGE));
                            pNewMessage->pNext = NULL;
                            sprintf(pNewMessage->szMessage, "Closing old file and opening file %s\n", pszTempFilename);
                            if (pMessageHead == NULL) {
                              pMessageHead = pNewMessage;
                              pMessageTail = pNewMessage;
                            } else {
                              pMessageTail->pNext = pNewMessage;
                              pMessageTail = pNewMessage;
                            }
                            nMessages++;
                            close(pids_map[i].fd);
                            f = fopen(pszTempFilename, "w+b");
                            free(pszTempFilename);
                            pids_map[i].fd = fileno(f);
                            make_nonblock(pids_map[i].fd);
                          } else {
                            char *pszTempFilename = malloc(strlen(pids_map[i].filename) + 5);
                            sprintf(pszTempFilename, pids_map[i].filename, pids_map[i].nCurrentSeq);
                            nPacketsSinceLastFileChange = 0;
                            lseek(pids_map[i].fd, 0, SEEK_SET);
                            pNewMessage = malloc(sizeof(struct MESSAGE));
                            pNewMessage->pNext = NULL;
                            sprintf(pNewMessage->szMessage, "Truncating file %s\n", pszTempFilename);
                            if (pMessageHead == NULL) {
                              pMessageHead = pNewMessage;
                              pMessageTail = pNewMessage;
                            } else {
                              pMessageTail->pNext = pNewMessage;
                              pMessageTail = pNewMessage;
                            }
                            nMessages++;
                            free(pszTempFilename);
                          }
                        }
                        write(pids_map[i].fd, pszLocalBuffer->pszBuffer[counter], TS_SIZE);

                        //write(pids_map[i].fd, buf, TS_SIZE);
                      }
                    }
                }
              }
            }
          }
        } else {
          pNewMessage = malloc(sizeof(struct MESSAGE));
          pNewMessage->pNext = NULL;
          sprintf(pNewMessage->szMessage, "NON 0X47\n");
          if (pMessageHead == NULL) {
            pMessageHead = pNewMessage;
            pMessageTail = pNewMessage;
          } else {
            pMessageTail->pNext = pNewMessage;
            pMessageTail = pNewMessage;
          }
          nMessages++;
        }
      }
    }
    free(pszLocalBuffer);
  }
  pthread_exit(NULL);
}



int open_fe(int* fd_frontend) {

    if((*fd_frontend = open(frontenddev[card],O_RDWR | O_NONBLOCK)) < 0){
        perror("FRONTEND DEVICE: ");
        return -1;
    }
    return 1;
}

static void SignalHandler(int signum) {
  struct timeval tv;

  if (signum == SIGALRM) {
    gettimeofday(&tv,(struct timezone*) NULL);
    now=tv.tv_sec-real_start_time;
    alarm(ALARM_TIME);
  } else if (signum != SIGPIPE) {
    Interrupted=signum;
  }
  signal(signum,SignalHandler);
}

long getmsec() {
  struct timeval tv;
  gettimeofday(&tv,(struct timezone*) NULL);
  return(tv.tv_sec%1000000)*1000 + tv.tv_usec/1000;
}

void set_ts_filt(int fd,uint16_t pid, dmx_pes_type_t pestype)
{
  struct dmx_pes_filter_params pesFilterParams;

  fprintf(stderr,"Setting filter for PID %d\n",pid);
  pesFilterParams.pid     = pid;
  pesFilterParams.input   = DMX_IN_FRONTEND;
  pesFilterParams.output  = DMX_OUT_TS_TAP;
  pesFilterParams.pes_type = pestype;
  pesFilterParams.flags   = DMX_IMMEDIATE_START;

  if (ioctl(fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)  {
    fprintf(stderr,"FILTER %i: ",pid);
    perror("DMX SET PES FILTER");
  }
}

typedef enum {STREAM_ON,STREAM_OFF} state_t;


  int pids[MAX_CHANNELS];
  int pestypes[MAX_CHANNELS];
  unsigned char hi_mappids[8192];
  unsigned char lo_mappids[8192];
  int fd_frontend;
  int pid,pid2;
  int connectionOpen;
  int fromlen;
  char hostname[64];
  char in_ch;
  struct hostent *hp;
  struct sockaddr_in name, fsin;
  int ReUseAddr=1;
  int oldflags;
  int npids = 0;
  int fd[MAX_CHANNELS];
  int to_stdout = 0; /* to stdout instead of rtp stream */

  /* rtp */
  struct rtpheader hdr;
  struct sockaddr_in sOut;
  int socketOut;

/* The output routine for sending a PS */
void my_write_out(uint8_t *buf, int count,void  *p)
{
  /* to fix: change this buffer size and check for overflow */
  static char out_buffer[1000000];
  static int out_buffer_n=0;
  int i;

  if (to_stdout) {
    /* This one is easy. */

    write(STDOUT_FILENO, buf, count);
  } else { /* We are streaming it. */
    /* Copy data to write to the end of out_buffer */

    memcpy(&out_buffer[out_buffer_n],buf,count);
    out_buffer_n+=count;

    /* Send as many full packets as possible */

    i=0;
    while ((i + MAX_RTP_SIZE) < out_buffer_n) {
       hdr.timestamp = getmsec()*90;
       sendrtp2(socketOut,&sOut,&hdr,&out_buffer[i],MAX_RTP_SIZE);
       i+=MAX_RTP_SIZE;
    }

    /* Move whatever data is left to the start of the buffer */

    memmove(&out_buffer[0],&out_buffer[i],out_buffer_n-i);
    out_buffer_n-=i;
  }
}

#define LARGE_BUF_SIZE (20 * 1024 * 1024)
unsigned char pszLargeBuf[LARGE_BUF_SIZE];

int main(int argc, char **argv)
{
  //  state_t state=STREAM_OFF;
  int fd_dvr;
  int i,j,n;
  unsigned char buf[MTU];
  int nBytesLeftOver = 0;
  struct pollfd pfds[1];  // DVR device
  unsigned int secs = -1;
  unsigned long freq=0;
  unsigned long srate=0;
  int count;
  char* ch;
  dmx_pes_type_t pestype;
  int bytes_read;
  int do_analyse=0;
  unsigned char* free_bytes;
  int output_type=RTP_TS;
  int64_t counts[8192];
  double f;
  long start_time=-1;
  long end_time=-1;
  struct timeval tv;
  int found;
  int nStrength, nBER, nSNR, nUncorrected, nTEI = 0, nTotalUncorrected = 0;
  int nLNB = 0;

  /* Output: {uni,multi,broad}cast socket */
  char ipOut[20];
  int portOut;
  int ttl;
  
  pids_map = NULL;
  map_cnt = 0;

  fprintf(stderr,"dvbstream v0.6 - (C) Dave Chapman 2001-2004\n");
  fprintf(stderr,"Released under the GPL.\n");
  fprintf(stderr,"Latest version available from http://www.linuxstb.org/\n");

  /* Initialise PID map */
  for (i=0;i<8192;i++) {
    hi_mappids[i]=(i >> 8);
    lo_mappids[i]=(i&0xff);
    counts[i]=0;
  }

  /* Set default IP and port */
  strcpy(ipOut,"224.0.1.2");
  portOut = 5004;

  if (argc==1) {
    fprintf(stderr,"Usage: dvbtune [OPTIONS] pid1 pid2 ... pid8\n\n");
    fprintf(stderr,"-i          IP multicast address\n");
    fprintf(stderr,"-r          IP multicast port\n");
    fprintf(stderr,"-o          Stream to stdout instead of network\n");
    fprintf(stderr,"-o:file.ts  Stream to named file instead of network\n");
    fprintf(stderr,"-n secs     Stop after secs seconds\n");
    fprintf(stderr,"-ps         Convert stream to Program Stream format (needs exactly 2 pids)\n");
    fprintf(stderr,"-v vpid     Decode video PID (full cards only)\n");
    fprintf(stderr,"-a apid     Decode audio PID (full cards only)\n");
    fprintf(stderr,"-t ttpid    Decode teletext PID (full cards only)\n");
    fprintf(stderr,"\nStandard tuning options:\n\n");
    fprintf(stderr,"-f freq     absolute Frequency (DVB-S in Hz or DVB-T in Hz)\n");
    fprintf(stderr,"            or L-band Frequency (DVB-S in Hz or DVB-T in Hz)\n");
    fprintf(stderr,"-p [H,V]    Polarity (DVB-S only)\n");
    fprintf(stderr,"-s N        Symbol rate (DVB-S or DVB-C)\n");

    fprintf(stderr,"\nAdvanced tuning options:\n\n");
    fprintf(stderr,"-c [0-3]    Use DVB card #[0-3]\n");
    fprintf(stderr,"-D [0-4]    DiSEqC command (0=none)\n\n");
    fprintf(stderr,"-I [0|1|2]  0=Spectrum Inversion off, 1=Spectrum Inversion on, 2=auto\n");
    fprintf(stderr,"-qam X      DVB-T modulation - 16%s, 32%s, 64%s, 128%s or 256%s\n",(CONSTELLATION_DEFAULT==QAM_16 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_32 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_64 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_128 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_256 ? " (default)" : ""));
    fprintf(stderr,"-gi N       DVB-T guard interval 1_N (N=128%s, 32%s, 16%s, 8%s or 4%s)\n",(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_128 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_32 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_16 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_8 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_4 ? " (default)" : ""));
    fprintf(stderr,"-cr N       DVB-T code rate. N=AUTO%s, 1_2%s, 2_3%s, 3_4%s, 5_6%s, 7_8%s\n",(HP_CODERATE_DEFAULT==FEC_AUTO ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_1_2 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_2_3 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_3_4 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_5_6 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_7_8 ? " (default)" : ""));
    fprintf(stderr,"-bw N       DVB-T bandwidth (Mhz) - N=6%s, 7%s or 8%s\n",(BANDWIDTH_DEFAULT==BANDWIDTH_6_MHZ ? " (default)" : ""),(BANDWIDTH_DEFAULT==BANDWIDTH_7_MHZ ? " (default)" : ""),(BANDWIDTH_DEFAULT==BANDWIDTH_8_MHZ ? " (default)" : ""));
    fprintf(stderr,"-tm N       DVB-T transmission mode - N=2%s or 8%s\n",(TRANSMISSION_MODE_DEFAULT==TRANSMISSION_MODE_2K ? " (default)" : ""),(TRANSMISSION_MODE_DEFAULT==TRANSMISSION_MODE_8K ? " (default)" : ""));

    fprintf(stderr,"\n-analyse    Perform a simple analysis of the bitrates of the PIDs in the transport stream\n");

    fprintf(stderr,"\n");
    fprintf(stderr,"NOTE: Use pid1=8192 to broadcast whole TS stream from a budget card\n");
    return(-1);
  } else {
    npids=0;
    pestype=DMX_PES_OTHER;  // Default PES type
    for (i=1;i<argc;i++) {
      if (strcmp(argv[i],"-analyse")==0) {
        do_analyse=1;
        output_type=RTP_NONE;
        if (secs==-1) { secs=10; }
      } else if (strcmp(argv[i],"-i")==0) {
        i++;
        strcpy(ipOut,argv[i]);
      } else if (strcmp(argv[i],"-r")==0) {
        i++;
        portOut=atoi(argv[i]);
      } else if (strcmp(argv[i],"-f")==0) {
        i++;
        freq=atoi(argv[i]);
        if (freq <= 13000) { freq *= 1000UL; }
      } else if (strcmp(argv[i],"-p")==0) {
        i++;
        if (argv[i][1]==0) {
          if (tolower(argv[i][0])=='v') {
            pol='V';
          } else if (tolower(argv[i][0])=='h') {
            pol='H';
          }
        }
      } 
      else if (strcmp(argv[i],"-s")==0) {
        i++;
        srate=atoi(argv[i])*1000UL;
      } 
      else if (strcmp(argv[i],"-D")==0) 
      {
        i++;
        diseqc=atoi(argv[i]);
        if(diseqc < 1 || diseqc > 4) diseqc = 1;
      } else if (strcmp(argv[i],"-I")==0) {
        i++;
        if (atoi(argv[i])==0)
           specInv = INVERSION_OFF;
        else if (atoi(argv[i])==1)
           specInv = INVERSION_ON;
        else
           specInv = INVERSION_AUTO;
      }
      else if (strcmp(argv[i],"-o")==0) {
        to_stdout = 1;
      } else if (strcmp(argv[i],"-n")==0) {
        i++;
        secs=atoi(argv[i]);
      } else if (strcmp(argv[i],"-c")==0) {
        i++;
        card=atoi(argv[i]);
        if ((card < 0) || (card > 7)) {
          fprintf(stderr,"ERROR: card parameter must be between 0 and %d\n",MAX_CARDS-1);
        }
      } else if (strcmp(argv[i],"-v")==0) {
        pestype=DMX_PES_VIDEO;
      } else if (strcmp(argv[i],"-a")==0) {
        pestype=DMX_PES_AUDIO;
      } else if (strcmp(argv[i],"-t")==0) {
        pestype=DMX_PES_TELETEXT;
      } else if (strcmp(argv[i],"-qam")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 16:  modulation=QAM_16; break;
          case 32:  modulation=QAM_32; break;
          case 64:  modulation=QAM_64; break;
          case 128: modulation=QAM_128; break;
          case 256: modulation=QAM_256; break;
          default:
            fprintf(stderr,"Invalid QAM rate: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-gi")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 128:  guardInterval=GUARD_INTERVAL_1_128; break;
          case 32:  guardInterval=GUARD_INTERVAL_1_32; break;
          case 16:  guardInterval=GUARD_INTERVAL_1_16; break;
          case 8:   guardInterval=GUARD_INTERVAL_1_8; break;
          case 4:   guardInterval=GUARD_INTERVAL_1_4; break;
          default:
            fprintf(stderr,"Invalid Guard Interval: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-tm")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 8:   TransmissionMode=TRANSMISSION_MODE_8K; break;
          case 2:   TransmissionMode=TRANSMISSION_MODE_2K; break;
          default:
            fprintf(stderr,"Invalid Transmission Mode: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-bw")==0) {
        i++;
        switch(atoi(argv[i])) {
          case 8:   bandWidth=BANDWIDTH_8_MHZ; break;
          case 7:   bandWidth=BANDWIDTH_7_MHZ; break;
          case 6:   bandWidth=BANDWIDTH_6_MHZ; break;
          default:
            fprintf(stderr,"Invalid DVB-T bandwidth: %s\n",argv[i]);
            exit(0);
        }
      } else if (strcmp(argv[i],"-fec")==0) {
        i++;
        if (!strcmp(argv[i],"AUTO")) {
          fec=FEC_AUTO;
        } else if (!strcmp(argv[i],"1_2")) {
          fec=FEC_1_2;
        } else if (!strcmp(argv[i],"2_3")) {
          fec=FEC_2_3;
        } else if (!strcmp(argv[i],"3_4")) {
          fec=FEC_3_4;
        } else if (!strcmp(argv[i],"5_6")) {
          fec=FEC_5_6;
        } else if (!strcmp(argv[i],"7_8")) {
          fec=FEC_7_8;
        } else {
          fprintf(stderr,"Invalid FEC: %s\n",argv[i]);
          exit(0);
        }
      } else if (strcmp(argv[i],"-cr")==0) {
        i++;
        if (!strcmp(argv[i],"AUTO")) {
          HP_CodeRate=FEC_AUTO;
        } else if (!strcmp(argv[i],"1_2")) {
          HP_CodeRate=FEC_1_2;
        } else if (!strcmp(argv[i],"2_3")) {
          HP_CodeRate=FEC_2_3;
        } else if (!strcmp(argv[i],"3_4")) {
          HP_CodeRate=FEC_3_4;
        } else if (!strcmp(argv[i],"5_6")) {
          HP_CodeRate=FEC_5_6;
        } else if (!strcmp(argv[i],"7_8")) {
          HP_CodeRate=FEC_7_8;
        } else {
          fprintf(stderr,"Invalid Code Rate: %s\n",argv[i]);
          exit(0);
        }
      } else if (strcmp(argv[i],"-s2")==0) {
        sys = SYS_DVBS2;
        modulation = QPSK;
      } else if (strcmp(argv[i],"-8psk")==0) {
        modulation = PSK_8;
      } else if (strcmp(argv[i],"-from")==0) {
        i++;
        if (map_cnt) {
          pids_map[map_cnt-1].start_time=atoi(argv[i])*60;
        } else {
          start_time=atoi(argv[i])*60;
        }
      } else if (strcmp(argv[i],"-to")==0) {
        i++;
        if (map_cnt) {
          pids_map[map_cnt-1].end_time=atoi(argv[i])*60;
        } else {
          end_time=atoi(argv[i])*60;
          secs=end_time;
        }
      } else if (strstr(argv[i], "-o:")==argv[i]) {
        if (strlen(argv[i]) > 3) {
          fprintf(stderr,"Processing %s\n",argv[i]);
          map_cnt++;
          pids_map = (pids_map_t*) realloc(pids_map, sizeof(pids_map_t) * map_cnt);
          pids_map[map_cnt-1].pid_cnt = 0;
          pids_map[map_cnt-1].start_time=start_time;
          pids_map[map_cnt-1].end_time=end_time;
          for(j=0; j < MAX_CHANNELS; j++) pids_map[map_cnt-1].pids[j] = -1;
          pids_map[map_cnt-1].filename = (char *) malloc(strlen(argv[i]) - 2);
          strcpy(pids_map[map_cnt-1].filename, &argv[i][3]);

          output_type = MAP_TS;
        }
      } else {
        if ((ch=(char*)strstr(argv[i],":"))!=NULL) {
          pid2=atoi(&ch[1]);
          ch[0]=0;
        } else {
          pid2=-1;
        }
        pid=atoi(argv[i]);

        // If we are currently processing a "-o:" option:
        if (map_cnt) {
          // block for the map
          found = 0;
          for (j=0;j<MAX_CHANNELS;j++) {
            if(pids_map[map_cnt-1].pids[j] == pid) found = 1;
          }
          if (found == 0) {
            pids_map[map_cnt-1].pids[pids_map[map_cnt-1].pid_cnt] = pid;
            pids_map[map_cnt-1].pid_cnt++;
          }
        }

        // block for the list of pids to demux
        found = 0;
        for (j=0;j<npids;j++) {
          if(pids[j] == pid) found = 1;
        }
        if (found==0) {
          if (npids == MAX_CHANNELS) {
            fprintf(stderr,"\nSorry, you can only set up to %d filters.\n\n",MAX_CHANNELS);
            return(-1);
          } else {
            pestypes[npids]=pestype;
            pestype=DMX_PES_OTHER;
            pids[npids++]=pid;
            if (pid2!=-1) {
              hi_mappids[pid]=pid2>>8;
              lo_mappids[pid]=pid2&0xff;
              fprintf(stderr,"Mapping %d to %d\n",pid,pid2);
            }
          }
        }
      }
    }
  }

  for (i=0;i<map_cnt;i++) {
    FILE *f;
    f = fopen(pids_map[i].filename, "w+b");
    if (f != NULL) {
      pids_map[i].fd = fileno(f);
      make_nonblock(pids_map[i].fd);
      fprintf(stderr, "Open file %s\n", pids_map[i].filename);
    } else {
      pids_map[i].fd = -1;
      fprintf(stderr, "Couldn't open file %s, errno:%d\n", pids_map[map_cnt-1].filename, errno);
    }
  }

  if (signal(SIGHUP, SignalHandler) == SIG_IGN) signal(SIGHUP, SIG_IGN);
  if (signal(SIGINT, SignalHandler) == SIG_IGN) signal(SIGINT, SIG_IGN);
  if (signal(SIGTERM, SignalHandler) == SIG_IGN) signal(SIGTERM, SIG_IGN);
  if (signal(SIGALRM, SignalHandler) == SIG_IGN) signal(SIGALRM, SIG_IGN);
  alarm(ALARM_TIME);

  if ( (freq>100000000)) {
    sys = SYS_DVBT;
    if (open_fe(&fd_frontend)) {
      i=tune_it_s2(fd_frontend,sys,freq,srate,0,tone,specInv,diseqc,modulation,HP_CodeRate,TransmissionMode,guardInterval,bandWidth);
      if (i < 0) tune_it_s2(fd_frontend,sys,freq,srate,0,tone,specInv,diseqc,modulation,HP_CodeRate,TransmissionMode,guardInterval,bandWidth);
    }
  } else if ((freq!=0) && (pol!=0) && (srate!=0)) {
    if (open_fe(&fd_frontend)) {
      fprintf(stderr,"Tuning to %ld Hz\n",freq);
      i=tune_it_s2(fd_frontend,sys,freq,srate,pol,tone,specInv,diseqc,modulation,fec,TransmissionMode,guardInterval,bandWidth);
      if (i < 0) tune_it_s2(fd_frontend,sys,freq,srate,pol,tone,specInv,diseqc,modulation,fec,TransmissionMode,guardInterval,bandWidth);
    }
  }

  //  if (i<0) { exit(i); }

  for (i=0;i<map_cnt;i++) {
    if ((secs==-1) || (secs < pids_map[i].end_time)) { secs=pids_map[i].end_time; }
    fprintf(stderr,"MAP %d, file %s: From %ld secs, To %ld secs, %d PIDs - ",i,pids_map[i].filename,pids_map[i].start_time,pids_map[i].end_time,pids_map[i].pid_cnt);
    for (j=0;j<MAX_CHANNELS;j++) { if (pids_map[i].pids[j]!=-1) fprintf(stderr," %d",pids_map[i].pids[j]); }
    fprintf(stderr,"\n");
  }
  
  fprintf(stderr,"dvbstream will stop after %d seconds (%d minutes)\n",secs,secs/60);

  for (i=0;i<npids;i++) {  
    if((fd[i] = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
    }
  }

  if((fd_dvr = open(dvrdev[card],O_RDONLY|O_NONBLOCK)) < 0){
    perror("DVR DEVICE: ");
    return -1;
  }

  /* Now we set the filters */
  for (i=0;i<npids;i++) set_ts_filt(fd[i],pids[i],pestypes[i]);

  gettimeofday(&tv,(struct timezone*) NULL);
  real_start_time=tv.tv_sec;
  now=0;

  if (do_analyse) {
    fprintf(stderr,"Analysing PIDS\n");
  } else {
    if (to_stdout) {
      fprintf(stderr,"Output to stdout\n");
    }
    else {
      ttl = 2;
      fprintf(stderr,"Using %s:%d:%d\n",ipOut,portOut,ttl);

      /* Init RTP */
      socketOut = makesocket(ipOut,portOut,ttl,&sOut);
      initrtp(&hdr,33);  // 33 is payload type for MPEG-TS
      fprintf(stderr,"version=%X\n",hdr.b.v);
    }
    fprintf(stderr,"Streaming %d stream%s\n",npids,(npids==1 ? "" : "s"));
  }

  /* The DIBCOM 7000PC gives problems with continuity errors in the first few packets, so we
     read and discard 5 packets to clean things up. */

  bytes_read = 0;
  while (bytes_read < 5*188) {  
    n = read(fd_dvr,buf,5*188-bytes_read);
    if (n > 0) { bytes_read += n; }
  }

  /* Read packets */
  free_bytes = buf;

  connectionOpen=0;
  pfds[0].fd=fd_dvr;
  pfds[0].events=POLLIN|POLLPRI;

  pthread_t hFileWriterThread;
  pthread_attr_t ThreadAttr;

  pthread_mutex_init(&muDataToFile, NULL);
  pthread_cond_init(&muconDataToFile, NULL);

  pthread_attr_init(&ThreadAttr);
  //pthread_attr_setdetachstate(&ThreadAttr, PTHREAD_CREATE_JOINABLE);                                                                                                         

  pthread_attr_setscope(&ThreadAttr, PTHREAD_SCOPE_SYSTEM);
  pthread_create(&hFileWriterThread, &ThreadAttr, FileWriterFunc, NULL);

  time_t nLast = 0;
  int nHighWaterMark = 0;

  while ( !Interrupted) {
    /* Poll the open file descriptors */
    poll(pfds,1,500);

    if (output_type==RTP_TS) {
      /* Attempt to read 188 bytes from /dev/ost/dvr */
      if ((bytes_read = read(fd_dvr,free_bytes,PACKET_SIZE)) > 0) {
        if (bytes_read!=PACKET_SIZE) {
          fprintf(stderr,"No bytes left to read - aborting\n");
          break;
        }

        pid=((free_bytes[1]&0x1f) << 8) | (free_bytes[2]);
        free_bytes[1]=(free_bytes[1]&0xe0)|hi_mappids[pid];
        free_bytes[2]=lo_mappids[pid];
        free_bytes+=bytes_read;

        // If there isn't enough room for 1 more packet, then send it.
        if ((free_bytes+PACKET_SIZE-buf)>MAX_RTP_SIZE) {
          hdr.timestamp = getmsec()*90;
          if (to_stdout) {
            write(1, buf, free_bytes-buf);
          } else {
            sendrtp2(socketOut,&sOut,&hdr,(char*)buf,free_bytes-buf);
          }
          free_bytes = buf;
        }
        count++;
      }
    } else if(output_type==MAP_TS) {
      int bytes_read;
      bytes_read = read(fd_dvr, &pszLargeBuf[nBytesLeftOver], 50 * TS_SIZE);
      unsigned char *buf;
      int nOffset = 0;
      if(bytes_read >= TS_SIZE) {
        int nBytesToProcess = bytes_read;
        bytes_read = TS_SIZE;
        while (nBytesToProcess - nOffset >= TS_SIZE) {
          buf = &pszLargeBuf[nOffset];
          for (i = 0; i < map_cnt; i++) {
            if(pids_map[i].filename) {
              struct EXCHANGE_BUFFER *pszLocalBuffer;
              struct MESSAGE *pLocalMessages;
              pthread_mutex_lock(&muDataToFile);
              nHighWaterMark = nGlobalHighWaterMark;
              nTEI = nGlobalTEI;
              pLocalMessages = pMessageExchange;
              pMessageExchange = NULL;
              if (pExchangeBuffer != NULL) {
                pszLocalBuffer = pExchangeBuffer;
                pExchangeBuffer = NULL;
                pthread_mutex_unlock(&muDataToFile);
              } else {
                pthread_mutex_unlock(&muDataToFile);
                pszLocalBuffer = malloc(sizeof(struct EXCHANGE_BUFFER));
                pszLocalBuffer->nEntries = 0;
              }
              if (pszLocalBuffer->nEntries == MAX_EXCHANGE_ENTRIES - 1) {
                fprintf(stderr, "Buffer overflow\n");
              } else {
                memcpy(pszLocalBuffer->pszBuffer[pszLocalBuffer->nEntries], buf, TS_SIZE);
                nTotalPackets++;
                pszLocalBuffer->nEntries++;
                pthread_mutex_lock(&muDataToFile);
                if (pExchangeBuffer != NULL) {
                  fprintf(stderr, "INTERNAL ERROR: Duplicate buffer appeared\n");
                  exit(1);
                }
                pExchangeBuffer = pszLocalBuffer;
                pthread_cond_signal(&muconDataToFile);
                pthread_mutex_unlock(&muDataToFile);
              }
              struct MESSAGE *pMessageLoop = pLocalMessages;
              if (pLocalMessages != NULL) {
                while (pMessageLoop != NULL) {
                  fprintf(stderr, pMessageLoop->szMessage);
                  struct MESSAGE *pFree = pMessageLoop;
                  pMessageLoop = pMessageLoop->pNext;
                  free(pFree);
                }
              }
            } else {
              if((pids_map[i].pos + PACKET_SIZE) > MAX_RTP_SIZE) {
                hdr.timestamp = getmsec()*90;
                sendrtp2(pids_map[i].socket, &(pids_map[i].sOut), &(pids_map[i].hdr), pids_map[i].buf, pids_map[i].pos);
                pids_map[i].pos = 0;
              }

              memcpy(&(pids_map[i].buf[pids_map[i].pos]), buf, bytes_read);
              pids_map[i].pos += bytes_read;
            }
          }
          nOffset += TS_SIZE;
        }
        if (nBytesToProcess - nOffset != 0) {
          memmove(pszLargeBuf, &pszLargeBuf[nOffset], nBytesToProcess - nOffset);
          nBytesLeftOver = nBytesToProcess - nOffset;
        } else {
          nBytesLeftOver = 0;
        }
      } else if (bytes_read  < 0) {
        if (errno != EAGAIN) fprintf(stderr, "Got error %d (%s) on read\n", errno, strerror(errno));
      } else if (bytes_read < TS_SIZE) {
        nBytesLeftOver = bytes_read;
      }
    } else {
      if (do_analyse) {
        if (read(fd_dvr,buf,TS_SIZE) > 0) {
          pid=((buf[1]&0x1f) << 8) | (buf[2]);
          counts[pid]++;
        }
      }
    }
    if ((secs!=-1) && (secs <=now)) { Interrupted=1; }
  }

  if (Interrupted) {
    fprintf(stderr,"Caught signal %d - closing cleanly.\n",Interrupted);
  }

  if (!to_stdout) close(socketOut);
  for (i=0;i<npids;i++) close(fd[i]);
  close(fd_dvr);
  close(fd_frontend);

  if (do_analyse) {
    for (i=0;i<8192;i++) {
      if (counts[i]) {
        f=(counts[i]*184.0*8.0)/(secs*1024.0*1024.0);
        if (f >= 1.0) {
          fprintf(stdout,"%d,%.3f Mbit/s\n",i,f);
        } else {
          fprintf(stdout,"%d,%.0f kbit/s\n",i,f*1024);
        }
      }
    }
  }

  return(0);
}
