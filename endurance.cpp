/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: endurance.cpp,v 1.1 2004/08/03 14:23:48 jnekl Exp $
 *
 * Utility to do flash endurance testing.
 */

#include	<stdio.h>
#include	<unistd.h>
#include	<signal.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	<string.h>
#include	<assert.h>
#include	<readline/readline.h>
#include	"xmalloc.h"

#include	"ez8dbg.h"
#include	"crc.h"
#include	"hexfile.h"
#include	"version.h"

/**************************************************************/

#ifndef	DEFAULT_SERIALPORT
#define	DEFAULT_SERIALPORT "auto"
#endif

#ifndef	DEFAULT_BAUDRATE
#ifndef	_WIN32
#define	DEFAULT_BAUDRATE 115200
#else
#define	DEFAULT_BAUDRATE 57600
#endif
#endif

#ifndef	DEFAULT_XTAL
#define	DEFAULT_XTAL 18432000
#endif

#ifndef	DEFAULT_MTU
#define	DEFAULT_MTU 1024
#endif

#define	MEMSIZE	0x10000

/**************************************************************/

const char *banner = "Endurance Utility";
const char *progname;

char *serialport = DEFAULT_SERIALPORT;
int baudrate = DEFAULT_BAUDRATE;
int mtu = DEFAULT_MTU;
int xtal = DEFAULT_XTAL;
char *programfilename = NULL;

uint8_t *buff, *blank;
uint16_t buff_crc, blank_crc;
int mem_size = 0;
int max_mem = 0;
int raw_commands = 0;

int max_error_retry = 3;
int loop_repeat_count = 4;
int erase_verify_repeat_count = 64;
int program_verify_repeat_count = 512;

volatile int done;

struct {
	long comm_errors;
	long erase;
	long erase_verify;
	long erase_verify_pass;
	long program;
	long program_verify;
	long program_verify_pass;
} stats;

/**************************************************************/

char *serialport_selection[] = 
#if (!defined _WIN32) && (defined __sun__)
{ "/dev/ttya", "/dev/ttyb", NULL };
#elif (!defined _WIN32)
{ "/dev/ttyS0", "/dev/ttyS1", "/dev/ttyS2", "/dev/ttyS3", NULL };
#else
{ "com1", "com2", "com3", "com4", NULL };
#endif

int baudrate_selection[] = { 115200, 57600, 38400, 0 };
int xtal_selection[] = { 20000000, 18432000, 1000000, 0 };

static ez8dbg _dbg;
static ez8dbg *dbg = &_dbg;

/**************************************************************/

void help(void)
{
printf("%s - build %s\n", progname, build);
printf("Usage: %s [OPTION]... [FILE]\n", progname);
printf( "Utility to test endurance of Z8 Encore! flash devices.\n\n");
printf("  -h               show this help\n");
printf("  -r               show raw ocd commands\n");
printf("  -i               display information about device\n");
printf("  -p SERIALPORT    specify serialport to use (default: %s)\n",
    DEFAULT_SERIALPORT);
printf("  -b BAUDRATE      use baudrate (default: %d)\n", 
    DEFAULT_BAUDRATE);
printf("  -t MTU           maximum transmission unit (default %d)\n", 
    DEFAULT_MTU);
printf("  -c FREQUENCY     clock frequency in hertz (default: %d)\n", 
    DEFAULT_XTAL);
printf("  -l COUNT         loop count\n");
printf("  -v COUNT         program verify count\n");
printf("  -e COUNT         erase verify count\n");
printf("\n");

return;
}

/**************************************************************/

void signal_handler(int signal)
{
	if(!done) {
		fprintf(stderr, 
		    "caught signal, exiting after this pass\n");
	}
	done++;

	if(done > 5) {
		fprintf(stderr, "alright already, exiting...\n");
		exit(EXIT_FAILURE);
	}
}

int catch_signals(void)
{
	int err;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	err = sigaction(SIGINT, &sa, NULL);
	if(err) {
		perror("sigaction");
		return -1;
	}

	return 0;
}

/**************************************************************/

