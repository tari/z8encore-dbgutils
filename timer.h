/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: timer.h,v 1.1 2004/08/03 14:23:48 jnekl Exp $
 *
 * Functions for dealing with time.
 */

#ifndef	TIMER_H
#define	TIMER_H

#ifdef	__cplusplus
extern "C" { 
#endif

struct timer {
	struct timeval start;
	struct timeval stop;
};

void timerstart(struct timer *);
void timerstop(struct timer *);
char *timerstr(struct timer *);

#ifdef	__cplusplus
};
#endif

#endif	/* TIMER_H */

