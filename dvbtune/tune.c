/* dvbtune - tune.c

   Copyright (C) Dave Chapman 2001,2002
  
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

*/
   
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include "tune.h"
#include "diseqc.h"

static const char* fe_pilot_tab[] = {
  "PILOT_ON",
  "PILOT_OFF",
  "PILOT_AUTO",
};

static const char* fe_rolloff_tab[] = {
  "ROLLOFF_35",
  "ROLLOFF_20",
  "ROLLOFF_25",
  "ROLLOFF_AUTO"
};


static const char* fe_delivery_system_tab[] = {
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
  "SYS_DAB",
  "SYS_DVBT2",
  "SYS_TURBO"
};


static const char* fe_spectral_inversion_tab[] = {
  "INVERSION_OFF",
  "INVERSION_ON",
  "INVERSION_AUTO"
};


static const char* fe_code_rate_tab[] = {
  "FEC_NONE",
  "FEC_1_2",
  "FEC_2_3",
  "FEC_3_4",
  "FEC_4_5",
  "FEC_5_6",
  "FEC_6_7",
  "FEC_7_8",
  "FEC_8_9",
  "FEC_AUTO",
  "FEC_3_5",
  "FEC_9_10",
};


static const char* fe_modulation_tab[] =  {
  "QPSK",
  "QAM_16",
  "QAM_32",
  "QAM_64",
  "QAM_128",
  "QAM_256",
  "QAM_AUTO",
  "VSB_8",
  "VSB_16",
  "PSK_8",
  "APSK_16",
  "APSK_32",
  "DQPSK"
};

static const char* fe_transmit_mode_tab[] = {
  "TRANSMISSION_MODE_2K",
  "TRANSMISSION_MODE_8K",
  "TRANSMISSION_MODE_AUTO",
  "TRANSMISSION_MODE_4K",
  "TRANSMISSION_MODE_1K",
  "TRANSMISSION_MODE_16K",
  "TRANSMISSION_MODE_32K"
};

static const char* fe_bandwidth_tab[] = {
  "BANDWIDTH_8_MHZ",
  "BANDWIDTH_7_MHZ",
  "BANDWIDTH_6_MHZ",
  "BANDWIDTH_AUTO",
  "BANDWIDTH_5_MHZ",
  "BANDWIDTH_10_MHZ",
  "BANDWIDTH_1_712_MHZ",
};


static const char* fe_guard_interval_tab[] = {
  "GUARD_INTERVAL_1_32",
  "GUARD_INTERVAL_1_16",
  "GUARD_INTERVAL_1_8",
  "GUARD_INTERVAL_1_4",
  "GUARD_INTERVAL_AUTO",
  "GUARD_INTERVAL_1_128",
  "GUARD_INTERVAL_19_128",
  "GUARD_INTERVAL_19_256"
};


static const char* fe_hierarchy_tab[] = {
  "HIERARCHY_NONE",
  "HIERARCHY_1",
  "HIERARCHY_2",
  "HIERARCHY_4",
  "HIERARCHY_AUTO"
};


void print_status(FILE* fd,fe_status_t festatus) {
  fprintf(fd,"FE_STATUS:");
  if (festatus & FE_HAS_SIGNAL) fprintf(fd," FE_HAS_SIGNAL");
  if (festatus & FE_TIMEDOUT) fprintf(fd," FE_TIMEDOUT");
  if (festatus & FE_HAS_LOCK) fprintf(fd," FE_HAS_LOCK");
  if (festatus & FE_HAS_CARRIER) fprintf(fd," FE_HAS_CARRIER");
  if (festatus & FE_HAS_VITERBI) fprintf(fd," FE_HAS_VITERBI");
  if (festatus & FE_HAS_SYNC) fprintf(fd," FE_HAS_SYNC");
  fprintf(fd,"\n");
}

