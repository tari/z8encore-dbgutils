/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: endurance.cpp,v 1.3 2008/10/02 17:55:21 jnekl Exp $
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
#include	<sys/wait.h>
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
#define	DEFAULT_MTU 4096
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

int max_error_retry = 3;
int maximum_cycles = 4;
int verify_repeat_count = 100;
int cycle = 0;

char *mailto;
char *state_filename = "cycle";
FILE *state_file;
volatile int done;
int errors = 0;

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
printf("  -i               display information about device\n");
printf("  -p SERIALPORT    specify serialport to use (default: %s)\n",
    serialport);
printf("  -b BAUDRATE      use baudrate (default: %d)\n", 
    baudrate);
printf("  -t MTU           maximum transmission unit (default %d)\n", 
    mtu);
printf("  -c FREQUENCY     clock frequency in hertz (default: %d)\n", 
    xtal);
printf("  -l COUNT         maximum cycle count\n");
printf("  -v COUNT         verify repeat count (default: %d)\n", 
    verify_repeat_count);
printf("  -m ADDRS         email results to ADDRS\n");
printf("  -s FILE          save state in FILE (default: %s)\n", 
    state_filename);
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

	if(done > 3) {
		fprintf(stderr, "exiting...\n");
		exit(EXIT_FAILURE);
	} else if(done > 2) {
		fprintf(stderr, "abort before finished ?\n");
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

	while((c = getopt(argc, argv, "hp:b:c:t:l:v:m:s:")) != EOF) {
		switch(c) {
		case '?':
			printf("Try '%s -h' for more information.\n", argv[0]);
			exit(EXIT_FAILURE);
			break;
		case 'h':
			help();
			exit(EXIT_SUCCESS);
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

			maximum_cycles = strtol(optarg, &end, 0);
			if(end && *end) {
				fprintf(stderr, "Invalid number %s\n",
				    optarg);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'v': {
			char *end;

			verify_repeat_count = strtol(optarg, &end, 0);
			if(end && *end) {
				fprintf(stderr, "Invalid number %s\n",
				    optarg);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'm':
			mailto = strdup(optarg);
			if(!mailto) {
				perror("strdup");
				exit(EXIT_FAILURE);
			}
			break;
		case 's':
			state_filename = strdup(optarg);
			if(!state_filename) {
				perror("strdup");
				exit(EXIT_FAILURE);
			}
			break;
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

	buff = (uint8_t *)xmalloc(MEMSIZE);
	blank = (uint8_t *)xmalloc(MEMSIZE);

	memset(buff, 0xff, MEMSIZE);
	memset(blank, 0xff, MEMSIZE);

	return 0;
}

/**************************************************************/

void mail_status(char *subject, char *message)
{
	int pid;
	int fd[2];
	int status;
	FILE *stream;

	// if noone to send to, return
	if(!mailto) {
		return;
	}

	// create pipe
	if(pipe(fd)) {
		perror("pipe");
		return;
	}

	// fork child process
	pid = fork();
	if(pid < 0) {
		perror("fork");
		close(fd[0]);
		close(fd[1]);
		return;
	} else if(!pid) {
		// exec sendmail
		close(fd[1]);
		dup2(fd[0], STDIN_FILENO);
		close(fd[0]);
		execlp("sendmail", "sendmail", "-t", "-i", NULL);
		perror("execlp");
		exit(EXIT_FAILURE);
	}
	close(fd[0]);
	stream = fdopen(fd[1], "w");

	// generate message
	fprintf(stream, "To: %s\n", mailto);
	fprintf(stream, "Subject: cycle %d - %s\n\n", cycle, subject);
	fprintf(stream, "cycle %d\n", cycle);
	if(message) {
		fprintf(stream, "%s\n", message);
	}
	fclose(stream);

	// wait for sendmail to exit
	waitpid(pid, &status, 0);

	return;
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
	try {
		dbg->flash_mass_erase();
	} catch(char *err) {
		mail_status("error during mass erase", err);
		fprintf(stderr, "cycle %d: error during mass erase: %s", 
		    cycle, err);
		return -1;
	}

	/* if memory read protect enabled, 
	 * reset after erased to clear it */
	if(dbg->state(dbg->state_protected)) {
		try {
			dbg->reset_chip();
		} catch(char *err) {
			mail_status("error resetting chip", err);
			fprintf(stderr, "cycle %d: error resetting chip: %s", 
			    cycle, err);
			return -1;
		}
	}

	return 0;
}

/**************************************************************/

int blank_check(void)
{
	int i;

	for(i=0; i<verify_repeat_count; i++) {
		uint16_t crc;

		try {
			crc = dbg->rd_crc();
		} catch(char *err) {
			mail_status("error reading crc", err);
			fprintf(stderr, "cycle %d: error reading crc: %s", 
			    cycle, err);
			return -1;
		}

		if(crc != blank_crc) {
			errors++;
			mail_status("blank check failed", "CRC mismatch");
			fprintf(stderr, "cycle %d: blank check failed: %s", 
			    cycle, "CRC mismatch");
			return -1;
		}
	}

	return 0;
}

/**************************************************************/

int program_device(void)
{
	try {
		dbg->wr_mem(0x0000, buff, 0x10000);
	} catch(char *err) {
		errors++;
		mail_status("programming failure", err);
		fprintf(stderr, "cycle %d: programming failure: %s", 
		    cycle, err);
		return -1;
	}
 
	return 0;
}

/**************************************************************/

int verify_device(void)
{
	int i;

	for(i=0; i<verify_repeat_count; i++) {
		uint16_t crc;

		try {
			crc = dbg->rd_crc();
		} catch(char *err) {
			mail_status("error reading crc", err);
			fprintf(stderr, "cycle %d: error reading crc: %s", 
			    cycle, err);
			return -1;
		}

		if(crc != buff_crc) {
			errors++;
			mail_status("program verify failed", "CRC mismatch");
			fprintf(stderr, "cycle %d: program verify failed: %s", 
			    cycle, "CRC mismatch");
		}
	}

	return 0;
}

/**************************************************************/

FILE *open_state(char *filename)
{
	FILE *file;
	char *end;
	char buff[16];

	file = fopen(filename, "w+");
	if(!file) {
		perror("fopen");
		exit(EXIT_FAILURE);
	}
	if(fgets(buff, sizeof(buff), file)) {
		cycle = strtol(buff, &end, 0);
	}

	return file;
}

void save_state(FILE *file)
{
	if(fseek(file, 0, SEEK_SET)) {
		perror("fseek");
		return;
	}
	if(fprintf(file, "%d\n", cycle) < 0) {
		perror("fprintf");
		return;
	}
	if(fflush(file)) {
		perror("fflush");
		return;
	}
	if(fsync(fileno(file))) {
		perror("fsync");
		return;
	}
}

void close_state(FILE *file)
{
	save_state(file);
	fclose(file);
}

/**************************************************************/

int run(void)
{
	int err;

	if(configure()) {
		return -1;
	}

	/* TODO: if mem_size unknown, will be zero. 
	   Need to automatically determine memory size. 
	   Can determine memory size by erasing and reading blank crc. */
	blank_crc = crc_ccitt(0x0000, blank, mem_size);

	err = 0;

	mail_status("started", NULL);

	while(!done && errors < 3 && 
	      (maximum_cycles <= 0 || cycle < maximum_cycles)) {

		// save state every 10 cycles, about every 60 seconds
		if(!(cycle % 10)) {
			save_state(state_file);
		}

		// report progress every 10000 cycles, about every 16 hours
		if(cycle && !(cycle % 10000)) {
			mail_status("running", NULL);
		}

		// if communication error, retry
		if(err) {
			int retry;
	
			retry = max_error_retry;
	
			while(err && retry > 0) {
				retry--;		
				try {
					dbg->reset_link();
					dbg->stop();
					dbg->reset_chip();
					err = 0;
				} catch(char *err) {
					mail_status("communication error", err);
					fprintf(stderr, "cycle %d: %s", 
					    cycle, err);
				}
			}
			if(err) {
				return -1;
			}
		}

		err = erase_device();
		if(err) {
			continue;
		}

		err = blank_check();
		if(err) {
			continue;
		}

		switch(cycle % 4) {
		case 0:	{	/* checkerboard */
			int addr;

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
			memset(buff, 0x00, MEMSIZE);
			break;
		}
		case 3: {	/* random data */
			int addr;

			for(addr=0; addr<mem_size; addr++) {
				buff[addr] = random();
			}
			break;
		}
		}
		buff[0] = 0xff;
		buff_crc = crc_ccitt(0x0000, buff, mem_size);

		err = program_device();
		if(err) {
			continue;
		}

		err = verify_device();
		if(err) {
			continue;
		}

		cycle++;
		errors = 0;
	}

	erase_device();

	return 0;
}

/**************************************************************/

int main(int argc, char **argv)
{
	int err;

	printf("%s - build %s\n", banner, build);

	catch_signals();
	err = setup(argc, argv);
	if(err) {
		return EXIT_FAILURE;
	}

	state_file = open_state(state_filename);
	err = run();
	close_state(state_file);
	mail_status("finished", NULL);
	if(err) {
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/**************************************************************/


