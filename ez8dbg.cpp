/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: ez8dbg.cpp,v 1.2 2004/08/06 14:39:35 jnekl Exp $
 *
 * This implements the debugger api. It makes calls to the
 * lower level ez8ocd to do all its work.
 */

#define		_REENTRANT

#include	<stdio.h>
#include	<unistd.h>
#include	<string.h>
#include	<errno.h>
#include	<inttypes.h>
#include	<ctype.h>
#include	<sys/stat.h>
#include	<assert.h>
#include	<time.h>
#include	"xmalloc.h"

#ifdef	_WIN32
#include	"winunistd.h"
#endif

#include	"ez8dbg.h"
#include	"ez8.h"
#include	"crc.h"
#include	"err_msg.h"

/**************************************************************
 * Constructor for the debugger.
 */

ez8dbg::ez8dbg(void)
{
	cache = 0;
	memcache_enabled = 1;

	dbgrev = 0x0000;
	dbgstat = 0x00;
	dbgctl = 0x00;
	pc = 0x0000;
	crc = 0x0000;

	memcrc = 0x0000;
	memsize = 0;

	/* memory cache */
	main_mem = (uint8_t *)xmalloc(EZ8MEM_SIZE);
	memset(main_mem, 0xff, EZ8MEM_SIZE);

	info_mem = (uint8_t *)xmalloc(EZ8MEM_PAGESIZE);
	memset(info_mem, 0xff, EZ8MEM_PAGESIZE);

	reg_mem = (uint8_t *)xmalloc(EZ8REG_SIZE);
	memset(reg_mem, 0, EZ8REG_SIZE);

	/* scratch buffer */
	buffer = (uint8_t *)xmalloc(EZ8MEM_SIZE);
	memset(buffer, 0, EZ8MEM_SIZE);

	sysclk = 0x0000;

	num_breakpoints = 0;
	breakpoints = NULL;
	tbreak = 0;

	return;
}

/**************************************************************
 * Destructor for the debugger.
 */

ez8dbg::~ez8dbg(void)
{
	int addr;

	while(num_breakpoints > 0) {
		addr = breakpoints[num_breakpoints-1].address;
		remove_breakpoint(addr);
	}

	if(main_mem) {
		free(main_mem);
	}
	if(info_mem) {
		free(info_mem);
	}
	if(reg_mem) {
		free(reg_mem);
	}
	if(buffer) {
		free(buffer);
	}

	if(dbg && link_open() && link_up()) {
		ez8ocd::wr_dbgctl(0x00);
	}

	return;
}

/**************************************************************
 * state()
 */

bool ez8dbg::state(enum dbg_state state)
{
	switch(state) {

	/* check if we are fully stopped and in debug mode */
	case state_stopped:
		cache_dbgctl();
		if(dbgctl & DBGCTL_DBG_MODE) {
			return 1;
		}
		return 0;

	/* check if cpu is running  
	 * Cpu may not be running, but not stopped either. 
	 *   This happens when interrupts are being serviced 
	 *   in the background. */
	case state_running:
		cache_dbgstat();
		cache_dbgctl();
		if(dbgctl & DBGCTL_DBG_MODE) {
			/* fully stopped */
			return 0;
		}
		if(dbgstat & DBGSTAT_STOPPED) {
			/* stopped at breakpoint */
			return 0;
		}
		return 1;

	/* check if read protect enabled */
	case state_protected:
		cache_dbgstat();
		if(dbgstat & DBGSTAT_RD_PROTECT) {
			return 1;
		}
		return 0;

	/* check if trace available */
	case state_trace:
		cache_dbgrev();
		if(dbgrev & 0xc000) {
			/* assume all emulator versions 8000-8FFF
			 * have trace */
			return 1;
		} 
		return 0;
	default:
		abort();
	}

	return 0;
}

/**************************************************************
 * flush_cache()
 *
 * This will flush any currently cached items.
 */

