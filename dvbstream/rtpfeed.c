/*
 * rtpfeed.c: get an rtp unicast/multicast/broacast stream and feed
 * the DVB-S card with it
 * Author: Guenter Wildmann
 *
 * Parts taken from dumprtp.c by David Podeur
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 *
 * The author can be reached at g.wildmann@it-lab.at, 
 */


// Linux includes:
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <resolv.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#include "rtp.h"

// DVB includes:
#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>


void dumprtp(int socket, int fd_dvr) {
  char* buf;
  struct rtpheader rh;
  int lengthData;
  int written;

  while(1) {
    getrtp2(socket,&rh, &buf,&lengthData);

    written = 0;
    while(written < lengthData){
	written += write(fd_dvr,buf+written, lengthData-written);
    }//end while

  }//end while
}// end dumprtp


void set_ts_filt(int fd,uint16_t pid, int type)
{
  struct dmx_pes_filter_params pesFilterParams;

  pesFilterParams.pid     = pid;
  pesFilterParams.input   = DMX_IN_DVR;
  pesFilterParams.output  = DMX_OUT_DECODER;
  if (type==1) pesFilterParams.pes_type = DMX_PES_VIDEO;
  if (type==2) pesFilterParams.pes_type = DMX_PES_AUDIO;
  pesFilterParams.flags   = DMX_IMMEDIATE_START;

  if (ioctl(fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)  {
    fprintf(stderr,"FILTER %i: ",pid);
    perror("DMX SET PES FILTER");
  }// end if
}// end set_ts_filt


int main(int argc, char *argv[]) {

// filedescriptors for video, audio and dvr-device
  int fda,fdv;
  int fd_dvr;

// pids for video and audio stream  
  uint16_t vpid = 0;
  uint16_t apid = 0;

  struct sockaddr_in si;
  int socketIn;

  char *ip = "224.0.1.2";
  int port = 5004;

// process command-line arguments
  static struct option long_options[]={
    {"group", required_argument, NULL, 'g'},
    {"port", required_argument, NULL, 'p'},
    {"vpid", required_argument, NULL, 'v'},
    {"apid", required_argument, NULL, 'a'},
    {"help", no_argument, NULL, 'h'},
    {0}
  };

  int c;
  int option_index = 0;

  fprintf(stderr,"*** rtpfeed 0.1 ***\n");

  while((c = getopt_long(argc, argv, "g:p:v:a:h",long_options, &option_index))!=-1)
  {
    switch(c)
    {
    case 'g':
      ip = optarg;
      break;
    case 'p':
      port = atoi(optarg);
      break;
    case 'v':
      vpid = atoi(optarg);
      break;
    case 'a':
      apid = atoi(optarg);
      break;
    case 'h':
      fprintf(stderr,"Usage: %s [-g group] [-p port] [-v video PID] [-a audio PID] \n",argv[0]);
      exit(1);
    }// end switch
  }// end while


// open dvr device for output			
  if((fd_dvr = open("/dev/ost/dvr",O_WRONLY)) < 0){
	perror("DVR DEVICE: ");
	return -1;
  }

// open video device, set filters
  if(vpid!=0){
    if((fdv = open("/dev/ost/demux",O_RDWR|O_NONBLOCK)) < 0){
      perror("DEMUX DEVICE: ");
      return -1;
    }// end if

    set_ts_filt(fdv, vpid, 1);
  }// end if

// open audio device, set filters
  if(apid!=0){
    if((fda = open("/dev/ost/demux",O_RDWR|O_NONBLOCK)) < 0){
      perror("DEMUX DEVICE: ");
      return -1;
    }// end if
    set_ts_filt(fda, apid, 2);
  }// end if

  socketIn  = makeclientsocket(ip,port,2,&si);
  dumprtp(socketIn, fd_dvr);

  close(socketIn);
  return(0);
}
