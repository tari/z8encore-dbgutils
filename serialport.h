/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: serialport.h,v 1.1 2004/08/03 14:23:48 jnekl Exp $
 *
 * This is a generic api for serial ports on both unix and
 * windows systems.
 */

#ifndef	SERIALPORT_HEADER
#define	SERIALPORT_HEADER

#include	<stdlib.h>
#ifdef	_WIN32
#include	<windows.h>
#endif

/**************************************************************/

#define	SERIAL_XONXOFF_INPUT	0x0001
#define	SERIAL_XONXOFF_OUTPUT	0x0002
#define	SERIAL_RTS_INPUT	0x0004
#define	SERIAL_CTS_OUTPUT	0x0008


struct baudvalue {
	int value;
	int define;
};

extern "C" const struct baudvalue baudrates[];

enum serial_exception {
	serial_none,
	serial_state,		// invalid state
	serial_arguments,	// invalid arguments
	serial_open,		// open failed
	serial_configure,	// configuration failed
	serial_io,		// io read/write failure
};

/**************************************************************/

class serialport 
{

private:
	#ifndef	_WIN32
	int fdes;
	#else	/* _WIN32 */
	HANDLE fdes;
	#endif	/* _WIN32 */

	static int baud2def(int);
	static int def2baud(int);

	#ifndef	_WIN32
	void setflock(void);
	#endif	/* _WIN32 */

	#ifdef	_WIN32	
	void seterror(char *, size_t n);
	#endif	/* _WIN32 */

protected:

public:

	int baudrate;
	enum parity_enum { none, odd, even, mark, space } parity;
	enum stopbits_enum { one, two } stopbits;
	enum size_enum { five, six, seven, eight } size;
	int flowcontrol;
	long readtimeout;

	serialport();
	~serialport();

	void configure(void);
	void loadconfig(void);
	
	void open(const char *);
	void close(void);
	int  read(void *, size_t);
	void write(const void *, size_t);
	bool available(void);

	void sendbreak(void);
	void flush(void);
};

/**************************************************************/

#endif	/* SERIALPORT_HEADER */



