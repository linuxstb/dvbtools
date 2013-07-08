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

/* The root for recordings - must include the trailing slash */
#define OUTDIR "/data3/DVBARCHIVE/"

// Enable to use PID 8192 instead of setting individual filters:
#define USE_8192

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

#include "../dvbtune/tune.h"
#include "freesat_huffman.h"

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
#define MAX_PIDS_PER_SERVICE 32
#define MAX_PIDS 256

#define writes(f,x) write((f),(x),strlen(x))

/* Signal handling code shamelessly copied from VDR by Klaus Schmidinger 
   - see http://www.cadsoft.de/people/kls/vdr/index.htm */

#define MAX_CARDS 8
static const char* frontenddev[MAX_CARDS]={"/dev/dvb/adapter0/frontend0","/dev/dvb/adapter1/frontend0","/dev/dvb/adapter2/frontend0","/dev/dvb/adapter3/frontend0","/dev/dvb/adapter4/frontend0","/dev/dvb/adapter5/frontend0","/dev/dvb/adapter6/frontend0","/dev/dvb/adapter7/frontend0"};
static const char* dvrdev[MAX_CARDS]={"/dev/dvb/adapter0/dvr0","/dev/dvb/adapter1/dvr0","/dev/dvb/adapter2/dvr0","/dev/dvb/adapter3/dvr0","/dev/dvb/adapter4/dvr0","/dev/dvb/adapter5/dvr0","/dev/dvb/adapter6/dvr0","/dev/dvb/adapter7/dvr0"};
static const char* demuxdev[MAX_CARDS]={"/dev/dvb/adapter0/demux0","/dev/dvb/adapter1/demux0","/dev/dvb/adapter2/demux0","/dev/dvb/adapter3/demux0","/dev/dvb/adapter4/demux0","/dev/dvb/adapter5/demux0","/dev/dvb/adapter6/demux0","/dev/dvb/adapter7/demux0"};

long now;
long real_start_time;
int Interrupted=0;
int card=0;
int include_dsmcc = 0;  // Default is to exclude dsmcc streams

struct event_info_t
{
  uint16_t event_id;
  char title[128];
  char description[4096];
  char crid_episode[128];
  char crid_series[128];
  char crid_recommendation[128];
  time_t starttime;
  time_t actualstart;
  time_t actualend;
  int duration;
  int content_type;
  int running_status;
};

struct service_info_t {
  char name[64];
  int lcn;
  char filename[4096];
  char eitfilename[4096];
  int fd;
  int service_id;
  int pmt_pid;
  int pmt_version;
  int pids[MAX_PIDS_PER_SERVICE];
  int num;
  int npids;
  int nCurrentSeq;
  int pos;
  int port;
  struct event_info_t event;
};

#define MAX_SERVICES 64
int services[65536];
struct service_info_t service_info[MAX_SERVICES];
int nservices;


int nCheckingCont = 0;
int nDropNullPid = 0;
int nTotalContErrors = 0;
int nPacketsSinceLastFileChange = 0;
unsigned long long nTotalPackets = 0ULL;
unsigned long long nFileSizeRotate = 0ULL;
pthread_mutex_t muDataToFile;
pthread_cond_t muconDataToFile;
time_t dvb_datetime = 0;

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

int bcd2dec(unsigned char buf)
{
    return (((buf&0xf0) >> 4) * 10) + (buf & 0x0f);
}

/*
 * return the DTT time in UNIX time_t format
 * Function taken from dvbdate by Laurence Culhane
 */

