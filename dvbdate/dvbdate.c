/*

   dvbdate - a program to set the system date and time from a DTT multiplex

   Copyright (C) Laurence Culhane 2002 <dvbdate@holmes.demon.co.uk>

   Mercilessly ripped off from dvbtune, Copyright (C) Dave Chapman 2001

  
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

   Copyright (C) Laurence Culhane 2002 <dvbdate@holmes.demon.co.uk>

   
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

#include <time.h>

#define DVBDATE
#include "dvbdate.h"

#ifdef NEWSTRUCT
#include <linux/dvb/dmx.h>
#define DVB_DEMUX_DEVICE "/dev/dvb/adapter0/demux0"
#else
#include <ost/dmx.h>
#define DVB_DEMUX_DEVICE "/dev/ost/demux0"
#define dmx_sct_filter_params dmxSctFilterParams
#endif

/*
 * return the DTT time in UNIX time_t format
 */

time_t convert_date(char *dvb_buf)
{
  int i;
  int year, month, day, hour, min, sec;
  long int mjd;
  struct tm dvb_time;

  mjd = (dvb_buf[0] & 0xff) << 8;
  mjd += (dvb_buf[1] & 0xff);
  hour = bcdtoint(dvb_buf[2] & 0xff);
  min = bcdtoint(dvb_buf[3] & 0xff);
  sec = bcdtoint(dvb_buf[4] & 0xff);
/*
 * Use the routine specified in ETSI EN 300 468 V1.4.1,
 * "Specification for Service Information in Digital Video Broadcasting"
 * to convert from Modified Julian Date to Year, Month, Day.
 */
  year = (int)((mjd - 15078.2)/365.25);
  month = (int)((mjd - 14956.1 - (int)(year * 365.25))/30.6001);
  day = mjd - 14956 - (int)(year * 365.25) - (int)(month * 30.6001);
  if (month == 14 || month == 15)
    i = 1;
  else
    i = 0;
  year += i;
  month = month - 1 - i * 12;

  dvb_time.tm_sec = sec;
  dvb_time.tm_min = min;
  dvb_time.tm_hour = hour;
  dvb_time.tm_mday = day;
  dvb_time.tm_mon = month - 1;
  dvb_time.tm_year = year;
  dvb_time.tm_isdst = 0;
  dvb_time.tm_wday = 0;
  dvb_time.tm_yday = 0;
  return(mktime(&dvb_time));
}


/*
 * Get the next UTC date packet from the DTT multiplex
 */

time_t scan_date() {
  int fd_date;
  int n,seclen;
  int i;
  time_t t;
  unsigned char buf[4096];
  struct dmx_sct_filter_params sctFilterParams;
  struct pollfd ufd;

  t = 0;
  if((fd_date = open(DVB_DEMUX_DEVICE,O_RDWR|O_NONBLOCK)) < 0){
      perror("fd_date DEVICE: ");
      return -1;
  }

  sctFilterParams.pid=0x14;
  memset(&sctFilterParams.filter.filter,0,DMX_FILTER_SIZE);
  memset(&sctFilterParams.filter.mask,0,DMX_FILTER_SIZE);
  sctFilterParams.timeout = 0;
  sctFilterParams.flags = DMX_IMMEDIATE_START;
  sctFilterParams.filter.filter[0]=0x70;
  sctFilterParams.filter.mask[0]=0xff;

  if (ioctl(fd_date,DMX_SET_FILTER,&sctFilterParams) < 0) {
    perror("DATE - DMX_SET_FILTER:");
    close(fd_date);
    return -1;
  }

  ufd.fd=fd_date;
  ufd.events=POLLPRI;
  if (poll(&ufd,1,10000) < 0) {
     errmsg("TIMEOUT reading from fd_date\n");
     close(fd_date);
     return;
  }
  if (read(fd_date,buf,3)==3) {
    seclen=((buf[1] & 0x0f) << 8) | (buf[2] & 0xff);
    n = read(fd_date,buf+3,seclen);
    if (n==seclen) {
      t = convert_date(&(buf[3]));
    } else {
      errmsg("Under-read bytes for DATE - wanted %d, got %d\n",seclen,n);
      exit(1);
    }
  } else {
    errmsg("Nothing to read from fd_date - try tuning to a multiplex?\n");
    exit(1);
  }
  close(fd_date);
  return(t);
}


/*
 * Set the system time
 */
int set_time(time_t *new_time)
{
  if (stime(new_time)) {
    perror("Unable to set time");
    exit(1);
  }
}


int main(int argc, char **argv)
{
  time_t dvb_time;
  time_t real_time;
  time_t offset;

  do_print = 0;
  do_force = 0;
  do_set = 0;
  do_quiet = 0;
  ProgName = argv[0];

/*
 * Process command line arguments
 */
  do_options(argc, argv);
  if (do_quiet && do_print) {
    errmsg("quiet and print options are mutually exclusive.\n");
    exit(1);
  }
/*
 * Get the date from the currently tuned DTT multiplex
 */
  dvb_time = scan_date();
  if (dvb_time == 0) {
    errmsg("Unable to get time from multiplex.\n");
    exit(1);
  }
  time(&real_time);
  offset = dvb_time - real_time;
  if (do_print) {
    fprintf(stdout, "System time: %s", ctime(&real_time));
    fprintf(stdout, "   DTT time: %s", ctime(&dvb_time));
    fprintf(stdout, "     Offset: %d seconds\n", offset);
  } else if (!do_quiet) {
    fprintf(stdout, "%s", ctime(&dvb_time));
  }
  if (do_set) {
    if (labs(offset) > ALLOWABLE_DELTA) {
      if (do_force) {
        set_time(&dvb_time);
      } else {
	errmsg("multiplex time differs by more than %d from system.\n", ALLOWABLE_DELTA);
	errmsg("use -f to force system clock to new time.\n");
        exit(1);
      }
    } else {
      set_time(&dvb_time);
    }
  } /* #end if (do_set) */
  return(0);
}