int check_status(int fd_frontend,int type, struct dvb_frontend_parameters* feparams,int hi_lo) {
  int32_t strength;
  fe_status_t festatus;
  struct dvb_frontend_event event;
  struct pollfd pfd[1];
  int status;

  while(1)  {
	if (ioctl(fd_frontend, FE_GET_EVENT, &event) < 0)	//EMPTY THE EVENT QUEUE
	break;
  }
  
  if (ioctl(fd_frontend,FE_SET_FRONTEND,feparams) < 0) {
    perror("ERROR tuning channel\n");
    return -1;
  }

  pfd[0].fd = fd_frontend;
  pfd[0].events = POLLPRI;

  event.status=0;
  while (((event.status & FE_TIMEDOUT)==0) && ((event.status & FE_HAS_LOCK)==0)) {
    fprintf(stderr,"polling....\n");
    if (poll(pfd,1,10000) > 0){
      if (pfd[0].revents & POLLPRI){
        fprintf(stderr,"Getting frontend event\n");
        if ((status = ioctl(fd_frontend, FE_GET_EVENT, &event)) < 0){
	  if (errno != EOVERFLOW) {
	    perror("FE_GET_EVENT");
	    fprintf(stderr,"status = %d\n", status);
	    fprintf(stderr,"errno = %d\n", errno);
	    return -1;
	  }
	  else fprintf(stderr,"Overflow error, trying again (status = %d, errno = %d)", status, errno);
        }
      }
      print_status(stderr,event.status);
    }
  }

  if (event.status & FE_HAS_LOCK) {
      switch(type) {
         case FE_OFDM:
           fprintf(stderr,"Event:  Frequency: %d\n",event.parameters.frequency);
           break;
         case FE_QPSK:
           fprintf(stderr,"Event:  Frequency: %d\n",(unsigned int)((event.parameters.frequency)+(hi_lo ? LOF2 : LOF1)));
           fprintf(stderr,"        SymbolRate: %d\n",event.parameters.u.qpsk.symbol_rate);
           fprintf(stderr,"        FEC_inner:  %d\n",event.parameters.u.qpsk.fec_inner);
           fprintf(stderr,"\n");
           break;
         case FE_QAM:
           fprintf(stderr,"Event:  Frequency: %d\n",event.parameters.frequency);
           fprintf(stderr,"        SymbolRate: %d\n",event.parameters.u.qpsk.symbol_rate);
           fprintf(stderr,"        FEC_inner:  %d\n",event.parameters.u.qpsk.fec_inner);
           break;
         default:
           break;
      }

      strength=0;
      if(ioctl(fd_frontend,FE_READ_BER,&strength) >= 0)
      fprintf(stderr,"Bit error rate: %d\n",strength);

      strength=0;
      if(ioctl(fd_frontend,FE_READ_SIGNAL_STRENGTH,&strength) >= 0)
      fprintf(stderr,"Signal strength: %d\n",strength);

      strength=0;
      if(ioctl(fd_frontend,FE_READ_SNR,&strength) >= 0)
      fprintf(stderr,"SNR: %d\n",strength);

      festatus=0;
      if(ioctl(fd_frontend,FE_READ_STATUS,&festatus) >= 0)
      print_status(stderr,festatus);
    } else {
    fprintf(stderr,"Not able to lock to the signal on the given frequency\n");
    return -1;
  }
  return 0;
}

