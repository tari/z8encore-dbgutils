/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: ocd_tcpip.h,v 1.1 2004/08/03 14:23:48 jnekl Exp $
 *
 * This is the network interface class for the ez8 on-chip 
 * debugger.
 */

#ifndef	OCD_TCPIP_HEADER
#define	OCD_TCPIP_HEADER

#include	<stdlib.h>
#include	<inttypes.h>

#include	"sockstream.h"
#include	"ocd.h"

/**************************************************************/

class ocd_tcpip : public ocd
{
private:
	SOCK *s;
	bool open, up;
	int version_major, version_minor;
	char *buff;

	/* Prohibit use of copy constructor */
	ocd_tcpip(ocd_tcpip &);	

	void connect_server(char *);
	void validate_server(void);
	void auth_server(char *);

public:
	ocd_tcpip();
	~ocd_tcpip();

	void connect(const char *);
	void reset(void);

	bool link_open(void);
	bool link_up(void);
	bool available(void);

	void read(uint8_t *, size_t);
	void write(const uint8_t *, size_t);
};

/**************************************************************/

#endif	/* OCD_TCPIP_HEADER */