time_t convert_date(char *dvb_buf)
{
  int i;
  int year, month, day, hour, min, sec;
  long int mjd;
  struct tm dvb_time;

  mjd = (dvb_buf[0] & 0xff) << 8;
  mjd += (dvb_buf[1] & 0xff);
  hour = bcd2dec(dvb_buf[2] & 0xff);
  min = bcd2dec(dvb_buf[3] & 0xff);
  sec = bcd2dec(dvb_buf[4] & 0xff);
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

void parse_descriptors(int info_len,unsigned char *buf, struct event_info_t* event,int service_id) 
{
  int i=0;
  int descriptor_tag,descriptor_length,j,k;

  while (i < info_len) {
    descriptor_tag=buf[i++];
    descriptor_length=buf[i++];
    while (descriptor_length > 0) {
      switch(descriptor_tag) {
        case 0x03: // audio_stream_descriptor
          //printf("<audio_info tag=\"0x03\" info=\"%02x\" />\n",buf[i]);
          i+=descriptor_length;
          descriptor_length=0;
          break;

        case 0x4d: // short_event_descriptor
          //printf("<short_event_descriptor tag=\"0x4d\">\n",descriptor_tag);
          //printf("  <iso_639>%c%c%c</iso_639>\n",buf[i],buf[i+1],buf[i+2]);
          i+=3;
          //printf("  <event_name>");
          j=buf[i++];
          if (buf[i] == 0x1f) { // Freesat-compressed
            freesat_huffman_to_string(event->title,sizeof(event->title),buf + i, j);
            //printf("%s",event->title);
            i += j;
          } else {
            k = 0;
            while (j>0) {
              if ((k < sizeof(event->title)-1) && ((buf[i] < 0x80) || (buf[i] > 0x9f))) { event->title[k++] = buf[i]; }
              //printf("%02x ",buf[i]); 
              i++; j--; 
            }
            event->title[k] = 0;
          }
          //printf("</event_name>\n");
          //printf("  <text>");
          j=buf[i++];
          if (buf[i] == 0x1f) { // Freesat-compressed
            freesat_huffman_to_string(event->description,sizeof(event->description),buf + i, j);
            //printf("%s",event->description);
            i += j;
          } else {
            k = 0;
            while (j>0) {
              if ((k < sizeof(event->description)-1) && ((buf[i] < 0x80) || (buf[i] > 0x9f))) { event->description[k++] = buf[i]; }
              //printf("%02x ",buf[i]); 
              i++; j--; 
            }
            event->description[k] = 0;
          }
          //printf("</text>\n");
          //printf("</short_event_descriptor>\n");
          descriptor_length=0;
          break;
          
        case 0x50: // component_descriptor
	  if (service_id==4544) {
	    fprintf(stderr,"BBC FOUR component_descriptor, length=%d %02x %02x %02x %02x %02x %02x %02x %02x\n",descriptor_length,buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7]);
             
          } else if (service_id==4164) {
             fprintf(stderr,"BBC ONE component_descriptor, length=%d\n",descriptor_length);
          }
          i+=descriptor_length;  // NOT INTERESTING
          descriptor_length=0;
          break;

        case 0x54: // content_descriptor
          //printf("<content_descriptor tag=\"0x54\" content=\"%02x%02x\" />\n",buf[i],buf[i+1]);
          event->content_type = (buf[i] << 8) | buf[i+1];
          i+=descriptor_length;
          descriptor_length=0;
          break;

        case 0x76:  // TV Anytime descriptor          
          // Stores CRID (Content Reference Identifier) for the programme
          // See http://lists.darkskiez.co.uk/pipermail/tvgrabdvb-darkskiez.co.uk/2007-November/000015.html for an implementation and comments
          // e.g. <descriptor tag="0x76" data="c4072f3234374e4c56" text="...247NLV" />
          // e.g. <descriptor tag="0x76" data="c8072f4c324e43354a" text="...L2NC5J" />
          //printf("<crid_descriptor tag=\"0x%02x\">\n",descriptor_tag);
          j = 0; // hack because a declaration can't be the first statment in a case...
          int end = i + descriptor_length;
          while (i < end) {
            int crid_type = (buf[i] & 0xfc) >> 2;
            int crid_location = (buf[i] & 3);
            i++;
            if (crid_location == 0) {
  	      j = buf[i++];
              if (crid_type == 0x31) {
                  memcpy(event->crid_episode,buf + i, j);
                  event->crid_episode[j] = 0;
              } else if (crid_type == 0x32) {
                  memcpy(event->crid_series,buf + i, j);
                  event->crid_series[j] = 0;
              } else if (crid_type == 0x33) {
                  memcpy(event->crid_recommendation,buf + i, j);
                  event->crid_recommendation[j] = 0;
              } else {
                  fprintf(stderr,"***************************************** Unknown crid_type 0x%02x\n",crid_type);
              }
              //printf("    <crid type=\"%d\" location=\"%d\">",crid_type,crid_location);
              while (j > 0) { 
		//printf("%c",buf[i]); 
                 i++ ; j--; 
              }
              //printf("</crid>\n");
            } else {
              // Contains reference to CRI table, not handled...
              i += 2;
            }
          }
          //printf("</crid_descriptor>\n");
          descriptor_length=0;
          break;
        
        default:
          //printf("<descriptor tag=\"0x%02x\" data=\"",descriptor_tag);
          //for (j=0;j<descriptor_length;j++) printf("%02x",buf[i+j]);
          //printf("\" text=\"");
          //for (j=0;j<descriptor_length;j++) printf("%c",(isalnum(buf[i+j]) ? buf[i+j] : '.'));
          //printf("\" />\n");
          i+=descriptor_length;
          descriptor_length=0;
          break;
      }
    }
  }
}

char* RST[] = {
  "Undefined",
  "Not running",
  "Starts in a few seconds",
  "Pausing",
  "Running",
  "Reserved",
  "Reserved",
  "Reserved"
};

void write_eit(struct service_info_t* service_info)
{
  FILE *f = fopen(service_info->eitfilename, "w+b");

  char eitbuffer[65536];
  struct tm start;
  struct tm actualstart;
  struct tm actualend;

  tzset();
  localtime_r(&service_info->event.starttime,&start);
  localtime_r(&service_info->event.actualend,&actualend);
  localtime_r(&service_info->event.actualstart,&actualstart);

  int duration_hours = service_info->event.duration / 3600;
  int duration_mins = (service_info->event.duration - (duration_hours * 3600)) / 60;
  int duration_secs = service_info->event.duration % 60;

  snprintf(eitbuffer,sizeof(eitbuffer),"filename=%s\nid=%d\nname=%s\ndescription=%s\ncontent_type=%04x\ncrid_episode=%s\ncrid_series=%s\ncrid_recommendation=%s\nstart_time=%04d-%02d-%02d %02d:%02d:%02d\nduration=%02d:%02d:%02d\nstart_time_actual=%04d-%02d-%02d %02d:%02d:%02d\nend_time_actual=%04d-%02d-%02d %02d:%02d:%02d\n",
	  service_info->filename,
	  service_info->event.event_id,
	  service_info->event.title,
	  service_info->event.description,
	  service_info->event.content_type,
	  service_info->event.crid_episode,
	  service_info->event.crid_series,
	  service_info->event.crid_recommendation,
          start.tm_year+1900,start.tm_mon+1,start.tm_mday,start.tm_hour,start.tm_min,start.tm_sec,
          duration_hours,duration_mins,duration_secs,
          actualstart.tm_year+1900,actualstart.tm_mon+1,actualstart.tm_mday,actualstart.tm_hour,actualstart.tm_min,actualstart.tm_sec,
          actualend.tm_year+1900,actualend.tm_mon+1,actualend.tm_mday,actualend.tm_hour,actualend.tm_min,actualend.tm_sec
  );

  fwrite(eitbuffer,strlen(eitbuffer),1,f);
  fclose(f);
  fprintf(stderr,"Written EIT information to %s\n",service_info->eitfilename);
}


void create_filename(struct service_info_t* service_info,struct event_info_t* event,char* suffix)
{
   char date1[64];
   char date2[64];
   char base_filename[4096];
   char cleaned_title[4096];

   int i = 0;
   int j = 0;

   while ((event->title[i] != 0) && (i <= sizeof(event->title))) {
     char ch = event->title[i++];
     if ((isalnum(ch)) || (ch == ' ') || (ch == ',') || (ch == '?') || (ch == '-') || (ch == '\'') || (ch == '"') || (ch == '.') || (ch == '&') || (ch == '!')) {
        cleaned_title[j++] = ch;
     } else {
        cleaned_title[j++] = '_';
     }
   }
   cleaned_title[j] = 0;

   struct tm* start = localtime(&event->starttime);
   snprintf(date1,sizeof(date1),"%04d-%02d-%02d_%02d-%02d",start->tm_year+1900,start->tm_mon+1,start->tm_mday,start->tm_hour,start->tm_min);
   struct tm* now = localtime(&dvb_datetime);
   snprintf(date2,sizeof(date2),"%04d-%02d-%02d_%02d-%02d-%02d",now->tm_year+1900,now->tm_mon+1,now->tm_mday,now->tm_hour,now->tm_min,now->tm_sec);

   snprintf(base_filename,4096,OUTDIR "%03d-%s/%s-%s-%d%s-%s",service_info->lcn,service_info->name,
	    date1,cleaned_title,event->event_id,suffix,date2);

   snprintf(service_info->filename,4096,"%s.ts",base_filename);
   snprintf(service_info->eitfilename,4096,"%s.eit",base_filename);
}

