#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "si.h"

char xmlify_result[10];
char* xmlify (char c) {
  switch(c) {
    case '&': strcpy(xmlify_result,"&amp;");
              break;
    case '<': strcpy(xmlify_result,"&lt;");
              break;
    case '>': strcpy(xmlify_result,"&gt;");
              break;
    case '\"': strcpy(xmlify_result,"&quot;");
              break;
    case 0: xmlify_result[0]=0;
              break;
    default: xmlify_result[0]=c;
             xmlify_result[1]=0;
             break;
 }
 return(xmlify_result);
}

/* Get the first unscanned transponder (or return NULL) */
transponder_t*  get_unscanned(transponder_t* transponders) {
  transponder_t* t;
  
  t=transponders;

  while (t!=NULL) {
    if (t->scanned==0) { return(t); };
    t=t->next;
  }
  return NULL;
}

void add_transponder(transponder_t* transponder,transponder_t** transponders) {
  transponder_t* t;
  int found;

  if (*transponders==NULL) {
    *transponders=(transponder_t*)malloc(sizeof(transponder_t));

    (*transponders)->freq=transponder->freq;
    (*transponders)->srate=transponder->srate;
    (*transponders)->pol=transponder->pol;
    (*transponders)->pos=transponder->pos;
    (*transponders)->we_flag=transponder->we_flag;
    (*transponders)->mod=transponder->mod;
    (*transponders)->scanned=0;
    (*transponders)->next=NULL;
  } else {
    t=(*transponders);
    found=0;
    while ((!found) && (t!=NULL)) {
       /* Some transponders appear with slightly different frequencies -
          ignore a new transponder if it is within 3MHz of another */
       if ((abs(t->freq-transponder->freq)<=3000) && (t->pol==transponder->pol)) {
          found=1;
       } else {
         t=t->next;
       }
    }

    if (!found) {
      t=(transponder_t*)malloc(sizeof(transponder_t));

      t->freq=transponder->freq;
      t->srate=transponder->srate;
      t->pol=transponder->pol;
      t->pos=transponder->pos;
      t->we_flag=transponder->we_flag;
      t->mod=transponder->mod;
      t->scanned=0;
      t->next=(*transponders);

      (*transponders)=t;
    }
  }
}