void ez8dbg::flush_cache(void)
{
	cache = 0;

	return;
}

/**************************************************************
 * This will read and cache the dbgrev if it is not already
 * cached.
 */

void ez8dbg::cache_dbgrev(void)
{
	if(cache & DBGREV_CACHED) {
		return;
	}

	dbgrev = rd_dbgrev();
	cache |= DBGREV_CACHED;

	return;
}

/**************************************************************
 * This will read and cache the dbgctl register if it is 
 * not already cached.
 */

void ez8dbg::cache_dbgctl(void)
{
	if(cache & DBGCTL_CACHED) {
		return;
	}
	
	dbgctl = rd_dbgctl();
	cache |= DBGCTL_CACHED;

	return;
}

/**************************************************************
 * This will read and cache the dbgstat register if it is not
 * already cached.
 */

void ez8dbg::cache_dbgstat(void)
{
	if(cache & DBGSTAT_CACHED) {
		return;
	}
	
	dbgstat = rd_dbgstat();
	cache |= DBGSTAT_CACHED;

	return;
}

/**************************************************************
 * This will cache the program counter if it is not already cached.
 */

void ez8dbg::cache_pc(void)
{
	if(cache & PC_CACHED) {
		return;
	}

	pc = rd_pc();
	cache |= PC_CACHED;

	return;
}

/**************************************************************
 * This will read and cache the remote memory crc if it is
 * not already cached.
 */

void ez8dbg::cache_crc(void)
{
	if(cache & CRC_CACHED) {
		return;
	}

	crc = rd_crc();
	cache |= CRC_CACHED;

	return;
}

/**************************************************************
 * This will read and cache the memory size if it is not
 * already cached.
 */

void ez8dbg::cache_memsize(void)
{
	if(cache & MEMSIZE_CACHED) {
		return;
	}

	memsize = rd_memsize();
	cache |= MEMSIZE_CACHED;

	return;
}

/**************************************************************
 * This will determine the program memory size.
 */

int ez8dbg::memory_size(void)
{
	int size;

	cache_dbgrev();
	cache_memsize();
	switch(dbgrev) {
	case 0x0124: 	
	case 0x0128: 	
	case 0x012A: 	
		switch(memsize & 0x07) {
		case 0x00:
			size = 0x0400;
			break;
		case 0x01:
			size = 0x0800;
			break;
		case 0x02:
			size = 0x1000;
			break;
		case 0x03:
			size = 0x2000;
			break;
		case 0x04:
			size = 0x4000;
			break;
		case 0x05:
			size = 0x8000;
			break;
		case 0x06:
		case 0x07:
			size = 0x10000;
			break;
		default:
			abort();
		}
		break;
	default:
		switch(memsize & 0x07) {
		case 0x00:
			size = 0x0800;
			break;
		case 0x01:
			size = 0x1000;
			break;
		case 0x02:
			size = 0x2000;
			break;
		case 0x03:
			size = 0x4000;
			break;
		case 0x04:
			size = 0x6000;
			break;
		case 0x05:
			size = 0x8000;
			break;
		case 0x06:
			size = 0xc000;
			break;
		case 0x07:
			size = 0x10000;
			break;
		default:
			abort();
		}
		break;
	}

	return size;
}

/**************************************************************
 * This will calculate and cache the memory crc if it is not
 * already cached.
 */

void ez8dbg::cache_memcrc(void)
{
	int size;

	if(cache & MEMCRC_CACHED) {
		return;
	}

	/* get memory size */
	size = memory_size();

	/* calculate crc on memory cache */
	memcrc = crc_ccitt(0x0000, main_mem, size);
	cache |= MEMCRC_CACHED;

	return;
}

/**************************************************************
 * This will reset the device.
 */