struct event_info_t recording_event;
struct event_info_t present_event;
struct event_info_t following_event;


void parse_eit(uint8_t * buf) 
{
  uint8_t table_id;
  uint16_t section_length;
  uint16_t transport_stream_id;
  uint8_t version_number;
  uint8_t current_next_indicator;
  uint8_t section_number;
  uint8_t last_section_number;
  uint16_t original_network_id;
  uint16_t service_id;
  uint8_t segment_last_section_number;
  uint8_t last_table_id;
  int duration;
  uint8_t free_CA_mode;
  uint16_t descriptors_loop_length;
  int i=0;
  struct event_info_t event;

  memset(&event,0,sizeof(event));

  table_id=buf[i];
  i++;
  section_length=((buf[i] & 0x0f) << 8) | (buf[i+1] & 0xff);
  i+=2;
  service_id=(buf[i]<<8) | buf[i+1];
  i+=2;
  version_number=(buf[i]&0x1e)>>1;
  current_next_indicator=buf[i]&0x01;
if (current_next_indicator == 0) {
   fprintf(stderr,"WARNING: current_next_indicator = 0, service_id=%d\n",service_id);
   return;
}
  i++;
  section_number=buf[i];
  i++;
  last_section_number=buf[i];
  i++;
  transport_stream_id=(buf[i]<<8) | buf[i+1];
  i+=2;
  original_network_id=(buf[i]<<8) | buf[i+1];
  i+=2;
  segment_last_section_number=buf[i];
  i++;
  last_table_id=buf[i];
  i++;

  //printf("table_id=0x%02x version %d (section %04d of %04d) last=%d\n",table_id,version_number,section_number,last_section_number,segment_last_section_number);

#if 0
if ((sid==0) || (service_id==sid)) {
  printf("*********************************\n");
  printf("table_id=0x%02x\n",table_id);
  printf("section_length=%d\n",section_length);
  printf("service_id=%d\n",service_id);
  printf("version_number=%d\n",version_number);
  printf("current_next_indicator=%d\n",current_next_indicator);
  printf("section_number=%d\n",section_number);
  printf("last_section_number=%d\n",last_section_number);
  printf("transport_stream_id=0x%04x\n",transport_stream_id);
  printf("original_network_id=0x%04x\n",original_network_id);
  printf("segment_last_section_number=%d\n",segment_last_section_number);
  printf("last_table_id=0x%02x\n",last_table_id);
}
#endif

  while (i < (section_length-1)) {
    event.event_id=(buf[i] << 8) | buf[i+1];
    i+=2;
    event.starttime=(((buf[i]<<8)|buf[i+1])-40587)*86400 + bcd2dec(buf[i+2])*3600 + bcd2dec(buf[i+3])*60 + bcd2dec(buf[i+4]);
    i+=5;
    event.duration = bcd2dec(buf[i]) * 3600 + bcd2dec(buf[i+1]) * 60 + bcd2dec(buf[i+2]);
    i+=3;
    event.running_status=((buf[i]&0xe0)>>5);
    free_CA_mode=(buf[i]&0x10)>>4;
    descriptors_loop_length=((buf[i]&0x0f)<<8) | buf[i+1];
    i+=2;

#if 0
if ((sid==0) || (service_id==sid)) {
    printf("event_id=%d\n",event_id);
    printf("  start_time=0x%02x%02x %02x:%02x:%02x\n",start_time[0],start_time[1],start_time[2],start_time[3],start_time[4]);
    printf("  start_time_unix=%s\n",ctime(&event.starttime));
    printf("  duration=%02x:%02x:%02x\n",duration[0],duration[1],duration[2]);
    printf("  event.running_status=%d\n",event.running_status);
    printf("  free_CA_mode=%d\n",free_CA_mode);
    printf("  descriptors_loop_length=%d\n",descriptors_loop_length);
    parse_descriptors(descriptors_loop_length,&buf[i],event);
    printf("*********************************\n");
}
#endif

    if (services[service_id] >= 0) {
      parse_descriptors(descriptors_loop_length,&buf[i],&event,service_id);
      //      printf("Service %5d, \"%s\" Event 0x%04x ,Content-type: 0x%04x,Status: %s, Start_time %s",service_id,event.title,event.event_id,event.content_type,RST[event.running_status],ctime(&event.starttime));
      if (event.running_status == 4) {
        int k = services[service_id];
        if (service_info[k].filename[0]==0) {
          create_filename(&service_info[k],&event,"-INPROGRESS");
          fprintf(stderr,"Starting to record in-progress event - filename is %s\n",service_info[k].filename);
          service_info[k].event = event;
          service_info[k].event.actualstart = dvb_datetime;
          service_info[k].event.actualend = 0;
          write_eit(&service_info[k]);

          FILE *f = fopen(service_info[k].filename, "w+b");
          service_info[k].fd = fileno(f);
          make_nonblock(service_info[k].fd);
        } else if (service_info[k].event.event_id != event.event_id) {
          service_info[k].event.actualend = dvb_datetime;
          write_eit(&service_info[k]);
          close(service_info[k].fd);
          fprintf(stderr,"Closing file %s\n",service_info[k].filename);

          create_filename(&service_info[k],&event,"");
          fprintf(stderr,"Opening new file: %s\n",service_info[k].filename);
          service_info[k].event = event;
          service_info[k].event.actualstart = dvb_datetime;
          service_info[k].event.actualend = 0;
          write_eit(&service_info[k]);

          FILE *f = fopen(service_info[k].filename, "w+b");
          service_info[k].fd = fileno(f);
          make_nonblock(service_info[k].fd);
        }

      }

      i+=descriptors_loop_length;
    }
  }
}

