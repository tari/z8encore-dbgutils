/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: ez8mon.cpp,v 1.1 2004/08/03 14:23:48 jnekl Exp $
 *
 * This is the debugger program entry point. Initialize environment,
 * then call main command loop.
 */

#include	<stdio.h>
#include	<stdlib.h>

/**************************************************************/

const char *banner = "Z8 Encore! command line debugger";

/**************************************************************/

extern int init(int, char **);
extern int finish(void);
extern int command_loop(void);

int main(int argc, char **argv)
{
	try {
		int err;

		err = init(argc, argv);
		if(err) {
			return EXIT_FAILURE;
		}

		err = command_loop();
		if(err) {
			return EXIT_FAILURE;
		}

		err = finish();
		if(err) {
			return EXIT_FAILURE;
		}
	} catch(char *err) {
		fprintf(stderr, "%s", err);
		fflush(stderr);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/**************************************************************/


