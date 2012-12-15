/*
   dvbtscut - a simplistic DVB Transport Stream file cutter

   File: dvbtscut.c

   Copyright (C) Dave Chapman 2003
  
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

/* Structure of Transport Stream (from ISO/IEC 13818-1):

transport_packet() {
  sync_byte                                   8    bslbf       0
  transport_error_indicator                   1    bslbf       1
  payload_unit_start_indicator                1    bslbf       1
  transport_priority                          1    bslbf       1
  PID                                         13   uimsbf      1,2
  transport_scrambling_control                2    bslbf       3
  adaption_field_control                      2    bslbf       3
  continuity_counter                          4    uimsbf      3
  if (adaption_field_control=='10' || adaption_field_control=='11'){
      adaption_field()
  }
  if (adaption_field_control=='01' || adaption_field_control=='11'){
      for (i=0;i<N;i++){
        data_byte                             8    bslbf
      }
  }
}

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include "dvbtscut.h"

int verbose=0;
uint64_t video_pts=0;
int cut_audio=1;

char pts_text[30];
char* pts2hmsu(uint64_t pts,char sep) {
  int h,m,s,u;

  pts/=90; // Convert to milliseconds
  h=(pts/(1000*60*60));
  m=(pts/(1000*60))-(h*60);
  s=(pts/1000)-(h*3600)-(m*60);
  u=pts-(h*1000*60*60)-(m*1000*60)-(s*1000);

  sprintf(pts_text,"%d:%02d:%02d%c%03d",h,m,s,sep,u);
  return(pts_text);
}

uint64_t get_pes_pts(unsigned char* buf, int n) {
  int i;
  int stream_id;
  int PES_packet_length;
  int PES_header_data_length;
  int PTS_DTS_flags;
  unsigned char PES_flags;
  uint64_t PTS;
  uint64_t p0,p1,p2,p3,p4;


//  buf=stream->pesbuf;
//  n=stream->peslength;

  if ((buf[0]!=0) || (buf[1]!=0) || (buf[2]!=1)) {
    //fprintf(stderr,"PES ERROR: does not start with 0x000001 - %02x %02x %02x %02x %02x\n",buf[0],buf[1],buf[2],buf[3],buf[4]);
    return(0);
  }

  i=3;
  stream_id=buf[i++];
  PES_packet_length=(buf[i]<<8)||buf[i+1];
  i+=2;

  PES_flags=buf[i++];
  PES_flags=buf[i++];
  PES_header_data_length=buf[i++];

  if ((stream_id==0xbd) 
  || ((stream_id&0xe0)==0xe0) 
  || ((stream_id&0xc0)==0xc0)) {
    PTS_DTS_flags=(buf[7]&0xb0)>>6;
    if ((PTS_DTS_flags&0x02)==0x02) { 
      //    fprintf(stderr,"PTS bytes: %02x %02x %02x %02x %02x\n",buf[9],buf[10],buf[11],buf[11],buf[12]);
      p0=(buf[13]&0xfe)>>1|((buf[12]&1)<<7);   // Bits 0-7  yes
      p1=(buf[12]&0xfe)>>1|((buf[11]&2)<<6);   // Bits 15-8 
      p2=(buf[11]&0xfc)>>2|((buf[10]&3)<<6);   // Bits 23-16
      p3=(buf[10]&0xfc)>>2|((buf[9]&6)<<5);    // Bits 31-24
      p4=(buf[9]&0x08)>>3;    // Bit 32

      PTS=p0|(p1<<8)|(p2<<16)|(p3<<24)|(p4<<32);
    } else {
      PTS=0;
      //      fprintf(stderr,"WARNING: pid=%d, stream_id=0x%02x, no PTS value in PES packet.\n",stream->pid,stream_id);
    }

    return(PTS);
  } else {
    return(0);
  }
}

void parse_ts_packet(unsigned char* buf, stream_info_t* stream) {
  int i;
  int adaption_field_control;
  int adaption_field_length;
  int splicing_point_flag;

  stream->payload_start=buf[1]&0x40;

  if (stream->payload_start) {
    stream->pid=(((buf[1]&0x1f)<<8) | buf[2]);

    adaption_field_control=(buf[3]&0x30)>>4;
    if (adaption_field_control==3) {
      adaption_field_length=buf[4]+1;
    } else if (adaption_field_control==2) {
      adaption_field_length=183+1;
    }
    splicing_point_flag=(buf[5]&0x04);

    i=4;
    if ((adaption_field_control==2) || (adaption_field_control==3)) {
      // Adaption field!!!
      adaption_field_length=buf[i++];
      if (adaption_field_length > 182) {
        fprintf(stderr,"ERROR: Adaption field length > 182.\n");
        exit(0);
      }
      i+=adaption_field_length;
    }

    if ((adaption_field_control==1) || (adaption_field_control==3)) {
      stream->PTS=get_pes_pts(&buf[i],188-i);
      if (stream->first_PTS==0) { 
        stream->first_PTS=stream->PTS; 
	fprintf(stderr,"Stream %d - first PTS is %s\n",stream->pid,pts2hmsu(stream->first_PTS,'.'));
      }
      if (stream->PTS==0) {
        fprintf(stderr,"\n[INFO] Stream %d is not a PES stream\n",stream->pid);
        stream->is_pes = 0;
      }
    }
  }
}

// Format: hh:mm:ss.nnn

int64_t parsepts(char* s) {
  int h,m,sec,us;
  int i,n,j;
  int x;
  int64_t PTS;
  double dd;

  i=0;
  n=strlen(s);

  //fprintf(stderr,"Parsing pts: %s\n",s);
  h=0;
  while (isdigit(s[i]) && (i<n)) {
    h*=10;
    h+=s[i]-'0';
    i++;
  }
  if (i>n) { return(-1); }
  if (s[i]!=':') { return(-1); }
  i++;

  m=0;
  while (isdigit(s[i]) && (i<n)) {
    m*=10;
    m+=s[i]-'0';
    i++;
  }
  if (i>n) { return(-1); }
  if (s[i]!=':') { return(-1); }
  i++;

  sec=0;
  while (isdigit(s[i]) && (i<n)) {
    sec*=10;
    sec+=s[i]-'0';
    i++;
  }
  x=1;
  if (i < n) {
    if (s[i]!='.') { return(-1); }
    i++;
    us=0;
    j=3;
    while (isdigit(s[i]) && (i<n) && j) {
      us*=10;
      x*=10;
      us+=s[i]-'0';
      i++;
      j--;
    }
  }
  //fprintf(stderr,"h=%d\n",h);
  //fprintf(stderr,"m=%d\n",m);
  //fprintf(stderr,"s=%d\n",sec);
  //fprintf(stderr,"us=%d\n",us);
  //fprintf(stderr,"x=%d\n",x);

  dd=(sec*1.0+m*60.0+h*3600.0)*90000.0;
  PTS=dd+(us*90000.0)/x;
  return(PTS);
}

int main(int argc, char** argv) {
  unsigned char buf[188];
  unsigned short pid;
  stream_info_t streams[8192];

  int status=0;
  int n,c;
  int test,verbose;
  int continuity_counter;
  int discontinuity_indicator;
  int adaption_field_control;
  int i;
  int eof=0;
  int shown_start = 0;
  int64_t prev_PTS;
  int64_t start,end;
  int64_t written;

  int avsync = 1;
  int vpid = 69;
  int apid = 68;

  /* Examples:
  # dvbtscut -from 0:10:15.0 -to 0:20:15.0 -i file.ts -o output.ts
  # dvbtscut -t -i file.ts [test file]
  # dvbtscut -to 1:20:41.0 < file.ts > output.ts
  # 
  # Or read from a .cuts file:
  #
  # 0:10:15 0:30:10
  # 0:33:10 0:44:30
  */

  test=0;
  verbose=0;
  start=-1;
  end=-1;

  n=1;
  while (n < argc) {
    if ((!strcmp(argv[n],"-t")) || (!strcmp(argv[n],"--test"))) {
      test=1;
      fprintf(stderr,"Test mode\n");
      n++;
    } else if ((!strcmp(argv[n],"-v")) || (!strcmp(argv[n],"--verbose"))) {
      verbose=1;
      n++;
    } else if ((!strcmp(argv[n],"-s")) || (!strcmp(argv[n],"--start"))) {
      n++;
      if (n < argc) start=parsepts(argv[n]);
      if (start==-1) {
        fprintf(stderr,"ERROR parsing start time\n");
        exit(1);
      }
      n++;
    } else if ((!strcmp(argv[n],"-e")) || (!strcmp(argv[n],"--end"))) {
      n++;
      if (n < argc) end=parsepts(argv[n]);
      if (end==-1) {
        fprintf(stderr,"ERROR parsing end time\n");
        exit(1);
      }
      n++;
    } else {
      n++;
    }
  }

  if ((start==-1) && (end==-1) && (test==0)) {
    fprintf(stderr,"USAGE: dvbtscut [-v] [-t] [-avsync vpid apid] [-s starttime] [-e endtime] < file.ts > cut.ts\n");
    exit(1);
  }

  if (start==-1) {
    fprintf(stderr,"Starting at beginning of stream\n");
  } else {
    fprintf(stderr,"Starting at %s\n",pts2hmsu(start,'.'));
  }
  if (end==-1) {
    fprintf(stderr,"Ending at end of stream\n");
  } else {
    fprintf(stderr,"Ending at %s\n",pts2hmsu(end,'.'));
  }

  // Initialise the streams
  for (i=0;i<8192;i++) {
    streams[i].pid=i;
    streams[i].ts_missing=0;
    streams[i].packet=0;
    streams[i].stream_packets=0;
    streams[i].counter=-1;
    streams[i].displayed_header=0;
    streams[i].is_pes=1;
  }

  if (start==-1) {
    status=1;
    fprintf(stderr,"STARTING TO WRITE OUTPUT STREAM\n");
  } else {
    status=0;
  }
  written=0;
  while (!eof) {
    c=0;
    while (c<188) {
      n=fread(&buf[c],1,188-c,stdin);
      if (n==0) {
        eof=1;
	break;
      }
      c+=n;
    }
    if (eof) break;

    if (buf[0]!=0x47) {
      fprintf(stderr,"FATAL ERROR: incomplete TS packet (I should really look for the start of the next one....)\n");
      break;
    }

    pid=(((buf[1]&0x1f)<<8) | buf[2]);
    continuity_counter=buf[3]&0x0f;
    adaption_field_control=(buf[3]&0x30)>>4;
    discontinuity_indicator=(buf[5]&0x80)>>7;
    /* Firstly, check the integrity of the stream */
    if (streams[pid].counter==-1) {
      streams[pid].counter=continuity_counter;
    } else {
      if ((adaption_field_control!=0) && (adaption_field_control!=2)) {
        streams[pid].counter++; streams[pid].counter%=16;
      }
    }

    if (streams[pid].counter!=continuity_counter) {
      if (discontinuity_indicator==0) {
        fprintf(stderr,"pid=%d, PTS=%s, packet=%d, continuity error: expecting %02x, received %02x\n",pid,pts2hmsu(streams[pid].PTS,'.'),streams[pid].packet, streams[pid].counter,continuity_counter);
        n=(continuity_counter+16-streams[pid].counter)%16;
        streams[pid].ts_missing+=n;
      }
      streams[pid].counter=continuity_counter;
    }

    if (streams[pid].is_pes) {
       /* This can change is_pes to 0 */
       parse_ts_packet(buf,&streams[pid]);
       //fprintf(stderr,"%d PTS=%s\n",pid,pts2hmsu(streams[pid].PTS,'.'));
    }

    if (streams[pid].is_pes) {
      if ((!shown_start) && (streams[pid].PTS)) {
	fprintf(stderr,"First PTS is %s\n",pts2hmsu(streams[pid].PTS,'.'));
        shown_start = 1;
      }
      if (streams[pid].PTS > start) {
        if (status==0) {
          fprintf(stderr,"PTS=%s\r",pts2hmsu(streams[pid].PTS,'.'));
          fprintf(stderr,"\nSTARTING TO WRITE OUTPUT STREAM\n");
        }
        status=1;
      }

      if (streams[pid].PTS > end) {
        if (status!=2) {
          fprintf(stderr,"PTS=%s\r",pts2hmsu(streams[pid].PTS,'.'));
          fprintf(stderr,"\nENDING OUTPUT\n");
        }
        break; //status=2;
      }

      if (streams[pid].payload_start) {
        if ((streams[pid].PTS-prev_PTS) > 90000) {
          fprintf(stderr,"PTS=%s\r",pts2hmsu(streams[pid].PTS,'.'));
          prev_PTS=streams[pid].PTS;
        }
      }
    }

    streams[pid].packet++;
    if ((status==1) && (test==0)) {
      if ((avsync) && (pid==apid) && ((streams[vpid].first_PTS==0) || (streams[apid].PTS < streams[vpid].first_PTS))) {
	fprintf(stderr,"Skipping audio TS packet for apid - audio PTS is %s (%lld)",pts2hmsu(streams[apid].PTS,'.'),streams[apid].PTS);
        fprintf(stderr,", first video PTS is %s (%lld)\n",pts2hmsu(streams[vpid].first_PTS,'.'),streams[vpid].first_PTS);
      } else {
       n=fwrite(buf,1,188,stdout);
       written++;
      }
    }
  }

  if (eof) { fprintf(stderr,"\nEND OF STREAM\n"); }

  i=0;
  for (pid=0;pid<8192;pid++) {
    if (streams[pid].packet) {
      fprintf(stderr,"INFO: PID %d, Processed %d TS packets (%d bytes), %d missing.\n",pid,streams[pid].packet,188*streams[pid].packet,streams[pid].ts_missing);
    }
  }
  fprintf(stderr,"\n\nTotal packets written: %lld (%'lld bytes)\n",written,written*188);
  exit(0);
}