unsigned char section[4096+188];  // Enough to store a section plus maybe the start of the next section
int current_section_length = 0;
int section_bytes_read = 0;

void process_eit_packet(unsigned char* buf)
{
  int payload_unit_start_indicator;
  int adaption_field_control;
  int pointer_field;
  int done;
  int table_id;

  payload_unit_start_indicator = (buf[1]&0x40)>>6;
  adaption_field_control=(buf[3]&0x30)>>4;

  if ((adaption_field_control==2) || (adaption_field_control==3)) {
      fprintf(stderr,"ERR: Unimplemented adaption_field_control\n");
      return 1;
  }

  if (payload_unit_start_indicator==1) {
     pointer_field = buf[4];
     //printf("pointer_field=0x%02x\n",pointer_field);
     if (section_bytes_read > 0) {
       // If we have any existing data (i.e. we are not resyncing to the stream), copy the end of the previous section
       //printf("1 - Copying %d bytes from buf+5\n",pointer_field);
       memcpy(section + section_bytes_read, buf+5, pointer_field);
       section_bytes_read += pointer_field;
       //dump(section, section_bytes_read);
     }           

     // A new section, so process any existing ones.
     done = 0;
     while ((section_bytes_read > 0) && (!done)) {
         table_id = section[0];
         if (table_id==0xff) {
	   //printf("PADDING: Resetting section_bytes_read to 0\n");
           //dump(section, section_bytes_read);
           section_bytes_read = 0;
           done = 1;
         } else {
           current_section_length=((section[1] & 0x0f) << 8) | (section[2] & 0xff);
           //printf("table_id=0x%02x, section_length=0x%04x (%d), section_bytes_read=%d\n",table_id,current_section_length,current_section_length,section_bytes_read);
           if (current_section_length > section_bytes_read) {
             printf("WARNING: Incomplete section\n");
             //dump(section, section_bytes_read);
             done = 1;
             //section_bytes_read = 0;
           } else {
             //printf("table_id=0x%02x, section_length=0x%04x (%d)\n",table_id,current_section_length,current_section_length);
             //dump(section, current_section_length+3);
             if (table_id==0x4e) parse_eit(section);
             section_bytes_read -= current_section_length+3;
             //printf("New section_bytes_read = %d\n",section_bytes_read);
             memmove(section, section + (current_section_length + 3), section_bytes_read);
             //dump(section, section_bytes_read);
           }
        }
     }

     //printf("2 - Copying %d bytes from buf+0x%02x\n",188-5-pointer_field,5+pointer_field);
     memcpy(section+section_bytes_read, buf + 5 + pointer_field, 188-5-pointer_field);
     section_bytes_read += 188-5-pointer_field;
     //dump(section, section_bytes_read);
  } else {
    // Only copy data if we are inside a section
    if (section_bytes_read > 0) {
      //printf("3 - Copying 184 bytes from buf+4\n");
      memcpy(section+section_bytes_read, buf + 4, 188-4);
      section_bytes_read += 188-4;
      //dump(section, section_bytes_read);
    } else {
      printf("Skipping packet - section_bytes_read==0\n");
    }
  }
}


