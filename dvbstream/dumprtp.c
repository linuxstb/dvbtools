/*
 * dumprtp.c: get an rtp unicast/multicast/broacast stream and outputs it
 * Author: David Podeur for Convergence integrated media GmbH
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
 * The author can be reached at david@convergence.de, 
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <resolv.h>
#include <unistd.h>

#include "rtp.h"

void dumprtp(int socket) {
  char* buf;
  struct rtpheader rh;
  int lengthData;
  unsigned short seq=0;
  int flag=0;

  while(1) {
    getrtp2(socket,&rh, &buf,&lengthData);
    if (flag==0) { seq=rh.b.sequence; flag=1; }
    if (seq!=rh.b.sequence) {
      fprintf(stderr,"rtptsaudio: NETWORK CONGESTION - expected %d, received %d\n",seq,rh.b.sequence);
      seq=rh.b.sequence;
    }
    seq++;
    write(1,buf,lengthData);
  }
}

int main(int argc, char *argv[]) {

  struct sockaddr_in si;
  int socketIn;

  char *ip;
  int port;

  fprintf(stderr,"Rtp dump\n");

  if (argc == 1) {
    ip   = "224.0.1.2";
    port = 5004;
  }
  else if (argc == 3) {
    ip   = argv[1];
    port = atoi(argv[2]);
  }
  else {
    fprintf(stderr,"Usage %s ip port\n",argv[0]);
    exit(1);
  }

  fprintf(stderr,"Using %s:%d\n",ip,port);

  socketIn  = makeclientsocket(ip,port,2,&si);
  dumprtp(socketIn);

  close(socketIn);
  return(0);
}
