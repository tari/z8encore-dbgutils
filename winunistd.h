/* $Id: winunistd.h,v 1.1 2004/08/03 14:23:48 jnekl Exp $
 */

#ifndef	UNISTD_HEADER
#define	UNISTD_HEADER

#include	<unistd.h>

#ifdef	_WIN32

#include	<windows.h>

#define	usleep(time)	Sleep( time%1000 ? (time/1000)+1 : time/1000 )

#endif	/* _WIN32 */

#endif	/* UNISTD_HEADER */

/**************************************************************/