int open_fe(int* fd_frontend,int card) {

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

void set_ts_filt(int fd,uint16_t pid)
{
  struct dmx_pes_filter_params pesFilterParams;

  pesFilterParams.pid     = pid;
  pesFilterParams.input   = DMX_IN_FRONTEND;
  pesFilterParams.output  = DMX_OUT_TS_TAP;
  pesFilterParams.pes_type = DMX_PES_OTHER; ;
  pesFilterParams.flags   = DMX_IMMEDIATE_START;

  if (ioctl(fd, DMX_SET_PES_FILTER, &pesFilterParams) < 0)  {
    fprintf(stderr,"FILTER %i: ",pid);
    perror("DMX SET PES FILTER");
  }
}

  typedef enum {STREAM_ON,STREAM_OFF} state_t;
  int npids = 0;
#ifdef USE_8192
  int fd;
#else
  int fd[MAX_PIDS];
#endif
  int pids[MAX_PIDS];

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

#define LARGE_BUF_SIZE (20 * 1024 * 1024)
unsigned char pszLargeBuf[LARGE_BUF_SIZE];

void add_pid(struct service_info_t* service_info, int pid)
{
  int i;
  for (i=0;i<service_info->npids;i++) {
    if (service_info->pids[i] == pid) { return; }
  }
  service_info->pids[service_info->npids++] = pid;

  int found=0;
  for (i=0;i<npids;i++) {
    if (pids[i] == pid) { return; }
  }

  if (npids == MAX_PIDS) {
    fprintf(stderr,"Maximum number of PIDs reached, aborting\n");
    exit(1);
  }

  pids[npids] = pid;

#ifndef USE_8192
  if ((fd[npids] = open(demuxdev[card],O_RDWR)) < 0){
    fprintf(stderr,"FD %i: ",npids);
    perror("DEMUX DEVICE: ");
    exit(1);
  }
  set_ts_filt(fd[npids],pid);
#endif

  npids++;
}

void process_pmt_packet(struct service_info_t* service_info, unsigned char* buf)
{
  int i,n;
  if (buf[4] != 0) {
    fprintf(stderr,"Unsupported PAT format - pointer_to_data != 0 (%d)\n",buf[4]);
    exit(1);;
  }
  i = 5;

  int table_id = buf[i++];
  int section_length = ((buf[i]&0x0f) << 8) | buf[i+1]; i += 2;
  if (section_length > 180) { 
    fprintf(stderr,"Multi-packet PMT not supported\n");
    exit(1);
  }
  int transport_stream_id = (buf[i] << 8) | buf[i+1]; i += 2;
  int version_number = (buf[i]&0x3e) >> 1;
  if (version_number == service_info->pmt_version) {
    return;
  }

  fprintf(stderr,"**** PMT version number changed for service %d - was %d, now %d\n",service_info->service_id,service_info->pmt_version,version_number);
  service_info->npids = 2;  // keep PAT and PMT PIDs

  int current_next_indicator = buf[i++] & 0x01;
  int section_number = buf[i++];
  int last_section_number = buf[i++];
  if (last_section_number != 0) {
    fprintf(stderr,"Multi-packet PMT not supported (last_section_number = %d)\n",last_section_number);
    exit(1);
  }

  i += 2; // PCR_PID
  int program_info_length = ((buf[i] & 0x0f) << 8) | buf[i+1]; i += 2;
  i += program_info_length;

  int num_apids = 0;
  while ( i < 7 + section_length - 4) {
    int stream_type = buf[i++];
    int pid = ((buf[i]&0x1f) << 8) | buf[i+1]; i += 2;
    int ES_info_length = ((buf[i] & 0x0f) << 8) | buf[i+1]; i += 2;
    i += ES_info_length;

    fprintf(stderr,"[INFO]    PID %d - stream_type 0x%02x\n",pid,stream_type);
    if ((include_dsmcc == 1) || (stream_type < 10) || (stream_type > 13)) {  // 10, 11, 12, 13 are DSM-CC (ISO 13818-6 types A/B/C/D)
      add_pid(service_info,pid);
    }
  }

  service_info->pmt_version = version_number;

  //  fprintf(stderr,"table_id=%d\nsection_length=%d\ntransport_stream_id=0x%04x\nversion_number=%d\ncurrent_next_indicator=%d\nsection_number=%d\nlast_section_number=%d\n",table_id,section_length,transport_stream_id,version_number,current_next_indicator,section_number,last_section_number);
}

void parse_pmt(int fd, struct service_info_t* service_info)
{
  int i,n;
  unsigned char buf[188];

  // Firstly add PAT amd PMT PIDs to service.
  add_pid(service_info,0);
  add_pid(service_info,service_info->pmt_pid);

  service_info->pmt_version = -1;

  while (1) {
    while ((n = read(fd,buf,188) != 188));

    if (buf[0] != 0x47) { fprintf(stderr,"Error reading PMT, aborting\n"); exit(1); }
    pid = ((buf[1] & 0x1f) << 8) | buf[2];
    if (pid==service_info->pmt_pid) break;
  }

  process_pmt_packet(service_info,buf);
}

void parse_pat(int fd)
{
  unsigned char buf[188];
  int i,n;
  int pid;

  while (1) {
    while ((n = read(fd,buf,188) != 188));

    if (buf[0] != 0x47) { fprintf(stderr,"Error reading PAT, aborting\n"); exit(1); }
    pid = ((buf[1] & 0x1f) << 8) | buf[2];
    if (pid==0) break;    
  }

  if (buf[4] != 0) {
    fprintf(stderr,"Unsupported PAT format - pointer_to_data != 0 (%d)\n",buf[4]);
    exit(1);;
  }
  i = 5;

  int table_id = buf[i++];
  int section_length = ((buf[i]&0x0f) << 8) | buf[i+1]; i += 2;
  if (section_length > 180) { 
    fprintf(stderr,"Multi-packet PAT not supported\n");
    exit(1);
  }

  int transport_stream_id = (buf[i] << 8) | buf[i+1]; i += 2;
  int version_number = (buf[i]&0x3e) >> 1;
  int current_next_indicator = buf[i++] & 0x01;
  int section_number = buf[i++];
  int last_section_number = buf[i++];
  if (last_section_number != 0) {
    fprintf(stderr,"Multi-packet PAT not supported (last_section_number = %d)\n",last_section_number);
    exit(1);
  }

  //fprintf(stderr,"table_id=%d\nsection_length=%d\ntransport_stream_id=0x%04x\nversion_number=%d\ncurrent_next_indicator=%d\nsection_number=%d\nlast_section_number=%d\n",table_id,section_length,transport_stream_id,version_number,current_next_indicator,section_number,last_section_number);
  int nprograms = (section_length - 9) / 4;
  if ((nprograms * 4 + 9) != section_length) {
    fprintf(stderr,"Unexpected section11_length in PAT - %d is not equal to 9 + nprograms * 4\n",section_length);
    exit(1);
  }

  int j;
  for (j = 0 ; j < nprograms; j++) {
    int service_id = (buf[i+j*4] << 8) | buf[i+j*4+1];
    int pmt_pid = ((buf[i+j*4+2]&0x1f) << 8) | buf[i+j*4+3];
    if (services[service_id] >= 0) {
      printf("Program %d PMT PID %d\n",service_id,pmt_pid);
      service_info[services[service_id]].pmt_pid = pmt_pid;
      parse_pmt(fd,&service_info[services[service_id]]);
    }
  }

  fprintf(stderr,"Processed PAT\n");
}

void parse_sdt(int fd)
{
  unsigned char buf[188];
  unsigned char section[1024];
  int sections_processed = 0;
  int i,n;
  int pid;
  int table_id;
  int bytes_read;

  // Find first packet of table 0x42
next_section:
  while (1) {
    while ((n = read(fd,buf,188) != 188));

    if (buf[0] != 0x47) { fprintf(stderr,"Error reading SDT, aborting\n"); exit(1); }
    pid = ((buf[1] & 0x1f) << 8) | buf[2];
    int payload_unit_start_indicator = (buf[1]&0x40)>>6;
    if ((payload_unit_start_indicator) && (pid==0x11)) {
      if (buf[4] != 0) {
        fprintf(stderr,"Unsupported SDT format - pointer_to_data != 0 (%d)\n",buf[4]);
        exit(1);;
      }
      i = 5;

      table_id = buf[i++];
      //fprintf(stderr,"HERE: table_id=0x%02x, section_number=%d\n",table_id,buf[i+5]);
      if ((table_id == 0x42) && (buf[i+5]==sections_processed)) break;
    }
  }

  int section_length = ((buf[i]&0x0f) << 8) | buf[i+1]; i += 2;

  memcpy(section,buf + 5,183);
  bytes_read = 183;

  while (bytes_read < section_length) {
    while ((n = read(fd,buf,188) != 188));

    if (buf[0] != 0x47) { fprintf(stderr,"Error reading SDT, aborting\n"); exit(1); }

    if (buf[0] != 0x47) { fprintf(stderr,"Error reading SDT, aborting\n"); exit(1); }
    pid = ((buf[1] & 0x1f) << 8) | buf[2];
    if (pid==0x11) {
      memcpy(section + bytes_read, buf + 4, 184);
      bytes_read += 184;
    }
  }

    //  fprintf(stderr,"table_id=%d\nsection_length=%d\ntransport_stream_id=0x%04x\nversion_number=%d\ncurrent_next_indicator=%d\nsection_number=%d\nlast_section_number=%d\n",table_id,section_length,transport_stream_id,version_number,current_next_indicator,section_number,last_section_number);

  i = 3;  // Skip Table ID and section_length
  int transport_stream_id = (section[i] << 8) | section[i+1]; i += 2;
  int version_number = (section[i]&0x3e) >> 1;
  int current_next_indicator = section[i++] & 0x01;
  int section_number = section[i++];
  int last_section_number = section[i++];
  int original_network_id = (section[i] << 8) | section[i+1]; i += 2;
  i++; // Reserved

  fprintf(stderr,"Processing SDT:\n");
  while (i < (section_length - 1)) {
    int service_id = (section[i] << 8) | section[i+1]; i += 2;
    i++;  // Reserved and EIT flags
    int running_status = (section[i]&0xe0) >> 5;
    int descriptors_loop_length=((section[i]&0x0f)<<8) | section[i+1]; i+= 2;

    int j = i;
    //fprintf(stderr,"service_id %d, running_status: %s\n",service_id,RST[running_status]);
    if (i + descriptors_loop_length > section_length) {
       fprintf(stderr,"ERROR in SDT: i=%d, j=%d, section_length=%d, descriptors_loop_length=%d\n",i,j,section_length,descriptors_loop_length);
       exit(1);
    }
    i+= descriptors_loop_length;

    int s = services[service_id];
    while (j < i) {
      int descriptor_tag=section[j++];
      int descriptor_length=section[j++];
      int k = j;
      j += descriptor_length;

      if (descriptor_tag == 0x48) {
        int service_type=section[k++];
        int provider_name_length=section[k++];
        k += provider_name_length;
        int service_name_length = section[k++]; 
        if (s >= 0) {
          memcpy(&service_info[s].name,section + k, service_name_length);
          service_info[s].name[service_name_length] = 0;
        }
        unsigned char tmp[128];
        memcpy(tmp,section + k, service_name_length);
        tmp[service_name_length] = 0;
        fprintf(stderr,"Service ID %d, Service type %d, name=\"%s\", running_status=%s%s\n",service_id,service_type,tmp,RST[running_status],((s >= 0) ? " - ARCHIVING" : ""));
      }
    }
  }

  sections_processed++;
  if (sections_processed <= last_section_number) 
    goto next_section;
}

void parse_nit(int fd)
{
  unsigned char buf[188];
  unsigned char section[1024];
  int sections_processed = 0;
  int i,n;
  int pid;
  int table_id;
  int bytes_read;

  // Find first packet of table 0x42
next_section: 
  while (1) {
    while ((n = read(fd,buf,188) != 188));

    if (buf[0] != 0x47) { fprintf(stderr,"Error reading NIT, aborting\n"); exit(1); }
    pid = ((buf[1] & 0x1f) << 8) | buf[2];
    int payload_unit_start_indicator = (buf[1]&0x40)>>6;
    if ((payload_unit_start_indicator) && (pid==0x10)) {
      if (buf[4] != 0) {
        fprintf(stderr,"Unsupported NIT format - pointer_to_data != 0 (%d)\n",buf[4]);
        exit(1);;
      }
      i = 5;

      table_id = buf[i++];
      fprintf(stderr,"NIT: table_id=0x%02x, section_number=%d, sections_processed=%d\n",table_id,buf[i+5],sections_processed);
      if ((table_id == 0x40) && (buf[i+5]==sections_processed)) break;
    }
  }

  int section_length = ((buf[i]&0x0f) << 8) | buf[i+1]; i += 2;
  int last_section_number = buf[i+6];

  memcpy(section,buf + 5,183);
  bytes_read = 183;

  while (bytes_read < section_length) {
    while ((n = read(fd,buf,188) != 188));

    if (buf[0] != 0x47) { fprintf(stderr,"Error reading NIT, aborting\n"); exit(1); }

    if (buf[0] != 0x47) { fprintf(stderr,"Error reading NIT, aborting\n"); exit(1); }
    pid = ((buf[1] & 0x1f) << 8) | buf[2];
    if (pid==0x10) {
      memcpy(section + bytes_read, buf + 4, 184);
      bytes_read += 184;
    }
  }

  i = 3;  // Skip Table ID and section_length
  int network_id = (section[i] << 8) | section[i+1]; i += 2;
  int version_number = (section[i]&0x3e) >> 1;
  int current_next_indicator = section[i++] & 0x01;
  int section_number = section[i++];
  last_section_number = section[i++];

  //fprintf(stderr,"NIT: table_id=0x%02x, section_length=%d,section_number=%d, last_section_number=%d,sections_processed=%d\n", table_id,section_length,section_number,last_section_number,sections_processed);


  int network_descriptors_length = ((section[i]&0x0f) << 8) | section[i+1]; i += 2;

  int j = i;
  i += network_descriptors_length;
  //fprintf(stderr,"network_descriptors_length=%d\n",network_descriptors_length);
#if 0
  while (j < i) {
    int descriptor_tag=section[j++];
    int descriptor_length=section[j++];
    int k = j;
    j += descriptor_length;

    fprintf(stderr,"tag=0x%02x, length=0x%02x\n",descriptor_tag,descriptor_length);
  }
#endif

  int transport_stream_loop_length = ((section[i]&0x0f) << 8) | section[i+1]; i += 2;
  int end = i + transport_stream_loop_length;

  while (i < end) {
    int transport_stream_id = (section[i] << 8) | section[i+1]; i += 2;
    int original_nework_id = (section[i] << 8) | section[i+1]; i += 2;

    int transport_descriptors_length = ((section[i]&0x0f) << 8) | section[i+1]; i += 2;
    int j = i;
    i += transport_descriptors_length;
    //fprintf(stderr,"transport_descriptors_length=%d\n",network_descriptors_length);
    while (j < i) {
      int descriptor_tag=section[j++];
      int descriptor_length=section[j++];
      int k = j;
      j += descriptor_length;

      //fprintf(stderr,"tag=0x%02x, length=0x%02x\n",descriptor_tag,descriptor_length);
      if (descriptor_tag == 0x83) {  // LCN descriptor
          while (k < j) {
            int service_id = (section[k] << 8) | section[k+1]; k+= 2;
            int visible_service_flag = (section[k] & 0x80) >> 7;
            int logical_channel_number = ((section[k] & 3) << 8) | section[k+1] ; k += 2;
            fprintf(stderr,"Service ID %d, visible=%d, lcn=%d\n",service_id,visible_service_flag,logical_channel_number);
            if (services[service_id] >= 0) { service_info[services[service_id]].lcn = logical_channel_number; }
          }
      }
    }
  }

  sections_processed++;
  if (sections_processed <= last_section_number) 
    goto next_section;

}

void process_tdt_packet(unsigned char* buf)
{
  int i;

  int payload_unit_start_indicator = (buf[1]&0x40)>>6;
  if (payload_unit_start_indicator) {
    int adaption_field_control=(buf[3]&0x30)>>4;

    i=4;

    if ((adaption_field_control==2) || (adaption_field_control==3)) {
      // Adaption field!!!
      int adaption_field_length=buf[i++];
      fprintf(stderr,"PID %d - adaption_field_length=%d\n",pid,adaption_field_length);
      if (adaption_field_length > 182) {
        fprintf(stderr,"WARNING: PID=%d, adaption field_length > TS packet size!, adaption_field_control=%d\n",pid,adaption_field_control);
        return 0;
      }
      i+=adaption_field_length;
    }

    if (buf[i] != 0) {
      fprintf(stderr,"Unsupported TDT format - pointer_to_data != 0 (%d)\n",buf[i]);
      exit(1);;
    }

    i++;

    int table_id = buf[i++];
    if (table_id == 0x70) {
      dvb_datetime = convert_date(buf + i + 2);
#if 0
      time_t real_time;
      time(&real_time);
      fprintf(stderr, "System time: %s", ctime(&real_time));
      fprintf(stderr, "   TDT time: %s", ctime(&dvb_datetime));
#endif
    }
  }
}

void parse_tdt(int fd)
{
  unsigned char buf[188];
  int i,j,n;
  int pid;
  int table_id;
  int found_tdt = 0;
  int found_tot = 0;

  // Keep reading TDT until we get a date and time
  while (dvb_datetime == 0) {
    while ((n = read(fd,buf,188) != 188));
    pid = ((buf[1] & 0x1f) << 8) | buf[2];

    if (pid == 0x14) process_tdt_packet(buf);
  }
}

void *FileWriterFunc(void *pszArg) {
  struct EXCHANGE_BUFFER *pszLocalBuffer;

  unsigned char my_cc[8192];
  int i;
  int counter;
  int nHighWaterMark = 0;
  int nTEI = 0;

  for (i=0;i<8192;i++) { my_cc[i] = 0xff; }


  while (1) {
    pthread_mutex_lock(&muDataToFile);
    pthread_cond_wait(&muconDataToFile, &muDataToFile);
    pszLocalBuffer = pExchangeBuffer;
    pExchangeBuffer = NULL;
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
          int pid, i, j, tei, conterror = 0;

          pid = ((buf[1] & 0x1f) << 8) | buf[2];
          tei = (buf[1] & 0x80) >> 7;
          if (tei) {
            nTEI++;
            continue;
          }

	  int discontinuity_indicator=(buf[5]&0x80)>>7;
	  int adaption_field_control=(buf[3]&0x30)>>4;
	  if (my_cc[pid]==0xff) {
	    my_cc[pid]=buf[3]&0x0f;
            fprintf(stderr, "Set first continuity for pid %d to %d\n", pid, my_cc[pid]);
	  } else {
	    if ((adaption_field_control!=0) && (adaption_field_control!=2)) {
	      my_cc[pid]++; my_cc[pid]%=16;
	    }
	  }

          if ((pid != 8191) && (discontinuity_indicator==0) && (my_cc[pid]!=(buf[3]&0x0f))) {
            fprintf(stderr,"PID %d - packet incontinuity - expected %02x, found %02x\n",pid,my_cc[pid],buf[3]&0x0f);
            my_cc[pid]=buf[3]&0x0f;
          }

          if (pid==0x12) {
            process_eit_packet(buf);
          } else if (pid == 0x14) {
            process_tdt_packet(buf);
          }

          nPacketsSinceLastFileChange++;
          for (i = 0; i < nservices; i++) {
            // Check if PMT has changed.
            if (pid == service_info[i].pmt_pid) {
	      process_pmt_packet(&service_info[i],buf);
            }

            for (j = 0; j < service_info[i].npids; j++) {
              if (service_info[i].pids[j] == pid) {
                errno = 0;
                if (service_info[i].fd > 0) {
                  write(service_info[i].fd, pszLocalBuffer->pszBuffer[counter], TS_SIZE);
                }
              }
            }
          }
        } else {
          fprintf(stderr, "NON 0X47\n");
        }
      }
    }
    free(pszLocalBuffer);
  }
  pthread_exit(NULL);
}