int setup(int argc, char **argv)
{
	int c;
	char *last, *s;
	double clock;

	progname = *argv;
	s = strrchr(progname, '/');
	if(s) {
		progname = s+1;
	}
	s = strrchr(progname, '\\');
	if(s) {
		progname = s+1;
	}

	while((c = getopt(argc, argv, "hrp:b:c:t:l:v:e:")) != EOF) {
		switch(c) {
		case '?':
			printf("Try '%s -h' for more information.\n", argv[0]);
			exit(EXIT_FAILURE);
			break;
		case 'h':
			help();
			exit(EXIT_SUCCESS);
			break;
		case 'r':
			raw_commands = 1;
			break;
		case 'p':
			serialport = optarg;
			break;
		case 'b':
			baudrate = strtol(optarg, &last, 0);
			if(!last || *last || last == optarg) {
				fprintf(stderr, 
				    "Invalid baudrate \'%s\'\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'c':
			clock = strtod(optarg, &last);
			if(!last || last == optarg) {
				fprintf(stderr, 
				    "Invalid clock frequency \'%s\'\n", 
				    optarg);
				exit(EXIT_FAILURE);
			}
			if(*last == 'k' || *last == 'K') {
				clock *= 1000;
				last++;
			} else if(*last == 'M') {
				clock *= 1000000;
				last++;
			}

			if(*last && strcasecmp(last, "Hz")) {
				fprintf(stderr, "Invalid clock suffix '%s'\n",
				    last);
				exit(EXIT_FAILURE);
			}
			if(clock < 20000 || clock > 65000000) {
				fprintf(stderr, 
				    "Clock frequency out of range\n");
				exit(EXIT_FAILURE);
			}
			xtal = (int)clock;
			break;
		case 't':
			mtu = strtol(optarg, &last, 0);
			if(last == NULL || last == optarg || *last != '\0') {
				fprintf(stderr, 
				    "Invalid MTU \'%s\'\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'l': {
			char *end;

			loop_repeat_count = strtol(optarg, &end, 0);
			if(end && *end) {
				fprintf(stderr, "Invalid number %s\n",
				    optarg);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'v': {
			char *end;

			program_verify_repeat_count = strtol(optarg, &end, 0);
			if(end && *end) {
				fprintf(stderr, "Invalid number %s\n",
				    optarg);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'e': {
			char *end;

			erase_verify_repeat_count = strtol(optarg, &end, 0);
			if(end && *end) {
				fprintf(stderr, "Invalid number %s\n",
				    optarg);
				exit(EXIT_FAILURE);
			}
			break;
		}
		default:
			abort();
		}
	}

	if(optind != argc) {
		fprintf(stderr, "%s: too many arguments\n", argv[0]);
		fprintf(stderr, "Try '%s -h' for more information.\n", 
		    argv[0]);
		exit(EXIT_FAILURE);
	} 

	dbg->mtu = mtu;
	dbg->set_sysclk(xtal);
	if(raw_commands) {
		dbg->log_proto = stdout;
	}

	buff = (uint8_t *)xmalloc(MEMSIZE);
	blank = (uint8_t *)xmalloc(MEMSIZE);

	memset(buff, 0xff, MEMSIZE);
	memset(blank, 0xff, MEMSIZE);

	return 0;
}

/**************************************************************/

int connect(void)
{
	int i;
	char *port;

	if(strcasecmp(serialport, "auto") == 0) {

		printf("Autoconnecting to device ... ");
		fflush(stdout);

		for(i=0; serialport_selection[i]; i++) {
			port = serialport_selection[i];
			try {
				dbg->connect_serial(port, baudrate);
			} catch(char *err) {
				continue;
			}

			try {
				dbg->reset_link();
			} catch(char *err) {
				dbg->disconnect();
				continue;
			}

			printf("found on %s\n", port);
			return 0;
		}

		printf("fail\n");
		printf("Could not connect to device.\n");

		return -1;

	} else {
		try {
			dbg->connect_serial(serialport, baudrate);
		} catch(char *err) {
			printf("Could not connect to device\n");
			fprintf(stderr, "%s", err);
			return -1;
		}
		try {
			dbg->reset_link();
		} catch(char *err) {
			printf("Could not communicate with device\n");
			fprintf(stderr, "%s", err);
			dbg->disconnect();
		}
	}

	return 0;
}

/**************************************************************/

int configure(void)
{
	int err;

	err = connect();
	if(err) {
		return -1;
	}

	try {
		dbg->stop();
		dbg->reset_chip();
		mem_size = dbg->memory_size();
	} catch(char *err) {
		fprintf(stderr, "%s", err);
		return -1;
	}

	return 0;
}

/**************************************************************/

int erase_device(void)
{
	printf("Erasing device ... ");
	fflush(stdout);

	try {
		dbg->flash_mass_erase();
	} catch(char *err) {
		printf("fail\n");
		fprintf(stderr, "%s", err);
		return -1;
	}

	/* if memory read protect enabled, 
	 * reset after erased to clear it */
	if(dbg->state(dbg->state_protected)) {
		try {
			dbg->reset_chip();
		} catch(char *err) {
			printf("fail\n");
			fprintf(stderr, "%s", err);
			return -1;
		}
	}

	printf("ok\n");

	return 0;
}

/**************************************************************/

int blank_check(void)
{
	int i;

	printf("Blank check ");
	fflush(stdout);

	for(i=0; i<erase_verify_repeat_count; i++) {
		uint16_t crc;

		stats.erase_verify++;
		try {
			crc = dbg->rd_crc();
		} catch(char *err) {
			printf(" fail\n");
			fprintf(stderr, "%s", err);
			return -1;
		}

		if(crc != blank_crc) {
			printf("X");
		} else {
			printf(".");
			stats.erase_verify_pass++;
		}
		fflush(stdout);
	}

	printf(" ok\n");
	
	return 0;
}

/**************************************************************/

int program_device(void)
{
	printf("Programming device ... ");
	fflush(stdout);
	try {
		dbg->wr_mem(0x0000, buff, 0x10000);
	} catch(char *err) {
		printf("fail\n");
		fprintf(stderr, "%s", err);
		return -1;
	}
 
	printf("ok\n");

	return 0;
}

/**************************************************************/

int verify_device(void)
{
	int i;

	printf("Verifying ");
	fflush(stdout);

	for(i=0; i<program_verify_repeat_count; i++) {
		uint16_t crc;

		stats.program_verify++;
		try {
			crc = dbg->rd_crc();
		} catch(char *err) {
			printf(" fail\n");
			fprintf(stderr, "%s", err);
			return -1;
		}

		if(crc != buff_crc) {
			printf("X");
		} else {
			printf(".");
			stats.program_verify_pass++;
		}
		fflush(stdout);
	}

	printf(" ok\n");

	return 0;
}

/**************************************************************/

int run(void)
{
	int err;

	if(configure()) {
		return -1;
	}

	blank_crc = crc_ccitt(0x0000, blank, mem_size);

	err = 0;

	while(loop_repeat_count > 0 && !done) {

		if(err) {
			int retry;
	
			stats.comm_errors++;
			retry = max_error_retry;
	
			while(err && retry > 0) {
				retry--;		
				try {
					dbg->reset_link();
					dbg->stop();
					dbg->reset_chip();
					err = 0;
				} catch(char *err) {
					fprintf(stderr, "%s", err);
				}
			}
		}

		err = erase_device();
		if(err) {
			continue;
		}
		stats.erase++;

		err = blank_check();
		if(err) {
			continue;
		}

		switch(loop_repeat_count % 4) {
		case 0:	{	/* checkerboard */
			int addr;

			printf("Generating checkerboard ... ");
			fflush(stdout);
			for(addr=0; addr<mem_size; addr++) {
				if(addr & 1) {
					buff[addr] = 0x55;
				} else {
					buff[addr] = 0xaa;
				}
			}
			break;
		}
		case 1: {	/* reverse checkerboard */
			int addr;

			printf("Generating reverse checkerboard ... ");
			fflush(stdout);
			for(addr=0; addr<mem_size; addr++) {
				if(addr & 1) {
					buff[addr] = 0xaa;
				} else {
					buff[addr] = 0x55;
				}
			}
			break;
		}
		case 2: {	/* zeros */
			printf("Generating zeros ... ");
			fflush(stdout);
			memset(buff, 0x00, MEMSIZE);
			break;
		}
		case 3: {	/* random data */
			int addr;

			printf("Generating random data ... ");
			fflush(stdout);
			for(addr=0; addr<mem_size; addr++) {
				buff[addr] = random() & 0xff;
			}
			break;
		}
		}
		buff[0] = 0xff;
		buff_crc = crc_ccitt(0x0000, buff, mem_size);
		printf("ok\n");

		err = program_device();
		if(err) {
			continue;
		}
		stats.program++;

		err = verify_device();
		if(err) {
			continue;
		}

		loop_repeat_count--;
	}

	printf("Cleaning up ... (erasing device before exit)\n");
	erase_device();

	return 0;
}

/**************************************************************/

void print_stats(void)
{
	printf("\n");
	printf("summary\n");
	printf("----------------\n");
	printf("    erase = %ld\n", stats.erase);
	printf("        erase_verify      = %ld\n", stats.erase_verify);
	printf("        erase_verify_pass = %ld\n", stats.erase_verify_pass);
	printf("    program = %ld\n", stats.program);
	printf("        program_verify    = %ld\n", stats.program_verify);
	printf("        program_verify    = %ld\n", stats.program_verify_pass);
	printf("    comm_errors = %ld\n", stats.comm_errors);
	printf("\n");

	return;
}

/**************************************************************/

int main(int argc, char **argv)
{
	int err;

	catch_signals();
	err = setup(argc, argv);
	if(err) {
		return EXIT_FAILURE;
	}

	printf("%s - build %s\n", banner, build);

	err = run();
	print_stats();

	if(err) {
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/**************************************************************/


