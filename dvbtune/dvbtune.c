/*

   dvbtune - a program for tuning DVB TV and Radio channels.

   Initial transponders for "-x" option:

   Astra   28E:  
   Astra   19E: 12670v - srate 22000
   Hotbird 13E: 10911v - srate 27500  ?? Doesn't work!
   Thor etc 1W: 11247v - srate 24500 (Most channels!)

   Copyright (C) Dave Chapman 2001-2004
  
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
   Or, point your browser to http://www.gnu.org/copyleft/gpl.html

   Added Switch -n that adds a network interface and switch -m that monitors
   the reception quality. Changed the tuning code
   Added command line parameters for spectral inversion. Changed code to allow
   L-Band frequencies with -f switch

   Copyright (C) Hilmar Linder 2002

   
*/

// Linux includes:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>

// DVB includes:
#include <linux/dvb/osd.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/video.h>
#include <linux/dvb/audio.h>
#include <linux/dvb/net.h>

#include "tune.h"
#include "si.h"

int fd_demuxv,fd_demuxa,fd_demuxtt,fd_demuxsi,fd_demuxrec,fd_demuxd;
int pnr=-1;
int apid=0;
int vpid=0;
int card=0;
fe_delivery_system_t sys = SYS_DVBS;
fe_spectral_inversion_t specInv = INVERSION_AUTO;
int tone = -1;

#define MAX_CARDS 8
char* frontenddev[MAX_CARDS]={"/dev/dvb/adapter0/frontend0","/dev/dvb/adapter1/frontend0","/dev/dvb/adapter2/frontend0","/dev/dvb/adapter3/frontend0","/dev/dvb/adapter4/frontend0","/dev/dvb/adapter5/frontend0","/dev/dvb/adapter6/frontend0","/dev/dvb/adapter7/frontend0"};
char* dvrdev[MAX_CARDS]={"/dev/dvb/adapter0/dvr0","/dev/dvb/adapter1/dvr0","/dev/dvb/adapter2/dvr0","/dev/dvb/adapter3/dvr0","/dev/dvb/adapter4/dvr0","/dev/dvb/adapter5/dvr0","/dev/dvb/adapter6/dvr0","/dev/dvb/adapter7/dvr0"};
char* demuxdev[MAX_CARDS]={"/dev/dvb/adapter0/demux0","/dev/dvb/adapter1/demux0","/dev/dvb/adapter2/demux0","/dev/dvb/adapter3/demux0","/dev/dvb/adapter4/demux0","/dev/dvb/adapter5/demux0","/dev/dvb/adapter6/demux0","/dev/dvb/adapter7/demux0"};

char* delivery_systems[] = {
  "SYS_UNDEFINED",
  "SYS_DVBC_ANNEX_AC",
  "SYS_DVBC_ANNEX_B",
  "SYS_DVBT",
  "SYS_DSS",
  "SYS_DVBS",
  "SYS_DVBS2",
  "SYS_DVBH",
  "SYS_ISDBT",
  "SYS_ISDBS",
  "SYS_ISDBC",
  "SYS_ATSC",
  "SYS_ATSCMH",
  "SYS_DMBTH",
  "SYS_CMMB",
  "SYS_DAB"
};
transponder_t* transponders=NULL;
int num_trans=0;

transponder_t transponder;

typedef struct _pat_t {
  int service_id;
  int pmt_pid;
  int scanned;
  struct _pat_t* next;
} pat_t;

pat_t* pats=NULL;

void free_pat_list() {
  pat_t* t=pats;

  while (pats!=NULL) {
    t=pats->next;
    free(pats);
    pats=t;
  }
}

int get_pmt_pid(int service_id) {
  pat_t* t=pats;
  int found=0;

  while ((!found) && (t!=NULL)) {
    if (t->service_id==service_id) {
      found=1;
    } else {
      t=t->next;
    }
  }

  if (found) {
    return(t->pmt_pid);
  } else {
    return(0);
  }
}

void add_pat(pat_t pat) {
  pat_t* t;
  int found;

  if (pats==NULL) {
    pats=(pat_t*)malloc(sizeof(pat));

    pats->service_id=pat.service_id;
    pats->pmt_pid=pat.pmt_pid;
    pats->scanned=0;
    pats->next=NULL;
  } else {
    t=pats;
    found=0;
    while ((!found) && (t!=NULL)) {
       if ((t->service_id==pat.service_id)) {
          found=1;
       } else {
         t=t->next;
       }
    }

    if (!found) {
      t=(pat_t*)malloc(sizeof(pat));

      t->service_id=pat.service_id;
      t->pmt_pid=pat.pmt_pid;
      t->scanned=0;
      t->next=pats;

      pats=t;
    }
  }
}

