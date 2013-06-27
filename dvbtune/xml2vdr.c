/*  xml2vdr - a conversion utility to convert XML DVB-SI files into a 
    channel list for VDR.

    It uses the SAX functions of libxml (version 2) - see www.xmlsoft.org

    (C) Dave Chapman, May 2001

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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <libxml/parser.h>
#include <libxml/parserInternals.h>

/* This structure isn't used - it could be used to check the validity of the
   file. */
typedef enum {
  PARSER_START,
  PARSER_IN_SATELLITE,
  PARSER_IN_TRANSPONDER,
  PARSER_IN_SERVICE,
  PARSER_IN_STREAM,
  PARSER_END
} ParserState;

char service_name[256];
char provider_name[256];
int freq;
char pol;
char diseqc[]="S28.2E";
int srate;
int vpid;
int apid[256];
char ca_systems[256][256];
int n_ca;
int is_ac3[256];
int n_apids=0;
char lang[256][4];
char tmp[32];
int tpid;
int ca;
int pnr;
int type;
int ignore_service=0;
int canal_radio_id;
int in_audio_stream=0;
int fta=0;

/* A hack to clean text strings - it breaks on non-UK character sets */
void my_strcpy(unsigned char* dest, unsigned char* source) {
  int i=0;
  int j=0;

  while (source[j]!=0) {
    if (source[j]==':') {
      dest[i++]='|';
    } else if ((source[j]>31) && (source[j]<128)) {
      dest[i++]=source[j];
    }
    j++;
  }
  dest[i]=0;

}
static xmlEntityPtr xmlsat_GetEntity(void *user_data, const char *name) {
  return xmlGetPredefinedEntity(name);
}

typedef struct _xmlsatParseState {
  ParserState state;
} xmlsatParseState;

static void xmlsat_StartDocument(xmlsatParseState *state) {
  state->state = PARSER_START;
}

static void xmlsat_EndDocument(xmlsatParseState *state) {
  state->state = PARSER_END;
}

static void xmlsat_StartElement(xmlsatParseState *state, const char *name,
                               const char **attrs) {
  int i;
  static int streamtype;
  static int pid;

    if (strcmp(name,"satellite")==0) {
       state->state=PARSER_IN_SATELLITE;
    } else if (strcmp(name,"transponder")==0) {
        state->state=PARSER_IN_TRANSPONDER;
        freq=0;
        pol='V';
        srate=27500;
        if (attrs!=NULL) {
           for (i=0;attrs[i]!=NULL;i+=2) {
              if (strcmp(attrs[i],"freq")==0) {
                freq=atoi(attrs[i+1])/1000;
              } else if (strcmp(attrs[i],"srate")==0) {
                srate=atoi(attrs[i+1])/1000;
              } else if (strcmp(attrs[i],"polarity")==0) {
                pol=attrs[i+1][0];
              }
           }
        }
    } else if (strcmp(name,"service")==0) {
        if (attrs!=NULL) {
           pnr=0;
           ca=0;
           for (i=0;attrs[i]!=NULL;i+=2) {
              if (strcmp(attrs[i],"id")==0) {
                pnr=atoi(attrs[i+1]);
              }
           }
        }
    } else if (strcmp(name,"stream")==0) {
        if (attrs!=NULL) {
           streamtype=0;
           pid=0;
           for (i=0;attrs[i]!=NULL;i+=2) {
              if (strcmp(attrs[i],"type")==0) {
                streamtype=atoi(attrs[i+1]);
              } else if (strcmp(attrs[i],"pid")==0) {
                pid=atoi(attrs[i+1]);
              }
           }
           if ((streamtype==1) || (streamtype==2)) {
             vpid=pid;
           } else if ((streamtype==3) || (streamtype==4)) {
             in_audio_stream=1;
             apid[n_apids]=pid;
             is_ac3[n_apids]=0;
             lang[n_apids][0]=0;
             n_apids++;
           } else if (streamtype==6) {
//             tpid=pid;
           }
        }
    } else if (strcmp(name,"ac3_descriptor")==0) {
      if (streamtype==6) {
        is_ac3[n_apids]=1;
        apid[n_apids]=pid;
        lang[n_apids][0]=0;
        n_apids++;
        in_audio_stream=1;
      }
    } else if (strcmp(name,"ca_system_descriptor")==0) {
      ca=1;
      for (i=0;attrs[i]!=NULL;i+=2) {
         if (strcmp(attrs[i],"system_id")==0) {
           if (strlen(attrs[i+1])>0) {
             strcpy(ca_systems[n_ca++],attrs[i+1]);
           }
         }
      }
    } else if (strcmp(name,"iso_639")==0) {
      if (in_audio_stream) {
        if (attrs!=NULL) {
           canal_radio_id=0;
           for (i=0;attrs[i]!=NULL;i+=2) {
              if (strcmp(attrs[i],"language")==0) {
                if (strlen(attrs[i+1])>0) {
                  strcpy(lang[n_apids-1],attrs[i+1]);
                }
              }
           }
        }
      }
    } else if (strcmp(name,"canal_radio")==0) {
        ignore_service=1;
        if (attrs!=NULL) {
           canal_radio_id=0;
           for (i=0;attrs[i]!=NULL;i+=2) {
              if (strcmp(attrs[i],"id")==0) {
                canal_radio_id=atoi(attrs[i+1]);
              } else if (strcmp(attrs[i],"name")==0) {
                my_strcpy((unsigned char*)service_name,(unsigned char*)attrs[i+1]);
              }
           }
        }
    } else if (strcmp(name,"description")==0) {
        if (attrs!=NULL) {
           type=0;
           service_name[0]=0;
           provider_name[0]=0;
           for (i=0;attrs[i]!=NULL;i+=2) {
              if (strcmp(attrs[i],"service_name")==0) {
                my_strcpy((unsigned char*)service_name,(unsigned char*)attrs[i+1]);
              } else if (strcmp(attrs[i],"provider_name")==0) {
                my_strcpy((unsigned char*)provider_name,(unsigned char*)attrs[i+1]);
              } else if (strcmp(attrs[i],"type")==0) {
                type=atoi(attrs[i+1]);
              }
           }
       }
   }
}

