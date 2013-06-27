#ifndef _TUNE_H
#define _TUNE_H

#include <linux/dvb/frontend.h>

#include "dvb_defaults.h"

int tune_it(int fd_frontend, unsigned int freq, unsigned int srate, char pol, int tone, fe_spectral_inversion_t specInv, unsigned char diseqc,fe_modulation_t modulation,fe_code_rate_t HP_CodeRate,fe_transmit_mode_t TransmissionMode,fe_guard_interval_t guardInterval, fe_bandwidth_t bandwidth);
int tune_it_s2(int fd_frontend, fe_delivery_system_t sys,unsigned int freq, unsigned int srate, char pol, int tone, fe_spectral_inversion_t specInv, unsigned char diseqc,fe_modulation_t modulation,fe_code_rate_t HP_CodeRate,fe_transmit_mode_t TransmissionMode,fe_guard_interval_t guardInterval, fe_bandwidth_t bandwidth);

#endif