void set_recpid(int fd, ushort ttpid) 
{  
struct dmx_pes_filter_params pesFilterParamsREC;

        if (ttpid==0 || ttpid==0xffff) {
	        ioctl(fd, DMX_STOP, 0);
	        return;
	}

	pesFilterParamsREC.pid     = ttpid;
	pesFilterParamsREC.input   = DMX_IN_FRONTEND; 
	pesFilterParamsREC.output  = DMX_OUT_TAP; 
	pesFilterParamsREC.pes_type = DMX_PES_OTHER; 
	pesFilterParamsREC.flags   = DMX_IMMEDIATE_START;
	if (ioctl(fd, DMX_SET_PES_FILTER, 
		  &pesFilterParamsREC) < 0)
		perror("set_recpid");
}

void set_sipid(ushort ttpid) 
{  
struct dmx_pes_filter_params pesFilterParamsSI;

        if (ttpid==0 || ttpid==0xffff) {
	        ioctl(fd_demuxsi, DMX_STOP, 0);
	        return;
	}

	pesFilterParamsSI.pid     = ttpid;
	pesFilterParamsSI.input   = DMX_IN_FRONTEND; 
	pesFilterParamsSI.output  = DMX_OUT_TS_TAP; 
	pesFilterParamsSI.pes_type = DMX_PES_OTHER; 
	pesFilterParamsSI.flags   = DMX_IMMEDIATE_START;
	if (ioctl(fd_demuxsi, DMX_SET_PES_FILTER, 
		  &pesFilterParamsSI) < 0)
		perror("set_sipid");
}

void set_ttpid(ushort ttpid) 
{  
struct dmx_pes_filter_params pesFilterParamsTT;

        if (ttpid==0 || ttpid==0xffff) {
	        ioctl(fd_demuxtt, DMX_STOP, 0);
	        return;
	}

	pesFilterParamsTT.pid     = ttpid;
	pesFilterParamsTT.input   = DMX_IN_FRONTEND; 
	pesFilterParamsTT.output  = DMX_OUT_DECODER; 
	pesFilterParamsTT.pes_type = DMX_PES_TELETEXT; 
	pesFilterParamsTT.flags   = DMX_IMMEDIATE_START;
	if (ioctl(fd_demuxtt, DMX_SET_PES_FILTER, 
		  &pesFilterParamsTT) < 0)
		perror("set_ttpid");
}

void set_vpid(ushort vpid) 
{  
struct dmx_pes_filter_params pesFilterParamsV;
        if (vpid==0 || vpid==0xffff) {
	        ioctl(fd_demuxv, DMX_STOP, 0);
	        return;
	}

	pesFilterParamsV.pid     = vpid;
	pesFilterParamsV.input   = DMX_IN_FRONTEND; 
	pesFilterParamsV.output  = DMX_OUT_DECODER; 
	pesFilterParamsV.pes_type = DMX_PES_VIDEO; 
	pesFilterParamsV.flags   = DMX_IMMEDIATE_START;
	if (ioctl(fd_demuxv, DMX_SET_PES_FILTER, 
		  &pesFilterParamsV) < 0)
		perror("set_vpid");
}

void set_apid(ushort apid) 
{  
struct dmx_pes_filter_params pesFilterParamsA;
        if (apid==0 || apid==0xffff) {
	        ioctl(fd_demuxa, DMX_STOP, apid);
	        return;
	}
	pesFilterParamsA.pid = apid;
	pesFilterParamsA.input = DMX_IN_FRONTEND; 
	pesFilterParamsA.output = DMX_OUT_DECODER; 
	pesFilterParamsA.pes_type = DMX_PES_AUDIO; 
	pesFilterParamsA.flags = DMX_IMMEDIATE_START;
	if (ioctl(fd_demuxa, DMX_SET_PES_FILTER, 
		  &pesFilterParamsA) < 0)
		perror("set_apid");
}

void set_dpid(ushort dpid) 
{ 
	struct dmx_sct_filter_params sctFilterParams;
 
        if (dpid==0 || dpid==0xffff) {
                ioctl(fd_demuxd, DMX_STOP, dpid);
                return;
        }
        memset(&sctFilterParams.filter,0,sizeof(sctFilterParams.filter));
        sctFilterParams.pid = dpid;
	//sctFilterParams.filter.filter[0] = 0x3e;
        //sctFilterParams.filter.mask[0] = 0xff; 
	sctFilterParams.timeout = 0;
        sctFilterParams.flags = DMX_IMMEDIATE_START;
        if (ioctl(fd_demuxd, DMX_SET_FILTER, &sctFilterParams) < 0)
                perror("set_dpid"); 
}


