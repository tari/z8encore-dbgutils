/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: ocd.h,v 1.1 2004/08/03 14:23:48 jnekl Exp $
 *
 * This is a generic interface class to be inherited by
 * specific ez8 on-chip debugger interface classes 
 * (serial, parallel, tcp-ip).
 */

#ifndef	OCD_HEADER
#define	OCD_HEADER

#include	<stdlib.h>
#include	<inttypes.h>

/**************************************************************/

class ocd
{
private:
	/* Prohibit use of copy constructor */
	ocd(ocd &);

public:
	ocd() { };
	virtual ~ocd() { };

	virtual void reset(void) = 0;

	virtual bool link_open(void) = 0;
	virtual bool link_up(void) = 0;
	virtual bool available(void) = 0;

	virtual void read(uint8_t *, size_t) = 0;
	virtual void write(const uint8_t *, size_t) = 0;
};

/**************************************************************/

#endif	/* OCD_HEADER */

