/********************************************************************\
 * util.h -- utility functions that are used everywhere for         *
 *           gnucash (ex-xacc (X-Accountant))                       *
 * Copyright (C) 1997 Robin D. Clark                                *
 * Copyright (C) 1998, 1999 Linas Vepstas                           *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, write to the Free Software      *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.        *
 *                                                                  *
 *   Author: Rob Clark                                              *
 * Internet: rclark@cs.hmc.edu                                      *
 *  Address: 609 8th Street                                         *
 *           Huntington Beach, CA 92648-4632                        *
\********************************************************************/

#ifndef __XACC_UTIL_H__
#define __XACC_UTIL_H__

#include <stdlib.h>
#include "config.h"
#include "gnc-common.h"

#define BUFSIZE   1024

/** DEBUGGING MACROS ************************************************/
/* The debuging macros enable the setting of trace messages */
#include <stdio.h>

#include <assert.h>

#define LG(condition,args...)	if (condition) fprintf(stderr, ##args)

#define MOD_ENGINE     1
#define MOD_IO         2
#define MOD_REGISTER   3
#define MOD_LEDGER     4
#define MOD_HTML       5
#define MOD_GUI        6
#define MOD_SCRUB      7
#define MOD_GTK_REG    8
#define MOD_GUILE      9
#define MODULE_MAX    10

extern int loglevel[MODULE_MAX];

#define LERR    (1 <= loglevel[module])
#define LWARN   (2 <= loglevel[module])
#define LINFO   (3 <= loglevel[module])
#define LDEBUG  (4 <= loglevel[module])
#define LDETAIL (5 <= loglevel[module])

#ifdef KDE
#undef DEBUG
#endif

/* utility macros  */
#define FATAL(x...)    LG(1, "Fatal Error: ");   LG(1,       ##x);
#define PERR(x...)     LG(LERR,    "Error: ");   LG(LERR,    ##x);
#define PWARN(x...)    LG(LWARN,   "Waring: ");  LG(LWARN,   ##x);
#define PINFO(x...)    LG(LINFO,   "Info: ");    LG(LINFO,   ##x);
#define DEBUG(x...)    LG(LDEBUG,  "Debug: ");   LG(LDEBUG,  ##x);
#define ENTER(x...)    LG(LDEBUG,  "Enter: ");   LG(LDEBUG,  ##x);
#define LEAVE(x...)    LG(LDEBUG,  "Leave: ");   LG(LDEBUG,  ##x);
#define DETAIL(x...)   LG(LDETAIL, "Detail: ");  LG(LDETAIL, ##x);

#define DEBUGCMD(x) { if (LINFO) { x; }}

#include <errno.h>

#define ERROR()     fprintf(stderr,"%s: Line %d, error = %s\n", \
			    __FILE__, __LINE__, strerror(errno));

#if DEBUG_MEMORY
void   *dmalloc( size_t size );
void   dfree( void *ptr );
size_t dcoresize();
#  define _malloc(x)   dmalloc(x)
#  define _free(x)     dfree(x)
#  define _coresize()  dcoresize()
#else
#  define _malloc(x)    malloc(x)
#  define _free(x)      free(x)
#  define _coresize()   0
#endif

/** COOL MACROS *****************************************************/
#ifndef ABS
#define ABS(x)   ((x)>=0) ? (x) : (-1*(x))
#endif
#define DABS(x)  ((x)>=0.0) ? (x) : (-1.0*(x))
#define DMAX(x,y) ((x)>(y)) ? (x) : (y)
#define isNum(x) (((x)-0x30) < 0) ? 0 : (((x)-0x30) > 9) ? 0 : 1

#define EPS  (1.0e-6)
#define DEQ(x,y) (((((x)+EPS)>(y)) ? 1 : 0) && ((((x)-EPS)<(y)) ? 1 : 0))


#define SAFE_STRCMP(da,db) {		\
  if ((da) && (db)) {			\
    int retval = strcmp ((da), (db));	\
    /* if strings differ, return */	\
    if (retval) return retval;		\
  } else 				\
  if ((!(da)) && (db)) {		\
    return -1;				\
  } else 				\
  if ((da) && (!(db))) {		\
    return +1;				\
  }					\
}

int safe_strcmp (const char * da, const char * db);

/********************************************************/
/* the ultostr() subroutihne is the inverse of strtoul().
 *    It accepts a number and prints it in the indicated base.
 *    The returned string should be freed when done.
 */

char * ultostr (unsigned long val, int base);


/* The gnc_localeconv() subroutine returns an lconv structure
 * containing locale information. If no locale is set, the
 * structure is given default (en_US) values.
 */
struct lconv * gnc_localeconv();


/** PROTOTYPES ******************************************************/