int tune_it(int fd_frontend, unsigned int freq, unsigned int srate, char pol, int tone, fe_spectral_inversion_t specInv, unsigned char diseqc,fe_modulation_t modulation,fe_code_rate_t HP_CodeRate,fe_transmit_mode_t TransmissionMode,fe_guard_interval_t guardInterval, fe_bandwidth_t bandwidth) {
  int res;
  struct dvb_frontend_parameters feparams;
  int hiband = 0;
  static int uncommitted_switch_pos = 0;
  struct dvb_frontend_info fe_info;
  uint32_t if_freq = 0;

  if ( (res = ioctl(fd_frontend,FE_GET_INFO, &fe_info) < 0)){
     perror("FE_GET_INFO: ");
     return -1;
  }
  
  fprintf(stderr,"Using DVB card \"%s\"\n",fe_info.name);

  switch(fe_info.type) {
    case FE_OFDM:
      if (freq < 1000000) freq*=1000UL;
      feparams.frequency=freq;
      feparams.inversion=INVERSION_OFF;
      feparams.u.ofdm.bandwidth=bandwidth;
      feparams.u.ofdm.code_rate_HP=HP_CodeRate;
      feparams.u.ofdm.code_rate_LP=LP_CODERATE_DEFAULT;
      feparams.u.ofdm.constellation=modulation;
      feparams.u.ofdm.transmission_mode=TransmissionMode;
      feparams.u.ofdm.guard_interval=guardInterval;
      feparams.u.ofdm.hierarchy_information=HIERARCHY_DEFAULT;
      fprintf(stderr,"tuning DVB-T (%s) to %u Hz, Bandwidth: %d\n",DVB_T_LOCATION,freq, bandwidth);
      break;
    case FE_QPSK:
      pol = toupper(pol);
      if (freq < SLOF) {
        feparams.frequency=(freq-LOF1);
        hiband = 0;
      } else {
        feparams.frequency=(freq-LOF2);
        hiband = 1;
      }

      fprintf(stderr,"tuning DVB-S to Freq: %u, Pol:%c Srate=%d, 22kHz tone=%s, LNB: %d\n",feparams.frequency,pol,srate,tone == SEC_TONE_ON ? "on" : "off", diseqc);
      feparams.inversion=specInv;
      feparams.u.qpsk.symbol_rate=srate;
      feparams.u.qpsk.fec_inner=FEC_AUTO;

      setup_switch (fd_frontend,
                   diseqc-1,
                   (toupper(pol)=='V' ? 0 : 1),
                   hiband,
                   uncommitted_switch_pos);

      usleep(50000);
      break;
    case FE_QAM:
      fprintf(stderr,"tuning DVB-C to %d, srate=%d\n",freq,srate);
      feparams.frequency=freq;
      feparams.inversion=INVERSION_OFF;
      feparams.u.qam.symbol_rate = srate;
      feparams.u.qam.fec_inner = FEC_AUTO;
      feparams.u.qam.modulation = modulation;
      break;
    default:
      fprintf(stderr,"Unknown FE type. Aborting\n");
      exit(-1);
  }
  usleep(100000);

  return(check_status(fd_frontend,fe_info.type,&feparams,hiband));
}

#if 0
typedef struct transponder {
	struct list_head list;
	struct list_head services;
	int network_id;
	int original_network_id;
	int transport_stream_id;
	uint32_t frequency;
	uint32_t symbol_rate;
	fe_spectral_inversion_t inversion;
	fe_rolloff_t rolloff;					/* DVB-S */
	fe_code_rate_t fec;						/* DVB-S, DVB-C */
	fe_code_rate_t fecHP;					/* DVB-T */
	fe_code_rate_t fecLP;					/* DVB-T */
	fe_modulation_t modulation;
	fe_bandwidth_t bandwidth;				/* DVB-T */
	fe_hierarchy_t hierarchy;				/* DVB-T */
	fe_guard_interval_t guard_interval;		/* DVB-T */
	fe_transmit_mode_t transmission_mode;	/* DVB-T */
	enum polarisation polarisation;			/* only for DVB-S */
	int orbital_pos;						/* only for DVB-S */
	fe_delivery_system_t delivery_system;
	unsigned int we_flag		  : 1;		/* West/East Flag - only for DVB-S */
	unsigned int scan_done		  : 1;
	unsigned int last_tuning_failed	  : 1;
	unsigned int other_frequency_flag : 1;	/* DVB-T */
	unsigned int wrong_frequency	  : 1;	/* DVB-T with other_frequency_flag */
	int n_other_f;
	uint32_t *other_f;			/* DVB-T freqeuency-list descriptor */
} transponder_t;
#endif