void ez8dbg::reset_chip(void)
{
	time_t start;

	cache_dbgctl();
	wr_dbgctl(dbgctl | DBGCTL_RST);

	cache = 0;
	start = time(NULL);

	do {
		usleep(5000);
		cache &= ~DBGCTL_CACHED;
		cache_dbgctl();
	} while(dbgctl & DBGCTL_RST && time(NULL)-start < RESET_TIMEOUT);

	if(dbgctl & DBGCTL_RST) {
		strncpy(err_msg, "Reset chip failed\n"
		    "timeout waiting for reset to finish\n", err_len-1);
		throw err_msg;
	}

	return;
}

/**************************************************************
 * reset_link()
 *
 * This will reset the ocd link.
 */

void ez8dbg::reset_link(void)
{
	cache = 0;

	ez8ocd::reset_link();

	return;
}


/**************************************************************
 * This will stop the ez8 cpu and put the device in debug mode.
 *
 * If temporary breakpoint is set (from step over or run to),
 * clear it.
 */

void ez8dbg::stop(void)
{
	/* check cache to see if already stopped */
	if((cache & DBGCTL_CACHED) && (dbgctl & DBGCTL_DBG_MODE)) {
		return;
	}

	/* cache dbgctl to see if already stopped */
	cache &= ~DBGCTL_CACHED;
	try {
		cache_dbgctl();
	} catch(char *err) {
		/* if in stopmode, part will reset when we try to read it.
		 * reset link and try again */
		ez8ocd::reset_link();
		cache_dbgctl();
	}

	/* if part is not stopped, stop it */
	if(!(dbgctl & DBGCTL_DBG_MODE)) {
		uint8_t ctl;

		ctl = DBGCTL_DBG_MODE | DBGCTL_BRK_EN;
		wr_dbgctl(ctl);
		cache &= ~DBGCTL_CACHED;
		cache_dbgctl();
		if(dbgctl != ctl) {
			strncpy(err_msg, 
			    "Write on-chip debugger control register failed\n"
			    "readback verify failed\n", err_len-1);
			throw err_msg;
		}
	}

	/* if temporary breakpoint set (on older parts without
	 * a hardware breakpoint), clear it */
	if(tbreak) {
		remove_breakpoint(tbreak);
		tbreak = 0x0000;
	}

	return;
}

/**************************************************************
 * This will put the ez8 in run mode.
 */

void ez8dbg::run(void)
{
	/* check if already running */
	if(!state(state_stopped)) {
		return;
	}

	/* check if we can put into run mode */
	cache_dbgrev();
	switch(dbgrev) {
	case 0x0100:
	case 0x0110:
	case 0x0120:
		/* initial parts could not reenter run mode
		   if read protect enabled */
		if(state(state_protected)) {
			strncpy(err_msg, "Cannot put device into run mode\n"
			    "memory read protect is enabled\n", err_len-1);
			throw err_msg;
		}
	}

	/* check if breakpoint set where we are at */
	cache_pc();
	if(breakpoint_set(pc)) {
		step();
	}

	/* run */
	cache &= ~(PC_CACHED | CRC_CACHED);
	cache |= DBGCTL_CACHED;
	dbgctl = DBGCTL_BRK_EN | DBGCTL_BRK_ACK;
	wr_dbgctl(dbgctl);

	return;
}

/**************************************************************
 * This is used to run to a specific address (unless a 
 * breakpoint is reached before then).
 */

