/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: timer.c,v 1.1 2004/08/03 14:23:48 jnekl Exp $
 *
 * Functions for dealing with time.
 */

#include	<stdio.h>
#include	<sys/time.h>
#include	<string.h>
#include	"xmalloc.h"
#include	"timer.h"

/* subtract two struct timeval */
void difftimeval(
	struct timeval *start, 
	struct timeval *end, 
	struct timeval *elapsed
)
{
	elapsed->tv_sec = end->tv_sec - start->tv_sec;
	elapsed->tv_usec = end->tv_usec - start->tv_usec;
	if(elapsed->tv_usec < 0) {
		elapsed->tv_sec--;
		elapsed->tv_usec += 1000000;
	}
}

#define	STRSIZE	16

char *timevalstr(struct timeval *t)
{
	long hours, minutes, seconds, ms;
	static char s[STRSIZE];

	seconds = t->tv_sec;
	hours = seconds / (60*60);
	seconds %= (60*60);
	minutes = seconds / 60;
	seconds %= 60;
	ms = t->tv_usec / 1000;

	if(hours) {
		snprintf(s, sizeof(s), "%dh%02dm%02d.%03ds",
		    (int)hours, (int)minutes, (int)seconds, (int)ms);
	} else if(minutes) {
		snprintf(s, sizeof(s), "%dm%02d.%03ds", 
		    (int)minutes, (int)seconds, (int)ms);
	} else {
		snprintf(s, sizeof(s), "%d.%03ds", (int)seconds, (int)ms);
	}

	return s;
}

void timerstart(struct timer *t)
{
	if(gettimeofday(&t->start, NULL)) {
		memset(&t->start, 0, sizeof(t->start));
	}
}

void timerstop(struct timer *t)
{
	if(gettimeofday(&t->stop, NULL)) {
		memset(&t->stop, 0, sizeof(t->stop));
	}
}

char *timerstr(struct timer *t)
{
	struct timeval elapsed;
	char *s;

	difftimeval(&t->start, &t->stop, &elapsed);
	s = timevalstr(&elapsed);
	return s;
}