/*
 * The xaccPrintAmount() and xaccSPrintAmount() routines provide
 *    i18n'ed convenience routines for printing share and currency
 *    amounts.  Both routines take a double argument, and print
 *    into a string.  The argument 'shrs' is a bitflag that controls
 *    the format.  If it is zero, two decimal places and no currency
 *    symbol will be printed.  If non-zero, it must be a bitwise-or
 *    of the following:
 *
 *    PRTSYM -- also print currency symbol.
 *    PRTSHR -- print four decimal places
 *    PRTSYM | PRTSHR --  prints three decimal places followed by string "shrs" 
 *    PRTSEP -- print comma-separated K's
 *
 * The xaccPrintAmount() routine returns a pointer to a statically
 *    allocated buffer, and is therefore not thread-safe.
 *
 * The xaccSPrintAmount() routine accepts a pointer to the buffer to be
 *    printed to.  It returns the length of the printed string.
 */

#define PRTSYM 0x1
#define PRTSHR 0x2
#define PRTSEP 0x4

char * xaccPrintAmount (double val, short shrs);
int xaccSPrintAmount (char *buf, double val, short shrs);

/* Parse i18n amount strings */
double xaccParseAmount (const char * instr, gncBoolean monetary);


/********************************************************************\
 * xaccParseQIFAmount                                               *
 *   parses monetary strings in QIF files                           *
 *   Note that these strings may be in the 'US' format of DDD,DDD,DDD.CC
 *   or they may be in european format: DDD.DDD.DDD,CC
 *   The routine tries to 'guess' which of these it is.
 *   This sounds really dopey, but Intuit/Quicken managed to 'internationalize'
 *   thier export format, causeing no end of pain.
 *
 * XXX hack alert: the right way to do this is to do the following:
 *  -- have a global flag that indicates 'euro' or 'us style'
 *  -- initial value of global flag depends on locale
 *  -- if during parsing, a euro-format currency is found, then flag is set to euro.
 *  -- if during parsing, a use-format amount is found, then flag set to us.
 *  -- if both styles found during one run, then flag an error.
 *                                                                  *
\********************************************************************/
double xaccParseQIFAmount (const char * str);


/** TEMPLATES ******************************************************/
/* 
 * There are several ideas going on in here.
 *  -- if an account is already being edited, and user clicks on "open", it
 *     should raise that dialog to the top or de-iconize it instead of 
 *     creating a new window. 
 *
 * -- association between windows and accounts is many-to-one & one-to-many.
 *    e.g.  if a "general ledger" dialog is open, then it might be displaying 
 *    four accounts all at once. Thus, an account may be visible in its 
 *    "main" dialog (of which there is only one), and possibly many "general 
 *    ledger" windows.
 *
 * -- I don't remember, but I think I might also use these to manage redraws
 *    when some entry is updated, and it is visible in multiple windows. 
 *    (again, visible in the account main window, and possibly some general 
 *    ledger windows).
 *
 * -- If user deletes an account, then any open windows associated with this
 *    account are auto-closed (and other windows possibly updated).
 *
 * -- the macros associate an xaccAccount struct with a gui-specific struct
 *    which contains things like widgets and other pieces the GUI needs 
 *    to "remember" about that dialog.
 */


#define FIND_IN_LIST(Type,list,id,member,found) 	\
{							\
  int i;						\
  Type *elt;						\
							\
  /* lets see if we have an */				\
  /* open window for this account */			\
  found = NULL;						\
  if (id && list) {					\
    i = 0;						\
    elt = list[i];					\
    while (elt) {					\
      if (id == elt->member) found = elt;		\
      i++;						\
      elt = list[i];					\
    } 							\
  } 							\
}

#define FETCH_FROM_LIST(Type,list,id,member,elt) 	\
{							\
  int i;						\
  Type **newlist;					\
  if (!(id)) return 0x0;				\
							\
  /* lets see if we already have an */			\
  /* open window for this account */			\
  i = 0;						\
  if (list) {						\
    elt = list[i];					\
    while (elt) {					\
      if ((id) == elt->member) return elt;		\
      i++;						\
      elt = list[i];					\
    }    						\
  } 							\
							\
  /* if we are here, we didn't find it */		\
  newlist = (Type **) malloc ((i+2) * sizeof (Type *));	\
  i = 0;						\
  if (list) {						\
    elt = list[i];					\
    while (elt) {					\
      newlist[i] = elt;					\
      i++;						\
      elt = list[i];					\
    }    						\
    free (list);					\
  }							\
  list = newlist;					\
							\
  /* malloc a new one, add it to the list */		\
  elt = (Type *) malloc (sizeof (Type));		\
  elt->member = (id);					\
  list [i] = elt;					\
  list [i+1] = NULL;					\
}

#define REMOVE_FROM_LIST(Type,list,id,member)	 	\
{							\
  int i,j;						\
  Type *elt;						\
							\
  /* find the item in the list, and remove it */	\
  if (list && id) {					\
    i = j = 0;						\
    elt = list[i];					\
    while (elt) {					\
      list[j] = elt;					\
      if (id == elt->member) j--; 			\
      i++; j++;						\
      elt = list[i];					\
    }    						\
    list[j] = NULL;					\
  }							\
}


#endif /* __XACC_UTIL_H__ */