static void xmlsat_EndElement(xmlsatParseState *state, const char *name) {

  int i,x;

  if (strcmp(name,"stream")==0) {
    in_audio_stream=0;
  } else if (strcmp(name,"canal_radio")==0) {
//     if (ca==0) printf("%s:RADIO:%s:%d:%c:%d:%d:%d:%d:%d:%d:%d\n",provider_name,service_name,freq,pol,diseqc,srate,vpid,apid[0],tpid,ca,pnr);
    if ((ca==0) || (fta==0)) printf("%s (RADIO):%d:%c:%s:%d:%d:%d:%d:%d:%d\n",service_name,freq,pol,diseqc,srate,vpid,apid[0],tpid,ca,pnr);
     n_apids=0;
     n_ca=0;
  } else if (strcmp(name,"service")==0) {
     if (service_name[0]==0) strcpy(service_name,"no name");

     if ((ignore_service==0) && ((type==1) || (type==2) || (type==155) || (type==130))) {  // TV or Radio or DishNetwork TV or BBC Interactive
       /* Only print service if at least 1 PID is non-zero */
       if (((ca==0) || (fta==0)) && (((vpid!=0) || (n_apids>0) || (tpid!=0)))) {
//         printf("%s:%s:%s:%d:%c:%s:%d:%d:",provider_name,((vpid==0) ? "RADIO" : "TV"),service_name,freq,pol,diseqc,srate,vpid);
         printf("%s (%s):%d:%c:%s:%d:%d:",service_name,((vpid==0) ? "RADIO" : "TV"),freq,pol,diseqc,srate,vpid);
         x=0;
         for (i=0;i<n_apids;i++) {
           if (!is_ac3[i]) { 
	     if (x) printf(",");
             x=1;
             printf("%d",apid[i]);
           }
         }
         x=0;
         for (i=0;i<n_apids;i++) x+=is_ac3[i];
         if (x) {
           printf(";");
           for (i=0;i<n_apids;i++) {
             x=0;
             if (is_ac3[i]) { 
               if (x) printf(",");
               x=1;
               printf("%d",apid[i]);
             }
           }
         }
         printf(":%d",tpid);
         if (ca==0) {
           printf(":0");
         } else {
           printf(":1");
//           for (i=0;i<n_ca;i++) {
//             if (i>0) printf(",");
//             printf("%s",ca_systems[i]);
//           }
         }
         printf(":%d\n",pnr);
       }
     }
     service_name[0]=0;
     n_apids=0;
     n_ca=0;
     vpid=0;
     tpid=0;
     pnr=0;
     ca=0;
     type=0;
     ignore_service=0;
  } else if (strcmp(name,"transponder")==0) {
    freq=0;
    pol=0;
    srate=0;
  }
}

static void xmlsat_Characters(xmlsatParseState *state, const char *chars,int len) {
// This is copied from a libxml example - it's not used in this application.
// xmlsat files don't have any text elements - all data is stored as 
// attributes.
  /*
  switch (state->state) {
  case PARSER_IN_NAME:
    for (i = 0; i < len; i++)
      g_string_append_c(state->nameData, chars[i]);
    break;
  case PARSER_IN_SUMMARY:
    for (i = 0; i < len; i++)
      g_string_append_c(state->summaryData, chars[i]);
    break;
  default:
    break;
  }
  */
}

static xmlSAXHandler xmlsatSAXParser = {
   0, /* internalSubset */
   0, /* isStandalone */
   0, /* hasInternalSubset */
   0, /* hasExternalSubset */
   0, /* resolveEntity */
   (getEntitySAXFunc)xmlsat_GetEntity, /* getEntity */
   0, /* entityDecl */
   0, /* notationDecl */
   0, /* attributeDecl */
   0, /* elementDecl */
   0, /* unparsedEntityDecl */
   0, /* setDocumentLocator */
   (startDocumentSAXFunc)xmlsat_StartDocument, /* startDocument */
   (endDocumentSAXFunc)xmlsat_EndDocument, /* endDocument */
   (startElementSAXFunc)xmlsat_StartElement, /* startElement */
   (endElementSAXFunc)xmlsat_EndElement, /* endElement */
   0, /* reference */
   (charactersSAXFunc)xmlsat_Characters, /* characters */
   0, /* ignorableWhitespace */
   0, /* processingInstruction */
   0, /* comment */
   0, /* warning */
   0, /* error */
   0 /* fatalError */
};

int main(int argc, char **argv) {
  xmlParserCtxtPtr ctxt;
  xmlsatParseState state;

  if (argc<2) {
    printf("Usage: %s [-fta] filename.xmlsat\n",argv[0]);
  } else {
    if (argc==2) {
      ctxt = (xmlParserCtxtPtr)xmlCreateFileParserCtxt(argv[1]);
    } else if (argc==3) {
      if (strcmp(argv[1],"-fta")==0) {
        fta=1;
      }
      ctxt = (xmlParserCtxtPtr)xmlCreateFileParserCtxt(argv[2]);
    }
    if (ctxt == NULL) {
      fprintf(stderr,"ERROR: can not open file\n");
    }

    ctxt->sax = &xmlsatSAXParser;
    ctxt->userData = &state;

    xmlParseDocument(ctxt);

    ctxt->sax = NULL;
    xmlFreeParserCtxt(ctxt);
  }
  return 0;
}