int tune_it_s2(int fd_frontend, fe_delivery_system_t sys,unsigned int freq, unsigned int srate, char pol, int tone, fe_spectral_inversion_t specInv, unsigned char diseqc,fe_modulation_t modulation,fe_code_rate_t HP_CodeRate,fe_transmit_mode_t TransmissionMode,fe_guard_interval_t guardInterval, fe_bandwidth_t bandwidth)
{
    int rc;
    int i;
    fe_status_t s;
    uint32_t if_freq = 0;
    uint32_t bandwidth_hz = 0;
    int hiband = 0;
    int res;
    static int uncommitted_switch_pos = 0;
    struct dvb_frontend_event event;
    struct dvb_frontend_info fe_info;

    struct dtv_property p_clear[] = {
        { .cmd = DTV_CLEAR },
    };

    struct dtv_properties cmdseq_clear = {
        .num = 1,
        .props = p_clear
    };

    //    while(1)  {
    //      if (ioctl(fd_frontend, FE_GET_EVENT, &event) < 0)       //EMPTY THE EVENT QUEUE
    //        break;
    //    }

    if (sys==SYS_DVBS) { modulation = QPSK; }  

    if ((ioctl(fd_frontend, FE_SET_PROPERTY, &cmdseq_clear)) == -1) {
        perror("FE_SET_PROPERTY DTV_CLEAR failed");
	//        return -1;
    }

    if ( (res = ioctl(fd_frontend,FE_GET_INFO, &fe_info) < 0)){
        perror("FE_GET_INFO: ");
        return -1;
    }
  
    fprintf(stderr,"Using DVB card \"%s\"\n",fe_info.name);

    if ((sys==SYS_DVBS2) && (!(fe_info.caps & FE_CAN_2G_MODULATION))) {
      fprintf(stderr,"ERROR: Card does not support DVB-S2\n");
      return -1;
    }

    switch(fe_info.type) 
    {
    case FE_QPSK:
        /* Voltage-controlled switch */
        hiband = 0;

        if (freq < SLOF) {
          if_freq = (freq-LOF1);
	  hiband = 0;
        } else {
          if_freq = (freq-LOF2);
	  hiband = 1;
        }

        fprintf(stderr,"freq=%d, if_freq=%d\n",freq,if_freq);
	fprintf(stderr,"Polarity=%c, diseqc=%d,hiband=%d\n",pol,diseqc,hiband);
        setup_switch (fd_frontend,
                      diseqc-1,
                      (toupper(pol)=='V' ? 0 : 1),
                      hiband,
                      uncommitted_switch_pos);

        usleep(50000);
        break;

    case FE_OFDM:
        if_freq = freq;

        switch(bandwidth) 
        {
        case BANDWIDTH_6_MHZ:    bandwidth_hz = 6000000; break;
        case BANDWIDTH_7_MHZ:    bandwidth_hz = 7000000; break;
        case BANDWIDTH_8_MHZ:    bandwidth_hz = 8000000; break;
        case BANDWIDTH_AUTO:    bandwidth_hz = 0; break;
        default:                bandwidth_hz = 0; break;
        }
        break;

    case FE_QAM:
    case FE_ATSC:
        break;
#if 0
    case SYS_DVBC_ANNEX_B:
    case SYS_DVBC_ANNEX_AC:
        if_freq = freq;

        if (verbosity >= 2){
            dprintf(1,"DVB-C frequency is %d\n", if_freq);
        }
        break;
#endif

    }

    struct dvb_frontend_event ev;
    struct dtv_property p_tune[] = {
        { .cmd = DTV_DELIVERY_SYSTEM,    .u.data = sys },
        { .cmd = DTV_FREQUENCY,            .u.data = if_freq },
        { .cmd = DTV_MODULATION,        .u.data = modulation },
        { .cmd = DTV_SYMBOL_RATE,        .u.data = srate },
        { .cmd = DTV_INNER_FEC,            .u.data = HP_CodeRate },
        { .cmd = DTV_INVERSION,            .u.data = INVERSION_AUTO },
        { .cmd = DTV_ROLLOFF,            .u.data = ROLLOFF_AUTO },
        { .cmd = DTV_PILOT,                .u.data = PILOT_AUTO },
        { .cmd = DTV_TUNE },
    };

    fprintf(stderr,"Tuning: DTV_DELIVERY_SYSTEM    = %s\n",fe_delivery_system_tab[sys]);
    fprintf(stderr,"        DTV_DELIVERY_FREQUENCY = %d (requested: %d)\n",if_freq, freq);
    fprintf(stderr,"        DTV_MODULATION         = %s\n",fe_modulation_tab[modulation]);
    fprintf(stderr,"        DTV_SYMBOL_RATE        = %d\n",srate);
    fprintf(stderr,"        DTV_INNER_FEC          = %s\n",fe_code_rate_tab[HP_CodeRate]);
    fprintf(stderr,"        DTV_INVERSION          = %s\n",fe_spectral_inversion_tab[INVERSION_AUTO]);
    fprintf(stderr,"        DTV_BANDWIDTH_HZ       = %s\n",fe_bandwidth_tab[bandwidth_hz]);
    fprintf(stderr,"        DTV_ROLLOFF            = %s\n",fe_rolloff_tab[ROLLOFF_AUTO]);
    fprintf(stderr,"        DTV_PILOT              = %s\n",fe_pilot_tab[PILOT_AUTO]);

    fprintf(stderr,"Tuning: %d,%d,%d,%d,%d,%d,%d,%d,%d\n",sys,if_freq,modulation,srate,HP_CodeRate,INVERSION_AUTO,ROLLOFF_AUTO,bandwidth_hz,PILOT_AUTO);
    struct dtv_properties cmdseq_tune = {
        .num = sizeof(p_tune)/sizeof(p_tune[0]),
        .props = p_tune
    };

    /* discard stale QPSK events */
    while (1) {
        if (ioctl(fd_frontend, FE_GET_EVENT, &ev) == -1)
            break;
    }

    if ((ioctl(fd_frontend, FE_SET_PROPERTY, &cmdseq_tune)) == -1) {
        perror("FE_SET_PROPERTY TUNE failed");
        return -1;
    }

    // wait for zero status indicating start of tunning
    do {
        ioctl(fd_frontend, FE_GET_EVENT, &ev);
    }
    while(ev.status != 0);

    // Wait for tunning
    for (i = 0; i < 10; i++) {
        usleep (200000);

        if (ioctl(fd_frontend, FE_GET_EVENT, &ev) == -1) {
            // no answer, consider it as not locked situation
            ev.status = 0;
        }

        // Tuning succeed
        if(ev.status & FE_HAS_LOCK) {
            struct dtv_property p[] = {
                { .cmd = DTV_DELIVERY_SYSTEM },
                { .cmd = DTV_MODULATION },
                { .cmd = DTV_INNER_FEC },
                { .cmd = DTV_INVERSION },
                { .cmd = DTV_ROLLOFF },
            };

            struct dtv_properties cmdseq = {
                .num = 5,
                .props = p
            };

            // get the actual parameters from the driver for that channel
            if ((ioctl(fd_frontend, FE_GET_PROPERTY, &cmdseq)) == -1) {
                perror("FE_GET_PROPERTY failed");
                return -1;
            }

#if 0
            t->delivery_system = p[0].u.data;
            t->modulation = p[1].u.data;
            t->fec = p[2].u.data;
            t->inversion = p[3].u.data;
            t->rolloff = p[4].u.data;
#endif

            return 0;
        }
    }

    fprintf(stderr,">>> tuning failed!!!\n");
    return -1;
}
