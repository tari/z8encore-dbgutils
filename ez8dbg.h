/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: ez8dbg.h,v 1.1 2004/08/03 14:23:48 jnekl Exp $
 *
 * This implements a debugger api for the ez8 on-chip debugger.
 */

#ifndef	EZ8DBG_HEADER
#define	EZ8DBG_HEADER

#include	<stdlib.h>
#include	<inttypes.h>

#include	"ez8ocd.h"

/**************************************************************/

/* cache status */
#define	DBGREV_CACHED	0x01
#define	DBGCTL_CACHED	0x02
#define	DBGSTAT_CACHED	0x04
#define	PC_CACHED	0x08
#define	CRC_CACHED	0x10
#define	MEMCRC_CACHED	0x20
#define	MEMSIZE_CACHED	0x40
#define	FCTL_CACHED	0x80

/* 5 second reset timeout (typical reset is 10ms) */
#define	RESET_TIMEOUT	5

/**************************************************************/

class ez8dbg : public ez8ocd
{
private:
	/* pending error */
	int error;

	/* copy constructor prohibited */
	ez8dbg(ez8dbg &);	

	/* cached data */
	int cache;
	uint16_t dbgrev;
	uint8_t dbgctl;
	uint8_t dbgstat;
	uint16_t pc;
	uint16_t crc;
	uint16_t memcrc;
	uint8_t memsize;

	/* buffers */
	uint8_t *main_mem;
	uint8_t *info_mem;
	uint8_t *reg_mem;
	uint8_t *buffer;

	uint16_t sysclk;

	/* breakpoints */
	struct breakpoint_t {
		uint16_t address;
		uint8_t data;
	};
	struct breakpoint_t *breakpoints;
	int num_breakpoints;
	uint16_t tbreak;
	void delete_breakpoint(int);

	/* internal functions */
	void cache_dbgrev(void);
	void cache_dbgctl(void);
	void cache_dbgstat(void);
	void cache_pc(void);
	void cache_crc(void);
	void cache_memcrc(void);
	void cache_memsize(void);

	void save_flash_state(uint8_t *);
	void restore_flash_state(uint8_t *);
	void flash_setup(uint8_t, uint16_t);
	void flash_lock(void);
	void flash_page_erase(uint8_t);

protected:


public:
	ez8dbg();
	~ez8dbg();

	bool memcache_enabled;

	enum dbg_state {
		state_stopped = 1,
		state_running,
		state_protected,
		state_trace,
	};

	bool state(enum dbg_state);
	void flush_cache(void);

	void reset_chip(void);
	void reset_link(void);

	void stop(void);
	void run(void);
	void run_to(uint16_t);
	void run_clks(uint16_t);
	int isrunning(void);

	void step(void);
	void next(void);

	uint16_t rd_cntr(void);
	uint16_t rd_crc(void);
	uint16_t rd_pc(void);
	void wr_pc(uint16_t);
	void rd_regs(uint16_t, uint8_t *, size_t);
	void wr_regs(uint16_t, const uint8_t *, size_t);
	void rd_data(uint16_t, uint8_t *, size_t);
	void wr_data(uint16_t, const uint8_t *, size_t);
	void rd_mem(uint16_t, uint8_t *, size_t);
	void wr_mem(uint16_t, const uint8_t *, size_t);

	void rd_info(uint16_t, uint8_t *, size_t);
	void wr_info(uint16_t, const uint8_t *, size_t);

	void set_sysclk(unsigned long);
	void mass_erase(bool);
	void flash_mass_erase(void);
	void write_flash(uint16_t, const uint8_t *, size_t);

	bool breakpoint_set(uint16_t);
	int get_num_breakpoints(void);
	uint16_t get_breakpoint(int);
	void set_breakpoint(uint16_t);
	void remove_breakpoint(uint16_t);
	void read_mem(uint16_t, uint8_t *, size_t);

	int memory_size(void);

	uint8_t rd_trce_status(void);
	void wr_trce_ctl(uint8_t);	
	uint8_t rd_trce_ctl(void);
	void wr_trce_event(uint8_t, struct trce_event *);
	void rd_trce_event(uint8_t, struct trce_event *);
	uint16_t rd_trce_wr_ptr(void);
	void rd_trce_buff(uint16_t, struct trce_frame *, size_t);

};

/**************************************************************/

#endif	/* EZ8DBG_HEADER */

