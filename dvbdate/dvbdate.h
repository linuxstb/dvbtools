/*
   dvbdate.h - variables and declarations used in 'dvbdate'
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

#ifdef DVBDATE
#define DVBDATE_DECL
#else
#define DVBDATE_DECL extern
#endif

DVBDATE_DECL char *ProgName;
DVBDATE_DECL int do_print;
DVBDATE_DECL int do_set;
DVBDATE_DECL int do_force;
DVBDATE_DECL int do_quiet;


#define bcdtoint(i) ((((i & 0xf0) >> 4) * 10) + (i & 0x0f))

/* How many seconds can the system clock be out before we get warned? */
#define ALLOWABLE_DELTA 30*60

