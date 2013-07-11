/* A simple filter (stdin -> stdout) to extract multiple streams from a
   multiplexed TS.  Specify the PID on the command-line 

   Updated 29th January 2003 - Added some error checking and reporting.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

int main(int argc, char **argv)
{
  int pid,n;
  int filters[8192];
  unsigned int i=0;
  uint64_t j=0;
  unsigned char buf[188];
  unsigned char my_cc[8192];
  int errors=0;
  FILE* fd=stdout;
  int output_count=0;
  int split=0;
  int info=0;
  int test=0;
  int discontinuity_indicator;
  int adaption_field_control;
  char filename[30];

  for (i=0;i<8192;i++) { filters[i]=0; my_cc[i]=0xff;}

  n=0;
  for (i=1;i<argc;i++) {
    if (!strcmp(argv[i],"-split")) {
      split=1;
    } else if (!strcmp(argv[i],"-info")) {
      info=1;
    } else if (!strcmp(argv[i],"-test")) {
      test=1;
    } else {
      pid=atoi(argv[i]);
      fprintf(stderr,"Filtering pid %d\n",pid);
      if (pid < 8192) {
        filters[pid]=1;
        n++;
      }
    }
  }

  if (info) { test = 1; }
  if (test) { split = 0; }

  if (n==0) {
    fprintf(stderr,"Filtering all PIDs\n");
    for (i=0;i<8192;i++) { filters[i]=1; }
  }

  if (split) {
    sprintf(filename,"part%04d.ts",++output_count);
    fd=fopen(filename,"w+");
    if (fd==NULL) {
      fprintf(stderr,"Could not create output file \"%s\", aborting\n",filename);
    }
    fprintf(stderr,"INFO: Writing output to file %s\n",filename);
  }
  n=fread(buf,1,188,stdin);
  i=1;
  while (n==188) {
    if (buf[0]!=0x47) {
      // TO DO: Re-sync.
      fprintf(stderr,"FATAL ERROR IN STREAM AT PACKET %d\n",i);
      exit(1);
    }
    pid=(((buf[1] & 0x1f) << 8) | buf[2]);
    discontinuity_indicator=(buf[5]&0x80)>>7;
    adaption_field_control=(buf[3]&0x30)>>4;
    if (my_cc[pid]==0xff) {
      my_cc[pid]=buf[3]&0x0f;
    } else {
      if ((adaption_field_control!=0) && (adaption_field_control!=2)) {
        my_cc[pid]++; my_cc[pid]%=16;
      }
    }

    if (filters[pid]==1) {
      if (info) {
        printf("PID %d, psi = %d\n",pid, (buf[1]&0x40)>>6);
      }
      if ((discontinuity_indicator==0) && (my_cc[pid]!=(buf[3]&0x0f))) {
        fprintf(stderr,"PID %d - packet incontinuity (%lld bytes read)- expected %02x, found %02x\n",pid,j*188,my_cc[pid],buf[3]&0x0f);
        my_cc[pid]=buf[3]&0x0f;
        errors++;
        if (split) {
          fclose(fd);
          fprintf(stderr,"INFO: Closed file %s\n",filename);
          sprintf(filename,"part%04d.ts",++output_count);
          fd=fopen(filename,"w+");
          if (fd==NULL) {
            fprintf(stderr,"Could not create output file \"%s\", aborting\n",filename);
          }
          fprintf(stderr,"INFO: Writing output to file %s\n",filename);
        }
      }
      if (!test) {
        n=fwrite(buf,1,188,fd);
        if (n==188) {
          j++;
        } else {
          fprintf(stderr,"FATAL ERROR - CAN NOT WRITE PACKET %d\n",i);
          exit(1);
        }
      }
    }
    n=fread(buf,1,188,stdin);
    i++;
  }
  fprintf(stderr,"Read %d packets, wrote %lld.\n",i,j);
  fprintf(stderr,"%d incontinuity errors.\n",errors);
  return(0);
}