void parse_descriptors(int info_len,unsigned char *buf, transponder_t* transponder, transponder_t** transponders) {
  int i=0;
  int descriptor_tag,descriptor_length,j,k,pid,id;
  int service_type;
  char tmp[128];
  unsigned int freq, pol, sr;

       while (i < info_len) {
        descriptor_tag=buf[i++];
        descriptor_length=buf[i++];
	//        printf("Found descriptor: 0x%02x - length %02d\n",descriptor_tag,descriptor_length);
        while (descriptor_length > 0) {
          switch(descriptor_tag) {
           case 0x03: // audio_stream_descriptor
             printf("<audio_info tag=\"0x03\" info=\"%02x\" />\n",buf[i]);
             i+=descriptor_length;
             descriptor_length=0;
             break;

           case 0x06: // data_stream_alignmentdescriptor
             printf("<data_stream_alignment tag=\"0x06\" data=\"%02x\" />\n",buf[i]);
             i+=descriptor_length;
             descriptor_length=0;
             break;

           case 0x0a: // iso_639_language_descriptor
             for (j=0;j<((descriptor_length)/4);j++) {
               printf("<iso_639 language=\"");
               if (buf[i]!=0) printf("%c",buf[i]);
               if (buf[i+1]!=0) printf("%c",buf[i+1]);
               if (buf[i+2]!=0) printf("%c",buf[i+2]);
               printf("\" type=\"%d\" />\n",buf[i+3]);
               i+=4;
               descriptor_length-=4;
             }
             break;

           case 0x0b: // system_clock_descriptor
             printf("<system_clock tag=\"0x0b\" data=\"%02x%02x\" />\n",buf[i],buf[i+1]);
             i+=descriptor_length;
             descriptor_length=0;
             break;

           case 0x09: // ca_descriptor
             k=((buf[i]<<8)|buf[i+1]);
             switch(k&0xff00) {
               case SECA_CA_SYSTEM:
                 for (j=2; j<descriptor_length; j+=15)
                 {
                   pid = ((buf[i+j] & 0x1f) << 8) | buf[i+j+1];
                   id = (buf[i+j+2] << 8) | buf[i+j+3];
                   printf("<ca_system_descriptor type=\"seca\" system_id=\"0x%04x\" ecm_pid=\"%d\" ecm_id=\"%06x\" />\n",k,pid,id);
                 }        
                 break;
               case VIACCESS_CA_SYSTEM:
                 j = 4;
                 while (j < descriptor_length)
                 {
                   if (buf[i+j]==0x14)
                   {
                     pid = ((buf[i+2] & 0x1f) << 8) | buf[i+3];
                     id = (buf[i+j+2] << 16) | (buf[i+j+3] << 8) | (buf[i+j+4] & 0xf0);
                     printf("<ca_system_descriptor type=\"viaccess\" system_id=\"0x%04x\" ecm_pid=\"%d\" ecm_id=\"%06x\" />\n",k,pid,id);
                   }
                   j += 2+buf[i+j+1];
                 }
                 break;
               case IRDETO_CA_SYSTEM:
               case BETA_CA_SYSTEM:
                 pid = ((buf[i+2] & 0x1f) << 8) | buf[i+3];
                 printf("<ca_system_descriptor type=\"irdeto\" system_id=\"0x%04x\" ecm_pid=\"%d\" />\n",k,pid);
                 break;
               case NAGRA_CA_SYSTEM:
                 pid = ((buf[i+2] & 0x1f) << 8) | buf[i+3];
                 printf("<ca_system_descriptor type=\"nagra\" system_id=\"0x%04x\" ecm_pid=\"%d\" />\n",k,pid);
                 break;
               case CONAX_CA_SYSTEM:
                 pid = ((buf[i+2] & 0x1f) << 8) | buf[i+3];
                 printf("<ca_system_descriptor type=\"conax\" system_id=\"0x%04x\" ecm_pid=\"%d\" />\n",k,pid);
                 break;
               case VIDEOGUARD_CA_SYSTEM:
                 pid = ((buf[i+2] & 0x1f) << 8) | buf[i+3];
                 printf("<ca_system_descriptor type=\"videoguard\" system_id=\"0x%04x\" ecm_pid=\"%d\" />\n",k,pid);
                 break;
               default:
                 pid = ((buf[i+2] & 0x1f) << 8) | buf[i+3];
                 printf("<ca_system_descriptor type=\"unknown\" system_id=\"0x%04x\" />\n",k);
                 break;
               }
               i+=descriptor_length;
               descriptor_length=0;
               break;

           case 0x40: // network_name
//             printf("<network_name tag=\"0x40\">");
             j=descriptor_length;
             while(j > 0) {
//               printf("%c",buf[i++]);
               j--;
             }
             descriptor_length=0;
//             printf("</network_name>\n");
             break;
             
           case 0x41: // service_list
//             printf("<services tag=\"0x41\" n=\"%d\">\n",descriptor_length/3);
             while (descriptor_length > 0) {
//               printf("<service id=\"%d\" type=\"%d\" />\n",(buf[i]<<8)|buf[i+1],buf[i+2]);
               i+=3;
               descriptor_length-=3;
             }
//             printf("</services>\n");
             break;

           case 0x43: // satellite_delivery_system
             freq=(unsigned int)(buf[i]<<24)|(buf[i+1]<<16)|(buf[i+2]<<8)|buf[i+3];
             sprintf(tmp,"%x",freq);
             transponder->freq=atoi(tmp)*10;
             i+=4;
             transponder->pos=(buf[i]<<8)|buf[i+1];
             i+=2;
             transponder->we_flag=(buf[i]&0x80)>>7;
             pol=(buf[i]&0x60)>>5;
             switch(pol) {
                 case 0 : transponder->pol='H'; break;
                 case 1 : transponder->pol='V'; break;
                 case 2 : transponder->pol='L'; break;
                 case 3 : transponder->pol='R'; break;
             }
             transponder->mod=buf[i]&0x1f;
             i++;
             sr=(unsigned int)(buf[i]<<24)|(buf[i+1]<<16)|(buf[i+2]<<8)|(buf[i+3]&0xf0);
             sr=(unsigned int)(sr >> 4);
             sprintf(tmp,"%x",sr);
             transponder->srate=atoi(tmp)*100;
             i+=4;
             descriptor_length=0;
             add_transponder(transponder,transponders);
//             printf("<satellite_delivery tag=\"0x43\" freq=\"%05d\" srate=\"%d\" pos=\"%04x\" we_flag=\"%d\" polarity=\"%c\" modulation=\"%d\" />\n",transponder->freq,transponder->srate,transponder->pos,transponder->we_flag,transponder->pol,transponder->mod);
	     break;

           case 0x48: // service_description
             service_type=buf[i++];
             printf("<description tag=\"0x48\" type=\"%d\"",service_type);
             descriptor_length--;
             j=buf[i++];
             descriptor_length-=(j+1);
             printf(" provider_name=\"");;
             while(j > 0) {
               printf("%s",xmlify(buf[i++]));
               j--;
             }
             printf("\" service_name=\"");
             j=buf[i++]; 
             descriptor_length-=(j+1);
             while(j > 0) {
               printf("%s",xmlify(buf[i]));
               i++;
               j--;
             }
             printf("\" />\n");
             break;

           case 0x49: // country_availability:
             printf("<country_availability tag=\"0x49\" type=\"%d\" countries=\" ",(buf[i]&0x80)>>7);
             i++;
             j=descriptor_length-1;
             while (j > 0) { 
               printf("%c",buf[i++]);
               j--;
             }
             printf("\" />\n");
             descriptor_length=0;
             break;

          case 0x4c:
             printf("<time_shifted_copy_of tag=\"0x4c\" service_id=\"%d\" />\n",(buf[i]<<8)|buf[i+1]);
             i+=descriptor_length;
             descriptor_length=0;
             break;

          case 0x52: // stream_identifier_descriptor
             printf("<stream_id id=\"%d\" />\n",buf[i]);
             i+=descriptor_length;
             descriptor_length=0;
             break;
	  
          case 0x53:
             printf("<ca_identifier tag=\"0x53\" length=\"%02x\">\n",descriptor_length);
             for (j=0;j<descriptor_length;j+=2) {
               k=(buf[i+j]<<8)|buf[i+j+1];
               printf("<ca_system_id>%04x</ca_system_id>\n",k);
             }
             i+=descriptor_length;
             descriptor_length=0;
             printf("</ca_identifier>\n");
             break;

          case 0x56:
             j=0;
             printf("<teletext tag=\"0x56\">\n");
             while (j < descriptor_length) {
               printf("<teletext_info lang=\"");
               printf("%s",xmlify(buf[i]));
               printf("%s",xmlify(buf[i+1]));
               printf("%s",xmlify(buf[i+2]));
               k=(buf[i+3]&0x07);
               printf("\" type=\"%d\" page=\"%d%02x\" />\n",(buf[i+3]&0xf8)>>3,(k==0 ? 8 : k),buf[i+4]);
               i+=5;
               j+=5;
             }
             printf("</teletext>\n");
             descriptor_length=0;
             break;

          case 0x59:
             j=0;
             printf("<subtitling_descriptor tag=\"0x59\">\n");
             while (j < descriptor_length) {
               printf("<subtitle_stream lang=\"");
               printf("%s",xmlify(buf[i]));
               printf("%s",xmlify(buf[i+1]));
               printf("%s",xmlify(buf[i+2]));
               printf("\" type=\"%d\" composition_page_id=\"%04x\" ancillary_page_id=\"%04x\" />\n",buf[i+3],(buf[i+4]<<8)|buf[i+5],(buf[i+6]<<8)|buf[i+7]);
               i+=8;
               j+=8;
             }
             printf("</subtitling_descriptor>\n");
             descriptor_length=0;
             break;

          case 0x6a:
             printf("<ac3_descriptor tag=\"0x6a\" data=\"");
             for (j=0;j<descriptor_length;j++) printf("%02x",buf[i+j]);
             printf("\" />\n");
             i+=descriptor_length;
             descriptor_length=0;
             break;

          case 0x73:
             printf("<default_authority_descriptor tag=\"0x%02x\" name=\"",descriptor_tag);
             for (j=0;j<descriptor_length;j++) printf("%c",(isalnum(buf[i+j]) ? buf[i+j] : '.'));
             printf("\" />\n");
             i+=descriptor_length;
             descriptor_length=0;
             break;

          case 0xc5: // canal_satellite_radio_descriptor
	    /* This is guessed from the data */
            printf("<canal_radio tag=\"0x%02x\" id=\"%d\" name=\"",descriptor_tag,buf[i]);
            for (j=1;j<descriptor_length;j++) 
              if (buf[i+j]!=0) printf("%c",buf[i+j]);
            printf("\" />\n");
            i+=descriptor_length;
            descriptor_length=0;
            break;

          default:
             printf("<descriptor tag=\"0x%02x\" data=\"",descriptor_tag);
             for (j=0;j<descriptor_length;j++) printf("%02x",buf[i+j]);
             printf("\" text=\"");
             for (j=0;j<descriptor_length;j++) printf("%c",(isalnum(buf[i+j]) ? buf[i+j] : '.'));
             printf("\" />\n");
             i+=descriptor_length;
             descriptor_length=0;
             break;
          }
        }
      }
}
