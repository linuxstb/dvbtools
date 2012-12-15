/*
   options.c - handle program options/arguments for 'dvbdate'
               A program to set the clock from a DTT multiplex

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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <errno.h>

#include "dvbdate.h"

void
errmsg(char *message, ...)
{
  va_list ap;
  
  va_start(ap, message);
  fprintf(stderr, "%s: ", ProgName);
  vfprintf(stderr, message, ap);
  va_end(ap);
}
  
void
usage()
{
  fprintf(stderr, "usage: %s [-p] [-s] [-f] [-q] [-h]\n", ProgName);
  exit(1);
}

void
help()
{
  fprintf(stderr,
	  "\nhelp:\n"
	  "%s [-p] [-s] [-f] [-q] [-h]\n"
	  "  --print	(print current time, DTT time and delta)\n"
	  "  --set	(set the system clock to DTT time)\n"
	  "  --force	(force the setting of the clock)\n"
	  "  --quiet	(be silent)\n"
	  "  --help	(display this message)\n", ProgName);
  exit(1);
}

int
do_options(int arg_count, char** arg_strings)
{
  static struct option Long_Options[]=
    {
      {"print", 0, 0, 'p'},
      {"set", 0, 0, 's'},
      {"force", 0, 0, 'f'},
      {"quiet", 0, 0, 'q'},
      {"help", 0, 0, 'h'},
      {0, 0, 0, 0}
    };
  int c;
  int Option_Index = 0;
  
  while (1)
    {
      c = getopt_long(arg_count, arg_strings, "psfqh", Long_Options, &Option_Index);
      if (c == EOF)
	break;
      switch (c)
	{
      case 'p':
	do_print = 1;
	break;
      case 's':
	do_set = 1;
	break;
      case 'f':
	do_force = 1;
	break;
      case 'q':
	do_quiet = 1;
	break;
      case 'h':
	help();
	break;
      case '?':
	usage();
	break;
      case 0:
/*
 * Which long option has been selected?  We only need this extra switch
 * to cope with the case of wanting to assign two long options the same
 * short character code.
 */
	printf("long option index %d\n", Option_Index);
	switch (Option_Index)
	  {
	case 0: /* Print */
	case 1: /* Set */
	case 2: /* Force */
	case 3: /* Quiet */
	case 4: /* Help */
	  break;
	default:
	  fprintf(stderr, "%s: unknown long option %d\n", ProgName, Option_Index);
	  usage();
	  }
	break;
/*
 * End of Special Long-opt handling code
 */
     default:
	  fprintf(stderr, "%s: unknown getopt error - returned code %02x\n", ProgName, c);
	  exit(1);
	}
    }
}
 