void set_ts_filter(int fd,uint16_t pid)
{
  struct dmx_pes_filter_params pesFilterParams;

  pesFilterParams.pid     = pid; 
  pesFilterParams.input   = DMX_IN_FRONTEND;
  pesFilterParams.output  = DMX_OUT_TS_TAP;
  pesFilterParams.pes_type = DMX_PES_OTHER;

// A HACK TO DECODE STREAMS ON DVB-S CARD WHILST STREAMING
//  if (pid==255) pesFilterParams.pesType = DMX_PES_VIDEO;
//  if (pid==256) pesFilterParams.pesType = DMX_PES_AUDIO;
  pesFilterParams.flags   = DMX_IMMEDIATE_START;

  if (ioctl(fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)  {
    fprintf(stderr,"FILTER %i: ",pid);
    perror("DMX SET PES FILTER");
  }
}


void dump(char* fname, int len, char* buf) {
  FILE* f;

  f=fopen(fname,"w");
  if (f) {
    fwrite(buf,1,len,f);
    fclose(f);
  }
}

int scan_nit(int x) {
  int fd_nit;
  int n,seclen;
  int i;
  struct pollfd ufd;
  unsigned char buf[4096];
  struct dmx_sct_filter_params sctFilterParams;
  int info_len,network_id;

  if((fd_nit = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      perror("fd_nit DEVICE: ");
      return -1;
  }

  sctFilterParams.pid=0x10;
  memset(&sctFilterParams.filter,0,sizeof(sctFilterParams.filter));
  sctFilterParams.timeout = 0;
  sctFilterParams.flags = DMX_IMMEDIATE_START;
  sctFilterParams.filter.filter[0]=x;
  sctFilterParams.filter.mask[0]=0xff;

  if (ioctl(fd_nit,DMX_SET_FILTER,&sctFilterParams) < 0) {
    perror("NIT - DMX_SET_FILTER:");
    close(fd_nit);
    return -1;
  }

  ufd.fd=fd_nit;
  ufd.events=POLLPRI;
  if (poll(&ufd,1,10000) < 0 ) {
    fprintf(stderr,"TIMEOUT on read from fd_nit\n");
    close(fd_nit);
    return -1;
  }
  if (read(fd_nit,buf,3)==3) {
    seclen=((buf[1] & 0x0f) << 8) | (buf[2] & 0xff);
    n = read(fd_nit,buf+3,seclen);
    if (n==seclen) {
      seclen+=3;
//      dump("nit.dat",seclen,buf);
//      printf("<nit>\n");
      network_id=(buf[3]<<8)|buf[4];
//      printf("<network id=\"%d\">\n",network_id);

      info_len=((buf[8]&0x0f)<<8)|buf[9];
      i=10;
      parse_descriptors(info_len,&buf[i],&transponder,&transponders);
      i+=info_len;
      i+=2;
      while (i < (seclen-4)) {
        transponder.id=(buf[i]<<8)|buf[i+1];
        i+=2;
        transponder.onid=(buf[i]<<8)|buf[i+1];
        i+=2;
	//        printf("<transponder id=\"%d\" onid=\"%d\">\n",transponder.id,transponder.onid);
        info_len=((buf[i]&0x0f)<<8)|buf[i+1];
        i+=2;
        parse_descriptors(info_len,&buf[i],&transponder,&transponders);
//        printf("</transponder>\n");
        i+=info_len;
      }
//      printf("</network>\n");
//      printf("</nit>\n");
    } else {
      fprintf(stderr,"Under-read bytes for NIT - wanted %d, got %d\n",seclen,n);
    }
  } else {
    fprintf(stderr,"Nothing to read from fd_nit\n");
  }
  close(fd_nit);
  return(0);
}

void scan_pmt(int pid,int sid,int change) {
  int fd_pmt;
  int n,seclen;
  int i;
  unsigned char buf[4096];
  struct dmx_sct_filter_params sctFilterParams;
  int service_id;
  int info_len,es_pid,stream_type;
  struct pollfd ufd;

  fprintf(stderr,"Scanning pmt: pid=%d, sid=%d\n",pid,sid);

  if (pid==0) { return; }

  if((fd_pmt = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      perror("fd_pmt DEVICE: ");
      return;
  }

  sctFilterParams.pid=pid;
  memset(&sctFilterParams.filter,0,sizeof(sctFilterParams.filter));
  sctFilterParams.timeout = 0;
  sctFilterParams.flags = DMX_IMMEDIATE_START;
  sctFilterParams.filter.filter[0]=0x02;
  sctFilterParams.filter.mask[0]=0xff;

  if (ioctl(fd_pmt,DMX_SET_FILTER,&sctFilterParams) < 0) {
    perror("PMT - DMX_SET_FILTER:");
    close(fd_pmt);
    return;
  }

  ufd.fd=fd_pmt;
  ufd.events=POLLPRI;
  if (poll(&ufd,1,10000) < 0) {
     fprintf(stderr,"TIMEOUT reading from fd_pmt\n");
     close(fd_pmt);
     return;
  }

  if (read(fd_pmt,buf,3)==3) {
    seclen=((buf[1] & 0x0f) << 8) | (buf[2] & 0xff);
    n = read(fd_pmt,buf+3,seclen);
    if (n==seclen) {
      seclen+=3;
//      printf("<pmt>\n");
      service_id=(buf[3]<<8)|buf[4];
//      printf("<service id=\"%d\" pmt_pid=\"%d\">\n",service_id,pid);

      if (sid != service_id) {
	close(fd_pmt);
	scan_pmt(pid, sid, change);
	return;
      }

      info_len=((buf[10]&0x0f)<<8)|buf[11];
      i=12;
      parse_descriptors(info_len,&buf[i],&transponder,&transponders);
      i+=info_len;
      while (i < (seclen-4)) {
        stream_type=buf[i++];
        es_pid=((buf[i]&0x1f)<<8)|buf[i+1];
        printf("<stream type=\"%d\" pid=\"%d\">\n",stream_type,es_pid);
        if (change) {
          if ((vpid==0) && ((stream_type==1) || (stream_type==2))) {
             vpid=es_pid;
          }
          if ((apid==0) && ((stream_type==3) || (stream_type==4))) {
            apid=es_pid;
          }
        }

        i+=2;
        info_len=((buf[i]&0x0f)<<8)|buf[i+1];
        i+=2;
        parse_descriptors(info_len,&buf[i],&transponder,&transponders);
        i+=info_len;
        printf("</stream>\n");
      }
//      printf("</service>\n");
//      printf("</pmt>\n");
    } else {
      printf("Under-read bytes for PMT - wanted %d, got %d\n",seclen,n);
    }
  } else {
    fprintf(stderr,"Nothing to read from fd_pmt\n");
  }

  close(fd_pmt);
}

void scan_pat() {
  int fd_pat;
  int n,seclen;
  int i;
  unsigned char buf[4096];
  struct dmx_sct_filter_params sctFilterParams;
  struct pollfd ufd;

  pat_t pat;

  if((fd_pat = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      perror("fd_pat DEVICE: ");
      return;
  }

  sctFilterParams.pid=0x0;
  memset(&sctFilterParams.filter,0,sizeof(sctFilterParams.filter));
  sctFilterParams.timeout = 0;
  sctFilterParams.flags = DMX_IMMEDIATE_START;
  sctFilterParams.filter.filter[0]=0x0;
  sctFilterParams.filter.mask[0]=0xff;

  if (ioctl(fd_pat,DMX_SET_FILTER,&sctFilterParams) < 0) {
    perror("PAT - DMX_SET_FILTER:");
    close(fd_pat);
    return;
  }

  ufd.fd=fd_pat;
  ufd.events=POLLPRI;
  if (poll(&ufd,1,10000) < 0) {
     fprintf(stderr,"TIMEOUT reading from fd_pat\n");
     close(fd_pat);
     return;
  }
  if (read(fd_pat,buf,3)==3) {
    seclen=((buf[1] & 0x0f) << 8) | (buf[2] & 0xff);
    n = read(fd_pat,buf+3,seclen);
    if (n==seclen) {
      seclen+=3;
      //      printf("Read %d bytes - Found %d services\n",seclen,(seclen-11)/4);
    //    for (i=0;i<seclen+3;i++) { printf("%02x ",buf[i]); }
//      printf("<pat>\n");
      i=8;
      while (i < seclen-4) {
        pat.service_id=(buf[i]<<8)|buf[i+1];
        pat.pmt_pid=((buf[i+2]&0x1f)<<8)|buf[i+3];
        add_pat(pat);
	/*        if (service_id!=0) {
          scan_pmt(pmt_pid,service_id,(service_id==pnr));
        } else {
          printf("<service id=\"0\" pmt_pid=\"%d\">\n</service>\n",pmt_pid);
        }
	*/        i+=4;
      }
//      printf("</pat>\n");
    } else {
      printf("Under-read bytes for PAT - wanted %d, got %d\n",seclen,n);
    }
  } else {
    fprintf(stderr,"Nothing to read from fd_pat\n");
  }
  close(fd_pat);
}

void scan_sdt() {
  int fd_sdt;
  int n,seclen;
  int i,k;
  int max_k;
  unsigned char buf[4096];
  struct dmx_sct_filter_params sctFilterParams;
  int ca,service_id,loop_length;
  int pmt_pid;
  struct pollfd ufd;
  int section_number,last_section_number;

  if((fd_sdt = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      perror("fd_sdt DEVICE: ");
      return;
  }

  sctFilterParams.pid=0x11;
  memset(&sctFilterParams.filter,0,sizeof(sctFilterParams.filter));
  sctFilterParams.timeout = 0;
  sctFilterParams.flags = DMX_IMMEDIATE_START;
  sctFilterParams.filter.filter[0]=0x42;
  sctFilterParams.filter.mask[0]=0xff;

  if (ioctl(fd_sdt,DMX_SET_FILTER,&sctFilterParams) < 0) {
    perror("SDT - DMX_SET_FILTER:");
    close(fd_sdt);
    return;
  }

  max_k=1;
//  printf("<sdt>\n");

for (k=0;k<max_k;k++) {
 ufd.fd=fd_sdt;
 ufd.events=POLLPRI;
 if (poll(&ufd,1,20000) < 0 ) {
   fprintf(stderr,"TIMEOUT on read from fd_sdt\n");
   close(fd_sdt);
   return;
 }
  if (read(fd_sdt,buf,3)==3) {
    seclen=((buf[1] & 0x0f) << 8) | (buf[2] & 0xff);
    n = read(fd_sdt,buf+3,seclen);
    if (n==seclen) {
      seclen+=3;
//      printf("Read %d bytes\n",seclen);
    //    for (i=0;i<seclen+3;i++) { printf("%02x ",buf[i]); }
/*      for (i=0;i< seclen;i++) {
        printf("%02x ",buf[i]);
        if ((i % 16)==15) { 
          printf("  ");
          for (j=i-15;j<=i;j++) { 
             printf("%c",((buf[j]>31) && (buf[j]<=127)) ? buf[j] : '.'); 
          }
          printf("\n");
        }
      }
*/
      section_number=buf[6];
      last_section_number=buf[7];
      fprintf(stderr,"Read SDT section - section_number=%d, last_section_number=%d\n",section_number,last_section_number);
      max_k=buf[7]+1; // last_sec_num - read this many (+1) sections

      i=11;
      while (i < (seclen-4)) {
       service_id=(buf[i]<<8)|buf[i+1];
       i+=2;
       i++;  // Skip a field
       ca=(buf[i]&0x10)>>4;
       loop_length=((buf[i]&0x0f)<<8)|buf[i+1];
       pmt_pid = get_pmt_pid(service_id);
       printf("<service id=\"%d\" pmt_pid=\"%d\" ca=\"%d\">\n",service_id,pmt_pid,ca);
       i+=2;
       parse_descriptors(loop_length,&buf[i],&transponder,&transponders);
       i+=loop_length;
       scan_pmt(pmt_pid,service_id,(service_id==pnr));
       printf("</service>\n");
      }
    }  else {
      printf("Under-read bytes for SDT - wanted %d, got %d\n",seclen,n);
    }
  } else {
    fprintf(stderr,"Nothing to read from fd_sdt\n");
    k--;
  }
}
//  printf("</sdt>\n");
  close(fd_sdt);

}

int FEReadBER(int fd, uint32_t *ber)
{
        int ans;

        if ( (ans = ioctl(fd,FE_READ_BER, ber) < 0)){
                perror("FE READ_BER: ");
                return -1;
        }
        return 0;
}


int FEReadSignalStrength(int fd, int32_t *strength)
{
        int ans;

        if ( (ans = ioctl(fd,FE_READ_SIGNAL_STRENGTH, strength) < 0)){
                perror("FE READ SIGNAL STRENGTH: ");
                return -1;
        }
        return 0;
}

int FEReadSNR(int fd, int32_t *snr)
{
        int ans;

        if ( (ans = ioctl(fd,FE_READ_SNR, snr) < 0)){
                perror("FE READ_SNR: ");
                return -1;
        }
        return 0;
}

#if 0
int FEReadAFC(int fd, int32_t *snr)
{   
        int ans;

        if ( (ans = ioctl(fd,FE_READ_AFC, snr) < 0)){
                perror("FE READ_AFC: ");
                return -1;
        }
        return 0;
}
#endif


int FEReadUncorrectedBlocks(int fd, uint32_t *ucb)
{
        int ans;

        if ( (ans = ioctl(fd,FE_READ_UNCORRECTED_BLOCKS, ucb) < 0)){
                perror("FE READ UNCORRECTED BLOCKS: ");
                return -1;
        }
        return 0;
}

int main(int argc, char **argv)
{
  int fd_frontend=0;
  int fd_dvr=0;
  int do_info=0;
  int do_scan=0;
  int do_monitor=0;
	
  unsigned int freq=0;
  char pol=0;
  unsigned int srate=0;
  unsigned char diseqc = 1;
  int ttpid=0;
  int dpid=0;

  fe_modulation_t modulation=CONSTELLATION_DEFAULT;
  fe_transmit_mode_t TransmissionMode=TRANSMISSION_MODE_DEFAULT;
  fe_bandwidth_t bandWidth=BANDWIDTH_AUTO;
  fe_guard_interval_t guardInterval=GUARD_INTERVAL_DEFAULT;
  fe_code_rate_t HP_CodeRate=HP_CODERATE_DEFAULT;
  fe_code_rate_t fec = FEC_AUTO;
  int count;
  transponder_t * t;

  int i;
  
  if (argc==1) {
    fprintf(stderr,"Usage: dvbtune [OPTIONS]\n\n");
    fprintf(stderr,"Standard options:\n\n");
    fprintf(stderr,"-f freq     absolute Frequency (DVB-S in KHz or DVB-T in Hz)\n");
    fprintf(stderr,"            or L-band Frequency (DVB-S in Hz or DVB-T in Hz)\n");
    fprintf(stderr,"-p [H,V]    Polarity (DVB-S only)\n");
    fprintf(stderr,"-s N        Symbol rate (DVB-S or DVB-C)\n");
    fprintf(stderr,"-v vpid     Decode video PID (full cards only)\n");
    fprintf(stderr,"-a apid     Decode audio PID (full cards only)\n");
    fprintf(stderr,"-t ttpid    Decode teletext PID (full cards only)\n");
    fprintf(stderr,"-pnr N      Tune to Program Number (aka service) N\n\n");
    fprintf(stderr,"-i          Dump SI information as XML\n");

    fprintf(stderr,"\nAdvanced tuning options:\n\n");
    fprintf(stderr,"-c [0-3]    Use DVB card #[0-3]\n");
    fprintf(stderr,"-I [0|1|2]  0=Spectrum Inversion off, 1=Spectrum Inversion on, 2=auto\n");
    fprintf(stderr,"-D [1-4AB]    DiSEqC command\n\n");
    fprintf(stderr,"-8psk       Use 8PSQK modulation (DVB-S2)\n");
    fprintf(stderr,"-qam X      DVB-T modulation - 16%s, 32%s, 64%s, 128%s or 256%s\n",(CONSTELLATION_DEFAULT==QAM_16 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_32 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_64 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_128 ? " (default)" : ""),(CONSTELLATION_DEFAULT==QAM_256 ? " (default)" : ""));
    fprintf(stderr,"-gi N       DVB-T guard interval 1_N (N=128%s, 32%s, 16%s, 8%s or 4%s)\n",(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_128 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_32 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_16 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_8 ? " (default)" : ""),(GUARD_INTERVAL_DEFAULT==GUARD_INTERVAL_1_4 ? " (default)" : ""));
    fprintf(stderr,"-cr N       DVB-T code rate. N=AUTO%s, 1_2%s, 2_3%s, 3_4%s, 5_6%s, 7_8%s\n",(HP_CODERATE_DEFAULT==FEC_AUTO ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_1_2 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_2_3 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_3_4 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_5_6 ? " (default)" : ""),(HP_CODERATE_DEFAULT==FEC_7_8 ? " (default)" : ""));
    fprintf(stderr,"-bw N       DVB-T bandwidth (Mhz) - N=6%s, 7%s or 8%s\n",(BANDWIDTH_DEFAULT==BANDWIDTH_6_MHZ ? " (default)" : ""),(BANDWIDTH_DEFAULT==BANDWIDTH_7_MHZ ? " (default)" : ""),(BANDWIDTH_DEFAULT==BANDWIDTH_8_MHZ ? " (default)" : ""));
    fprintf(stderr,"-tm N       DVB-T transmission mode - N=2%s or 8%s\n",(TRANSMISSION_MODE_DEFAULT==TRANSMISSION_MODE_2K ? " (default)" : ""),(TRANSMISSION_MODE_DEFAULT==TRANSMISSION_MODE_8K ? " (default)" : ""));

    fprintf(stderr,"-x          Attempt to auto-find other transponders (experimental - DVB-S only)\n");
    fprintf(stderr,"-m          Monitor the reception quality\n");
    fprintf(stderr,"-n dpid     Add network interface and receive MPE on PID dpid\n");
    fprintf(stderr,"\n");
    return(-1);
  } else {
    count=0;
    for (i=1;i<argc;i++) {
      if (strcmp(argv[i],"-f")==0) {
        i++;
        freq=atoi(argv[i]);
        if (freq < 13000) { freq *= 1000UL; }
      } else if (strcmp(argv[i],"-i")==0) { // 
        do_info=1;
      } else if (strcmp(argv[i],"-m")==0) { // 
        do_monitor=1;
      } else if (strcmp(argv[i],"-n")==0) { // 
        i++;
        dpid=atoi(argv[i]);
      } else if (strcmp(argv[i],"-c")==0) { // 
        i++;
        card=atoi(argv[i]);
        if ((card < 0) || (card >= MAX_CARDS)) {
	  fprintf(stderr,"card must be between 0 and %d\n",MAX_CARDS-1);
          exit(-1);
        }
      } else if (strcmp(argv[i],"-8psk")==0) { // 
        modulation = PSK_8;
      } else if (strcmp(argv[i],"-x")==0) { // 
        do_scan=1;
      } else if (strcmp(argv[i],"-v")==0) {
        i++;
        vpid=atoi(argv[i]);
      } else if (strcmp(argv[i],"-pnr")==0) {
        i++;
        pnr=atoi(argv[i]);
        do_info=1;
      } else if (strcmp(argv[i],"-a")==0) {
        i++;
        apid=atoi(argv[i]);
      } else if (strcmp(argv[i],"-t")==0) {
        i++;
        ttpid=atoi(argv[i]);
      } else if (strcmp(argv[i],"-p")==0) {
        i++;
        if (argv[i][1]==0) {
	  if (tolower(argv[i][0])=='v') {
            pol='V';
          } else if (tolower(argv[i][0])=='h') {
            pol='H';
          } else if (tolower(argv[i][0])=='l') {
            pol='L';
          } else if (tolower(argv[i][0])=='r') {
            pol='R';
          }
        }
      } else if (strcmp(argv[i],"-s")==0) {
        i++;
        srate=atoi(argv[i])*1000UL;
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
          case 128: guardInterval=GUARD_INTERVAL_1_128; break;
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
      } else if (strcmp(argv[i],"-s2")==0) {
        fprintf(stderr,"Using SYS_DVBS2\n");
        sys = SYS_DVBS2;
        modulation = QPSK;
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
        } else if (!strcmp(argv[i],"8_9")) {
          fec=FEC_8_9;
        } else {
          fprintf(stderr,"Invalid Code Rate: %s\n",argv[i]);
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
      } else if (strcmp(argv[i],"-D")==0) {
        i++;
	diseqc = argv[i][0];
	if(toupper(diseqc) == 'A')
	    diseqc = 'A';
	else if(toupper(diseqc) == 'B')
	    diseqc = 'B';
	else if(diseqc >= '1' && diseqc <= '4') {
    	    diseqc=diseqc - '0';
	}
	else {
		fprintf(stderr,"DiSEqC must be between 1 and 4 or A | B\n");
        	exit(-1);
    	}
      } else if (strcmp(argv[i],"-I")==0) {
        i++;
        if (atoi(argv[i])==0)
           specInv = INVERSION_OFF;
	else if (atoi(argv[i])==1)
           specInv = INVERSION_ON;
        else
           specInv = INVERSION_AUTO;
      }
    }
  }

#if 0
  if (!((freq > 100000000) || ((freq > 0) && (pol!=0) && (srate!=0)))) {
    fprintf(stderr,"Invalid parameters\n");
    exit(-1);
  }
#endif

  if((fd_dvr = open(dvrdev[card],O_RDONLY|O_NONBLOCK)) < 0){
      fprintf(stderr,"FD %d: ",i);
      perror("fd_dvr DEMUX DEVICE: ");
      return -1;
  }

  if((fd_frontend = open(frontenddev[card],O_RDWR|O_NONBLOCK)) < 0){
      fprintf(stderr,"frontend: %d",i);
      perror("FRONTEND DEVICE: ");
      return -1;
  }

  if((fd_demuxrec = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if((fd_demuxv = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if((fd_demuxa = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if((fd_demuxtt = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if((fd_demuxd = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if((fd_demuxsi = open(demuxdev[card],O_RDWR|O_NONBLOCK)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
  }

  if (freq > 0) {
    /* Stop the hardware filters */
    set_apid(0);
    set_vpid(0);
    set_ttpid(0);

    if ((pol==0) || (srate==0)) { sys = SYS_DVBT; } 

    if ((sys==SYS_DVBS2) || (sys==SYS_DVBS)) {
      if (sys==SYS_DVBS2) { fprintf(stderr,"Calling tune_it_s2 with sys=SYS_DVBS2\n"); }
      else { fprintf(stderr,"Calling tune_it_s2 with sys=SYS_DVBS\n"); }

      if (tune_it_s2(fd_frontend,sys,freq,srate,pol,tone,specInv,diseqc,modulation,fec,TransmissionMode,guardInterval,bandWidth) < 0) {
        fprintf(stderr,"First tuning attempt failed, trying again...\n");
        if (tune_it_s2(fd_frontend,sys,freq,srate,pol,tone,specInv,diseqc,modulation,fec,TransmissionMode,guardInterval,bandWidth) < 0) {
          return -1;
        }
      }
    } else {
      fprintf(stderr,"Tuning with diseqc=%d\n",diseqc);
      if (tune_it(fd_frontend,freq,srate,pol,tone,specInv,diseqc,modulation,(sys == SYS_DVBT ? HP_CodeRate : fec),TransmissionMode,guardInterval,bandWidth) < 0) {
        fprintf(stderr,"First tuning attempt failed, trying again...\n");
        if (tune_it(fd_frontend,freq,srate,pol,tone,specInv,diseqc,modulation,(sys == SYS_DVBT ? HP_CodeRate : fec),TransmissionMode,guardInterval,bandWidth) < 0) {
          return -1;
        }
      }
    }
  }

  if (do_scan) {
    printf("<?xml version=\"1.0\" encoding=\"iso-8859-1\"?>\n<satellite>\n");
    scan_nit(0x40); /* Get initial list of transponders */
    scan_nit(0x41); /* Get initial list of transponders */
    while ((t=get_unscanned(transponders))!=NULL) {
      free_pat_list();
      fprintf(stderr,"Scanning %d%c %d\n",t->freq,t->pol,t->srate);
      tune_it_s2(fd_frontend,sys,t->freq,t->srate,t->pol,tone,specInv,0,modulation,HP_CodeRate,TransmissionMode,guardInterval,bandWidth);
      printf("<transponder id=\"%d\" onid=\"%d\" freq=\"%05d\" srate=\"%d\" pos=\"%04x\" we_flag=\"%d\" polarity=\"%c\" modulation=\"%d\">\n",t->id,t->onid,t->freq,t->srate,t->pos,t->we_flag,t->pol,t->mod);
      t->scanned=1;
      scan_pat();
      scan_sdt();
      printf("</transponder>\n");
      scan_nit(0x40); /* See if there are any new transponders */
      scan_nit(0x41); /* See if there are any new transponders */
    }
    printf("</satellite>\n");
  }

  if (do_info) {
    if (pol!=0) {
      printf("<transponder type=\"S\" freq=\"%d\" srate=\"%d\" polarity=\"%c\" diseqc=\"%d\" card=\"%d\" system=\"%s\">\n",freq,srate,pol,diseqc,card,delivery_systems[sys]);
    } else {
      if (srate!=0) {
        printf("<transponder type=\"C\" freq=\"%d\" srate=\"%d\">\n",freq,srate);
      } else {
        if (freq<1000000) freq*=1000UL;
        printf("<transponder type=\"T\" freq=\"%d\">\n",freq);
      }
    }
    scan_pat();
    scan_sdt();
//    scan_nit(0x40);
    printf("</transponder>\n");
  }

  if ((vpid!=0) || (apid!=0) || (ttpid!=0)) {
    set_vpid(vpid);
    set_apid(apid);
    set_ttpid(ttpid);
    fprintf(stderr,"A/V/TT Filters set\n");
  }

  if (dpid > 0) {
    char devnamen[80];
    int dev, fdn;
    struct dvb_net_if netif;

    dev = card;
    netif.pid = dpid;
    netif.if_num = 0;  // always choosen the next free number

    sprintf(devnamen,"/dev/dvb/adapter%d/net0",dev);
    //printf("Trying to open %s\n",devnamen);
    if((fdn = open(devnamen,O_RDWR|O_NONBLOCK)) < 0) {
      fprintf(stderr, "Failed to open DVB NET DEVICE");
      close(fd_frontend);
    } else {
      // Add the network interface
      ioctl( fdn,NET_ADD_IF,&netif);

      close (fdn);
      printf("Successfully opened network device, please configure the dvb interface\n");
    }
  }

  if (do_monitor) {
        int32_t strength, ber, snr, uncorr;
        fe_status_t festatus;

        if((fd_frontend = open(frontenddev[card],O_RDONLY|O_NONBLOCK)) < 0){
                fprintf(stderr,"frontend: %d",i);
                perror("FRONTEND DEVICE: ");
                return -1;
        }

        // Check the signal strength and the BER
        while (1) {
                festatus = 0; strength = 0; ber = 0; snr = 0; uncorr = 0;
                FEReadBER(fd_frontend, &ber);
                FEReadSignalStrength(fd_frontend, &strength);
                FEReadSNR(fd_frontend, &snr);
                FEReadUncorrectedBlocks(fd_frontend, &uncorr);
                ioctl(fd_frontend,FE_READ_STATUS,&festatus);
                fprintf(stderr,"Signal=%d, Verror=%d, SNR=%ddB, BlockErrors=%d, (", strength, ber, snr, uncorr);
		if (festatus & FE_HAS_SIGNAL) fprintf(stderr,"S|");
		if (festatus & FE_HAS_LOCK) fprintf(stderr,"L|");
		if (festatus & FE_HAS_CARRIER) fprintf(stderr,"C|");
		if (festatus & FE_HAS_VITERBI) fprintf(stderr,"V|");
		if (festatus & FE_HAS_SYNC) fprintf(stderr,"SY|");
		fprintf(stderr,")\n");
                sleep(1);
        }
  }


  close(fd_frontend);
  return(0);
}
