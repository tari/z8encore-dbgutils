/* $Id: tclmon.cpp,v 1.1 2005/01/19 20:59:01 jnekl Exp $
 *
 * This implements the tcl api.
 */

#include	<stdlib.h>
#include	<unistd.h>
#ifdef	_WIN32
#include	"winunistd.h"
#endif
#include	<string.h>
#include	<inttypes.h>
#include	<assert.h>
#include	<tcl.h>
#include	"ez8dbg.h"
#include	"xmalloc.h"
#include	"hexfile.h"

#define	MAX_MEMSIZE	0x10000

extern ez8dbg *ez8;
static uint8_t *buff = NULL;

enum tcl_cmds { dbg_null, dbg_rd_id, dbg_run, 
    dbg_ld_hexfile, dbg_rd_hexfile, dbg_wr_hexfile,
    dbg_reset_chip, dbg_reset_link, dbg_rd_pc, dbg_wr_pc, 
    dbg_rd_reg, dbg_wr_reg, dbg_rd_regs, dbg_wr_regs, 
    dbg_rd_mem, dbg_wr_mem, dbg_prog_mem, dbg_erase_mem };

/* execute command */

int dbg_cmd(ClientData clientData,
	Tcl_Interp *interp,
	int objc,
	Tcl_Obj *CONST objv[])
{
	switch((int)clientData) {
	case dbg_null:
		abort();
		break;
	case dbg_reset_chip: {
		if(objc != 1) {
			Tcl_WrongNumArgs(interp, 1, objv, NULL);
			return TCL_ERROR;
		}
		ez8->reset_chip();
		break;
	}
	case dbg_reset_link: {
		if(objc != 1) {
			Tcl_WrongNumArgs(interp, 1, objv, NULL);
			return TCL_ERROR;
		}
		ez8->reset_chip();
		break;
	}
	case dbg_rd_id: {
		Tcl_Obj *obj;
		uint16_t id;

		if(objc != 1) {
			Tcl_WrongNumArgs(interp, 1, objv, NULL);
			return TCL_ERROR;
		}
		id = ez8->rd_dbgrev();
		obj = Tcl_NewIntObj(id);
		Tcl_SetObjResult(interp, obj);
		break;
	}
	case dbg_run: {
		if(objc != 1) {
			Tcl_WrongNumArgs(interp, 1, objv, NULL);
			return TCL_ERROR;
		}
		ez8->run();
		while(ez8->isrunning()) {
			usleep(250000);
		}
		break;
	}
	case dbg_ld_hexfile: {
		int status;
		char *filename;

		if(objc != 2) {
			Tcl_WrongNumArgs(interp, 1, objv, "filename");
			return TCL_ERROR;
		}

		/* get filename */
		filename = Tcl_GetString(objv[1]);

		/* allocate buffer if not done already */
		buff = (uint8_t *)xmalloc(MAX_MEMSIZE);

		/* clear buffer */
		memset(buff, 0xff, MAX_MEMSIZE);

		/* read hexfile */
		status = rd_hexfile(buff, MAX_MEMSIZE, filename);
		if(status) {
			Tcl_SetResult(interp, "Error reading hexfile", NULL);
			return TCL_ERROR;
		}

		/* erase part */	
		ez8->flash_mass_erase();
		if(ez8->state(ez8->state_protected)) {
			ez8->reset_chip();
		}

		/* program memory */
		ez8->wr_mem(0x0000, buff, MAX_MEMSIZE);
		ez8->reset_chip(); 
		break;
	}
	case dbg_rd_pc: {
		Tcl_Obj *obj;
		uint16_t pc;

		if(objc != 1) {
			Tcl_WrongNumArgs(interp, 1, objv, NULL);
			return TCL_ERROR;
		}
		pc = ez8->rd_pc();
		obj = Tcl_NewIntObj(pc);
		Tcl_SetObjResult(interp, obj);
		break;
	}
	case dbg_wr_pc: {
		int pc, status;

		if(objc != 2) {
			Tcl_WrongNumArgs(interp, 1, objv, "pc");
			return TCL_ERROR;
		}
		status = Tcl_GetIntFromObj(interp, objv[1], &pc);
		if(status != TCL_OK) {
			return status;
		}
		if(pc > 0xffff || pc < 0) {
			Tcl_SetResult(interp, "Invalid value", NULL);
			return TCL_ERROR;
		}
		ez8->wr_pc(pc);
		break;
	}
	case dbg_rd_reg: {
		int addr, status;
		uint8_t data[1];
		Tcl_Obj *obj;

		if(objc != 2) {
			Tcl_WrongNumArgs(interp, 1, objv, "address");
			return TCL_ERROR;
		}
		/* get address */
		status = Tcl_GetIntFromObj(interp, objv[1], &addr);
		if(status != TCL_OK) {
			return status;
		}
		if(addr >= 0x1000 || addr < 0) {
			Tcl_SetResult(interp, "Invalid address", NULL);
			return TCL_ERROR;
		}

		/* read data */
		ez8->rd_regs(addr, data, 1);

		/* return data */
		obj = Tcl_NewIntObj(*data);
		Tcl_SetObjResult(interp, obj);
		break;
	}
	case dbg_wr_reg: {
		int addr, value, status;
		uint8_t data[1];

		if(objc != 3) {
			Tcl_WrongNumArgs(interp, 1, objv, "address data");
			return TCL_ERROR;
		}
		/* get address */
		status = Tcl_GetIntFromObj(interp, objv[1], &addr);
		if(status != TCL_OK) {
			return status;
		}
		if(addr >= 0x1000 || addr < 0) {
			Tcl_SetResult(interp, "Invalid address", NULL);
			return TCL_ERROR;
		}
		/* get data */
		status = Tcl_GetIntFromObj(interp, objv[2], &value);
		if(status != TCL_OK) {
			return status;
		}
		if(value > 255 || value < -128) {
			Tcl_SetResult(interp, "Invalid data", NULL);
			return TCL_ERROR;
		}

		/* write register */
		*data = (uint8_t)value;
		ez8->wr_regs(addr, data, 1);
		break;
	}
	case dbg_rd_regs: {
		int addr, size, status;
		Tcl_Obj *obj;

		if(objc != 3) {
			Tcl_WrongNumArgs(interp, 1, objv, "address size");
			return TCL_ERROR;
		}
		/* get address */
		status = Tcl_GetIntFromObj(interp, objv[1], &addr);
		if(status != TCL_OK) {
			return status;
		}
		if(addr >= 0x1000 || addr < 0) {
			Tcl_SetResult(interp, "Invalid address", NULL);
			return TCL_ERROR;
		}

		/* get size */
		status = Tcl_GetIntFromObj(interp, objv[2], &size);
		if(status != TCL_OK) {
			return status;
		}
		if(addr+size > 0x1000 || size <= 0) {
			Tcl_SetResult(interp, "Invalid size", NULL);
			return TCL_ERROR;
		}

		/* read data */
		ez8->rd_regs(addr, buff, size);

		/* return data */
		obj = Tcl_NewByteArrayObj(buff, size);
		Tcl_SetObjResult(interp, obj);
		break;
	}
	case dbg_wr_regs: {
		int status, addr, size;
		uint8_t *data;

		if(objc != 3) {
			Tcl_WrongNumArgs(interp, 1, objv, "address data");
			return TCL_ERROR;
		}
		/* get address */
		status = Tcl_GetIntFromObj(interp, objv[1], &addr);
		if(status != TCL_OK) {
			return status;
		}
		if(addr >= 0x1000 || addr < 0) {
			Tcl_SetResult(interp, "Invalid address", NULL);
			return TCL_ERROR;
		}
		/* get data */
		data = Tcl_GetByteArrayFromObj(objv[2], &size);
		if(addr+size > 0x1000) {
			Tcl_SetResult(interp, "Invalid size", NULL);
			return TCL_ERROR;
		}

		/* write register */
		ez8->wr_regs(addr, data, size);
		break;
	}
	case dbg_rd_mem: {
		int addr, size, status;
		Tcl_Obj *obj;

		if(objc != 3) {
			Tcl_WrongNumArgs(interp, 1, objv, "address size");
			return TCL_ERROR;
		}
		/* get address */
		status = Tcl_GetIntFromObj(interp, objv[1], &addr);
		if(status != TCL_OK) {
			return status;
		}
		if(addr >= 0x10000 || addr < 0) {
			Tcl_SetResult(interp, "Invalid address", NULL);
			return TCL_ERROR;
		}

		/* get size */
		status = Tcl_GetIntFromObj(interp, objv[2], &size);
		if(status != TCL_OK) {
			return status;
		}
		if(addr+size > 0x10000 || size <= 0) {
			Tcl_SetResult(interp, "Invalid size", NULL);
			return TCL_ERROR;
		}

		/* read data */
		ez8->rd_mem(addr, buff, size);

		/* return data */
		obj = Tcl_NewByteArrayObj(buff, size);
		Tcl_SetObjResult(interp, obj);
		break;
	}
	case dbg_wr_mem: {
		int status, addr, size;
		uint8_t *data;

		if(objc != 3) {
			Tcl_WrongNumArgs(interp, 1, objv, "address data");
			return TCL_ERROR;
		}
		/* get address */
		status = Tcl_GetIntFromObj(interp, objv[1], &addr);
		if(status != TCL_OK) {
			return status;
		}
		if(addr >= 0x10000 || addr < 0) {
			Tcl_SetResult(interp, "Invalid address", NULL);
			return TCL_ERROR;
		}
		/* get data */
		data = Tcl_GetByteArrayFromObj(objv[2], &size);
		if(addr+size > 0x10000) {
			Tcl_SetResult(interp, "Invalid size", NULL);
			return TCL_ERROR;
		}

		/* write register */
		ez8->wr_mem(addr, data, size);
		break;
	}
	default:
		printf("Command %s not implemented\n", 
		    Tcl_GetString(objv[0]));
		break;
	}

	return TCL_OK;
}