int main(int argc, char **argv)
{
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
  //  state_t state=STREAM_OFF;
  int fd_dvr;
  int i,j,k,n;
  unsigned char buf[MTU];
  int nBytesLeftOver = 0;
  struct pollfd pfds[1];  // DVR device
  unsigned long freq=0;
  unsigned long srate=0;
  int count;
  char* ch;
  int bytes_read;
  unsigned char* free_bytes;
  int64_t counts[8192];
  double f;
  struct timeval tv;
  int found;
  int nStrength, nBER, nSNR, nUncorrected, nTEI = 0, nTotalUncorrected = 0;
  int nLNB = 0;

  nservices = 0;
  for (i=0;i<65536;i++) { services[i] = -1; }
  memset(counts,0,sizeof(counts));
  memset(service_info,0,sizeof(service_info));

  fprintf(stderr,"dvbarchive v0.1 - (C) Dave Chapman 2012\n");

  if (argc==1) {
    fprintf(stderr,"Usage: dvbarchive [OPTIONS] service1 service2 ...8\n\n");
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

    fprintf(stderr,"\n");
    fprintf(stderr,"NOTE: Use pid1=8192 to broadcast whole TS stream from a budget card\n");
    return(-1);
  } else {
    for (i=1;i<argc;i++) {
      if (strcmp(argv[i],"-f")==0) {
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
      } else if (strcmp(argv[i],"-c")==0) {
        i++;
        card=atoi(argv[i]);
        if ((card < 0) || (card > 7)) {
          fprintf(stderr,"ERROR: card parameter must be between 0 and %d\n",MAX_CARDS-1);
        }
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
      } else {
        if (nservices == MAX_SERVICES) {
          fprintf(stderr,"Maximum number of services surpassed, aborting\n");
          return 1;
        }
        service_info[nservices].service_id = atoi(argv[i]);
        services[atoi(argv[i])] = nservices++;
      }
    }
  }

  if (signal(SIGHUP, SignalHandler) == SIG_IGN) signal(SIGHUP, SIG_IGN);
  if (signal(SIGINT, SignalHandler) == SIG_IGN) signal(SIGINT, SIG_IGN);
  if (signal(SIGTERM, SignalHandler) == SIG_IGN) signal(SIGTERM, SIG_IGN);
  if (signal(SIGALRM, SignalHandler) == SIG_IGN) signal(SIGALRM, SIG_IGN);
  alarm(ALARM_TIME);

  if ( (freq>100000000)) {
    sys = SYS_DVBT;
    if (open_fe(&fd_frontend,card)) {
      i=tune_it_s2(fd_frontend,sys,freq,srate,0,tone,specInv,diseqc,modulation,HP_CodeRate,TransmissionMode,guardInterval,bandWidth);
      if (i < 0) tune_it_s2(fd_frontend,sys,freq,srate,0,tone,specInv,diseqc,modulation,HP_CodeRate,TransmissionMode,guardInterval,bandWidth);
    }
  } else if ((freq!=0) && (pol!=0) && (srate!=0)) {
    if (open_fe(&fd_frontend,card)) {
      fprintf(stderr,"Tuning to %ld Hz\n",freq);
      i=tune_it_s2(fd_frontend,sys,freq,srate,pol,tone,specInv,diseqc,modulation,fec,TransmissionMode,guardInterval,bandWidth);
      if (i < 0) tune_it_s2(fd_frontend,sys,freq,srate,pol,tone,specInv,diseqc,modulation,fec,TransmissionMode,guardInterval,bandWidth);
    }
  }

  // Common PIDS - PAT, SDT, EIT, NIT
  npids=0;
  pids[npids++]=0x0;  // PAT
  pids[npids++]=0x10; // NIT
  pids[npids++]=0x11; // SDT
  pids[npids++]=0x12; // EIT
  pids[npids++]=0x14; // TDT

  if((fd_dvr = open(dvrdev[card],O_RDONLY|O_NONBLOCK)) < 0){
    perror("DVR DEVICE: ");
    return -1;
  }

#ifdef USE_8192
   if((fd = open(demuxdev[card],O_RDWR)) < 0){
     fprintf(stderr,"FD: ");
     perror("DEMUX DEVICE: ");
     return -1;
   }
   set_ts_filt(fd,8192);
#else
  for (i=0;i<npids;i++) {
    fprintf(stderr,"Opening demux device for PID %d\n",pids[i]);
    if((fd[i] = open(demuxdev[card],O_RDWR)) < 0){
      fprintf(stderr,"FD %i: ",i);
      perror("DEMUX DEVICE: ");
      return -1;
    }
    set_ts_filt(fd[i],pids[i]);
  }
#endif
  /* The DIBCOM 7000PC gives problems with continuity errors in the first few packets, so we
     read and discard 5 packets to clean things up. */
  bytes_read = 0;
  while (bytes_read < 5*188) {  
    n = read(fd_dvr,buf,5*188-bytes_read);
    if (n > 0) { bytes_read += n; }
  }

  parse_pat(fd_dvr);  
  parse_sdt(fd_dvr);  
  parse_nit(fd_dvr);
  parse_tdt(fd_dvr);
  time_t real_time;
  time(&real_time);
  fprintf(stderr, "System time: %s", ctime(&real_time));
  fprintf(stderr, "DVBTDT time: %s", ctime(&dvb_datetime));

  gettimeofday(&tv,(struct timezone*) NULL);
  real_start_time=tv.tv_sec;
  now=0;

  for (i=0;i<nservices;i++) {
    fprintf(stderr,"MAP %d, service %d, name %s: LCN: %d, %d PIDs - ",i,service_info[i].service_id,service_info[i].name,service_info[i].lcn,service_info[i].npids);
    for (j=0;j<service_info[i].npids;j++) { if (service_info[i].pids[j]!=-1) fprintf(stderr," %d",service_info[i].pids[j]); }
    fprintf(stderr,"\n");
    if (service_info[i].name[0] == 0) {
       fprintf(stderr,"No name found for service %d, aborting\n",service_info[i].service_id);
       return 1;
    }
    char str[4096];
    sprintf(str,"%s%03d-%s",OUTDIR,service_info[i].lcn,service_info[i].name);
    mkdir(str,0755);
  }

  fprintf(stderr,"Streaming %d stream%s\n",npids,(npids==1 ? "" : "s"));

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

      int bytes_read;
      bytes_read = read(fd_dvr, &pszLargeBuf[nBytesLeftOver], 50 * TS_SIZE);
      unsigned char *buf;
      int nOffset = 0;
      if(bytes_read >= TS_SIZE) {
        int nBytesToProcess = bytes_read;
        bytes_read = TS_SIZE;
        while (nBytesToProcess - nOffset >= TS_SIZE) {
          buf = &pszLargeBuf[nOffset];
              struct EXCHANGE_BUFFER *pszLocalBuffer;
              pthread_mutex_lock(&muDataToFile);
              nHighWaterMark = nGlobalHighWaterMark;
              nTEI = nGlobalTEI;
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
  }

  if (Interrupted) {
    fprintf(stderr,"Caught signal %d - closing cleanly.\n",Interrupted);
  }

#ifdef USE_8192
  close(fd);
#else
  for (i=0;i<npids;i++) close(fd[i]);
#endif
  close(fd_dvr);
  close(fd_frontend);

  return(0);
}