void ez8dbg::run_to(uint16_t addr)
{
	if(!state(state_stopped)) {
		strncpy(err_msg, "Cannot run to address\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	if(state(state_protected)) {
		strncpy(err_msg, "Cannot run to address\n"
		    "memory read protect enabled\n", err_len-1);
		throw err_msg;
	}

	/* if stopped at breakpoint, step over it */
	cache_pc();
	if(breakpoint_set(pc)) {
		step();
	}

	/* set address to stop at */
	dbgctl = DBGCTL_BRK_EN | DBGCTL_BRK_ACK;

	cache_dbgrev();
	switch(dbgrev) {
	case 0x0100:
	case 0x0110:
		/* these revisions do not have a hardware breakpoint 
		 * set a manual temporary breakpoint */
		assert(!tbreak);

		set_breakpoint(addr);
		tbreak = addr;
		break;
	default:
		wr_cntr(addr);
		dbgctl |= DBGCTL_BRK_PC;
	}

	cache &= ~(PC_CACHED | CRC_CACHED);
	cache |= DBGCTL_CACHED;
	wr_dbgctl(dbgctl);

	return;
}

/**************************************************************/

void ez8dbg::run_clks(uint16_t clks)
{
	uint16_t cntr;
	uint8_t ctl;

	if(!state(state_stopped)) {
		strncpy(err_msg, "Cannot run for duration of clocks\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	if(state(state_protected)) {
		strncpy(err_msg, "Cannot run for duration of clocks\n"
		    "memory read protect enabled\n", err_len-1);
		throw err_msg;
	}

	switch(dbgrev) {
	case 0x0100:
	case 0x0110:
		/* these revisions do not have a counter breakpoint */
		strncpy(err_msg, "Cannot run for duration of clock cycles\n"
		    "hardware version does not support clock runtime\n", 
		    err_len-1);
		throw err_msg;
	default:
		break;
	}

	wr_cntr(clks);
	cntr = rd_cntr();
	if(cntr != clks) {
		strncpy(err_msg, "Write on-chip debugger counter failed\n"
		    "readback verify failed\n", err_len-1);
		throw err_msg;
	}
	
	dbgctl = DBGCTL_BRK_EN | DBGCTL_BRK_ACK | DBGCTL_BRK_CNTR;
	cache &= ~(PC_CACHED | CRC_CACHED);
	cache |= DBGCTL_CACHED;

	wr_dbgctl(ctl);

	return;
}

/**************************************************************
 * This function returns zero if the device is stopped (in debug
 * mode). If the part is running, it returns 1, if an error occurs,
 * it returns -1.
 */

int ez8dbg::isrunning(void)
{
	if(cache & DBGCTL_CACHED) {
		if(dbgctl & DBGCTL_DBG_MODE) {
			return 0;
		} else if(!ez8ocd::rd_ack()) {
			return 1;
		}
	}
 
	cache &= ~DBGCTL_CACHED;
	cache_dbgctl();
	if(dbgctl & DBGCTL_DBG_MODE) {
		if(tbreak) {
			remove_breakpoint(tbreak);
			tbreak = 0x0000;
		} 

		return 0;
	} else {
		return 1;
	}
}

/**************************************************************
 * This will step into the next instruction.
 */

void ez8dbg::step(void)
{
	if(!state(state_stopped)) {
		strncpy(err_msg, "Could not single step instruction\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	if(state(state_protected)) {
		strncpy(err_msg, "Could not single step instruction\n"
		    "memory read protect is enabled\n", err_len-1);
		throw err_msg;
	}

	cache_dbgrev();
	switch(dbgrev) {
	case 0x0100:
		/* Workaround for z8f640ba pending interrupt bug */
		cache_pc();
		if(breakpoint_set(pc)) {
			int i;
			uint8_t irqctl;

			for(i=0; i<num_breakpoints; i++) {
				if(breakpoints[i].address == pc) {
					break;
				}
			}
			assert(i < num_breakpoints);
			ez8ocd::rd_regs(EZ8_IRQCTL, &irqctl, 1);
			if(irqctl & 0x80) {
				uint8_t data;

				data = irqctl & 0x7f;
				ez8ocd::wr_regs(EZ8_IRQCTL, &data, 1);
			}
			cache &= ~(PC_CACHED | CRC_CACHED);
			ez8ocd::stuf_inst(breakpoints[i].data);
			if((irqctl & 0x80) && 
			    (breakpoints[i].data != EZ8_DI_OPCODE)) {
				ez8ocd::wr_regs(EZ8_IRQCTL, &irqctl, 1);
			}
		} else {
			uint8_t irqctl;
			uint8_t opcode;
	
			ez8ocd::rd_regs(EZ8_IRQCTL, &irqctl, 1);
			if(irqctl & 0x80) {
				irqctl &= 0x7f;
				ez8ocd::wr_regs(EZ8_IRQCTL, &irqctl, 1);
				irqctl |= 0x80;
			}
			rd_mem(pc, &opcode, 1);

			cache &= ~(PC_CACHED | CRC_CACHED);
			ez8ocd::step_inst();
			if((irqctl & 0x80) && (opcode != EZ8_DI_OPCODE)) {
				ez8ocd::wr_regs(EZ8_IRQCTL, &irqctl, 1);
			}
		}
		break;

	default:
		cache_pc();
		if(breakpoint_set(pc)) {
			int i;

			for(i=0; i<num_breakpoints; i++) {
				if(breakpoints[i].address == pc) {
					break;
				}
			}
			assert(i < num_breakpoints);

			cache &= ~(PC_CACHED | CRC_CACHED);
			ez8ocd::stuf_inst(breakpoints[i].data);
		} else {
			cache &= ~(PC_CACHED | CRC_CACHED);
			ez8ocd::step_inst();
		}
		break;
	}

	return;
}

/**************************************************************
 * This will step over the next instruction.
 * 
 * In order to step over a call, this function will set
 * a temporary breakpoint after the call instruction. It will
 * then put the part into run mode and return immediately.
 * Software should poll isrunning() to see when the call is done,
 * or else call stop() to exit before the subroutine is
 * finished. 
 */

void ez8dbg::next(void)
{
	uint8_t buff[1];
	uint16_t addr;

	if(!state(state_stopped)) {
		strncpy(err_msg, "Could not step over instruction\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	if(state(state_protected)) {
		strncpy(err_msg, "Could not step over instruction\n"
		    "memory read protect is enabled\n", err_len-1);
		throw err_msg;
	}

	/* get instruction at program counter */
	cache_pc();
	rd_mem(pc, buff, 1);

	/* determine if we need to set a breakpoint */
	switch(*buff) {
	case 0xd6:			
		/* call da */
		addr = pc + 3;
		break;
	case 0xd4:			
		/* call irr */
		addr = pc + 2;
		break;
	default:
		addr = 0x0000;
		break;
	}

	if(addr) {
		run_to(addr);
	} else {
		step();
	}

	return;
}

/**************************************************************
 * This will read the 16 bit run counter that counts the time
 * between breakpoints.
 */

uint16_t ez8dbg::rd_cntr(void)
{
	uint16_t counter;

	if(!state(state_stopped)) {
		strncpy(err_msg, "Cannot read counter\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	counter = ez8ocd::rd_cntr();

	return counter;
}

/**************************************************************
 * This will read and cache the remote memory crc if it is
 * not already cached.
 */

uint16_t ez8dbg::rd_crc(void)
{
	uint16_t cyclic_redundancy_check;

	if(!state(state_stopped)) {
		strncpy(err_msg, "Cannot read crc\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	cyclic_redundancy_check = ez8ocd::rd_crc();

	return cyclic_redundancy_check;
}

/**************************************************************
 * This will read the program counter.
 */

uint16_t ez8dbg::rd_pc(void)
{
	uint16_t program_counter;

	if(!state(state_stopped)) {
		strncpy(err_msg, 
		    "Cannot read program counter\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	if(state(state_protected)) {
		strncpy(err_msg, 
		    "Cannot read program counter\n"
		    "memory read protect enabled\n", err_len-1);
		throw err_msg;
	}

	program_counter = ez8ocd::rd_pc();

	return program_counter;
}

/**************************************************************
 * This will write the program counter.
 */

void ez8dbg::wr_pc(uint16_t address)
{
	if(!state(state_stopped)) {
		strncpy(err_msg, "Cannot write program counter\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	if(state(state_protected)) {
		strncpy(err_msg, "Cannot write program counter\n"
		    "memory read protect is enabled\n", err_len-1);
		throw err_msg;
	}

	cache &= ~PC_CACHED;
	ez8ocd::wr_pc(address);
	cache_pc();
	if(pc != address) {
		strncpy(err_msg, "Write program counter failed\n"
		    "readback verify failed\n", err_len-1);
		throw err_msg;
	}

	return;
}

/**************************************************************
 * This will read from the register file.
 */

void ez8dbg::rd_regs(uint16_t address, uint8_t *data, size_t size)
{
	if(!state(state_stopped)) {
		strncpy(err_msg, "Cannot read register file\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	if(address < 0xf00) {
		if(state(state_protected)) {
			strncpy(err_msg, "Cannot read register file\n"
			    "memory read protect is enabled\n", err_len-1);
			throw err_msg;
		}
	}

	if(address + size > EZ8REG_SIZE) {
		strncpy(err_msg, "Cannot read register file\n"
		    "invalid address range\n", err_len-1);
		throw err_msg;
	}

	ez8ocd::rd_regs(address, data, size);

	return;
}

/**************************************************************
 * This will write data to the register file.
 */

void ez8dbg::wr_regs(uint16_t address, const uint8_t *data, size_t size)
{
	if(!state(state_stopped)) {
		strncpy(err_msg, "Cannot write register file\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	if(address+size > EZ8MEM_SIZE) {
		strncpy(err_msg, "Cannot write register file\n"
		    "invalid address range\n", err_len-1);
		throw err_msg;
	}

	if(address < 0xf00) {
		if(state(state_protected)) {
			strncpy(err_msg, "Cannot write register file\n"
			    "memory read protect enabled\n", err_len-1);
			throw err_msg;
		}
	}

	/* if writing to flash control register, clear cached crc */
	if(address <= EZ8_FIF_BASE && address + size > EZ8_FIF_BASE) {
		cache &= ~CRC_CACHED;
	}

	/* write data to register file */
	ez8ocd::wr_regs(address, data, size);

	/* Determine address range to verify.
	 * Peripherials may be read/write, so only verify ram.
	 */
	if(address >= EZ8_PERIPHERIAL_BASE) {
		size = 0;
	} else if(address + size > EZ8_PERIPHERIAL_BASE) {
		size = EZ8_PERIPHERIAL_BASE - address;
	}

	if(size > 0) {
		/* read back register ram */
		ez8ocd::rd_regs(address, reg_mem, size);

		/* compare with what was written */
		if(memcmp(reg_mem, data, size)) {
			strncpy(err_msg, "Register write failed\n"
			    "readback verify failed\n", 
			    err_len-1);
			throw err_msg;
		}
	}

	return;
}

/**************************************************************
 * This will read from data memory.
 */

void ez8dbg::rd_data(uint16_t address, uint8_t *buff, size_t size)
{
	if(!state(state_stopped)) {
		strncpy(err_msg, "Cannot read data memory\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	if(state(state_protected)) {
		strncpy(err_msg, "Cannot read data memory\n"
		    "memory read protect is enabled\n", err_len-1);
	}

	ez8ocd::rd_data(address, buff, size);

	return;
}

/**************************************************************
 * This will write data to external data memory.
 */

void ez8dbg::wr_data(uint16_t address, const uint8_t *buff, size_t size)
{
	if(!state(state_stopped)) {
		strncpy(err_msg, "Cannot write data memory\n"
		    "device is running\n", err_len-1);
		throw err_msg;
	}

	if(state(state_protected)) {
		strncpy(err_msg, "Cannot write data memory\n"
		    "memory read protect is enabled\n", err_len-1);
		throw err_msg;
	}

	ez8ocd::wr_data(address, buff, size);

	return;
}

/**************************************************************/