/* execute tcl command by passing to dbg_cmd */

int tcl_cmd(ClientData clientData,
	Tcl_Interp *interp,
	int objc,
	Tcl_Obj *CONST objv[])
{
	int status;

	try {
		status = dbg_cmd(clientData, interp, objc, objv);
	} catch(char *err) {
		Tcl_SetResult(interp, err, NULL);
		return TCL_ERROR;	
	}

	return status;
}

/* initialize ez8 tcl api commands */
int Tcl_AppInit(Tcl_Interp *interp)
{
/*
        int status;
        status = Tcl_Init(interp);
        if(status != TCL_OK) {
                return status;
        }
*/
        Tcl_CreateObjCommand(interp, "dbg_rd_id", tcl_cmd, 
	    (void *)dbg_rd_id, NULL);
        Tcl_CreateObjCommand(interp, "dbg_run", tcl_cmd, 
	    (void *)dbg_run, NULL);
        Tcl_CreateObjCommand(interp, "dbg_ld_hexfile", tcl_cmd, 
	    (void *)dbg_ld_hexfile, NULL);
        Tcl_CreateObjCommand(interp, "dbg_reset_chip", tcl_cmd, 
	    (void *)dbg_reset_chip, NULL);
        Tcl_CreateObjCommand(interp, "dbg_reset_link", tcl_cmd, 
	    (void *)dbg_reset_link, NULL);
        Tcl_CreateObjCommand(interp, "dbg_rd_pc", tcl_cmd, 
	    (void *)dbg_rd_pc, NULL);
        Tcl_CreateObjCommand(interp, "dbg_wr_pc", tcl_cmd, 
	    (void *)dbg_wr_pc, NULL);
        Tcl_CreateObjCommand(interp, "dbg_rd_reg", tcl_cmd, 
	    (void *)dbg_rd_reg, NULL);
        Tcl_CreateObjCommand(interp, "dbg_wr_reg", tcl_cmd, 
	    (void *)dbg_wr_reg, NULL);
        Tcl_CreateObjCommand(interp, "dbg_rd_regs", tcl_cmd, 
	    (void *)dbg_rd_regs, NULL);
        Tcl_CreateObjCommand(interp, "dbg_wr_regs", tcl_cmd, 
	    (void *)dbg_wr_regs, NULL);
        Tcl_CreateObjCommand(interp, "dbg_rd_mem", tcl_cmd, 
	    (void *)dbg_rd_mem, NULL);
        Tcl_CreateObjCommand(interp, "dbg_wr_mem", tcl_cmd, 
	    (void *)dbg_wr_mem, NULL);

        return TCL_OK;
}

/* invoke tcl interpreter */
int exec_tcl(char *script, int argc, char **argv)
{
	char **args;
	int len;

	len = 2+argc-optind;
	args = (char **)xmalloc(sizeof(char *) * (len+1));
	args[0] = argv[0];
	args[1] = script;
	memcpy(args+2, argv+optind, (len-2+1)*sizeof(char *));

	if(!buff) {
		buff = (uint8_t *)xmalloc(MAX_MEMSIZE);
	}

	Tcl_Main(len, args, Tcl_AppInit);

	return 0;
}

