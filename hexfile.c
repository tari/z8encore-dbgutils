/* Copyright (C) 2002, 2003, 2004 Zilog, Inc.
 *
 * $Id: hexfile.c,v 1.1 2004/08/03 14:23:48 jnekl Exp $
 *
 * These functions are used to read and write Intel hexfiles.
 */

#define		_GNU_SOURCE

#include	<stdio.h>
#include	<stdlib.h>
#include	<inttypes.h>
#include	<ctype.h>
#include	<string.h>
#include	<assert.h>
#include	"xmalloc.h"

#include	"hexfile.h"

/**************************************************************
 * This function will read an intel hexfile into the memory 
 * pointed to by *buff.
 */

int rd_hexfile(uint8_t *buff, size_t buffsize, const char *filename)
{
	int c, i, line;
	FILE *file;
	char *readbuff;
	uint8_t size, dri, type, *data, checksum;
	uint16_t drlo, sba, lba;
	uint32_t address;
	uint8_t *ptr1, *ptr2, *end, fill;

	sba = lba = 0;

	assert(buff != NULL);
	assert(filename != NULL);

	fill = *buff;

	for(i=0; i<buffsize; i++) {
		if(buff[i] != fill) {
			/* buffer not initialized */
			abort();
		}
	}

	file = fopen(filename, "rb");
	if(file == NULL) {
		perror("fopen");
		return -1;
	}

	line = 0;

	readbuff = (char *)xmalloc(BUFSIZ);

	while(fgets(readbuff, BUFSIZ, file)) {
		line++;
		data = (uint8_t *)strtok(readbuff, " \t\r\n");
		if(data == NULL) {
			continue;
		}
		if(*data == '\0') {
			continue;
		}
		if(*data++ != ':') {
			fprintf(stderr, "%s:%d hexfile corrupt\n", 
			    filename, line);
			fclose(file);
			free(readbuff);
			return -1;
		}
		/* remove ascii bias */
		end = data;
		while(*end != '\0') {
			c = *end;
			if(c >= '0' && c <= '9') {
				*end++ = c - '0';
			} else if(c >= 'A' && c <= 'F') {
				*end++ = c - 'A' + 10;
			} else if(c >= 'a' && c <= 'f') {
				*end++ = c - 'a' + 10;
			} else {
				fprintf(stderr, "Corrupt hexfile, line %d\n", 
				    line);
				fclose(file);
				free(readbuff);
				return -1;
			}
		}
		/* should be even number of nibbles */
		if((end - data) % 2) {
			fprintf(stderr, "%s:%d hexfile corrupt\n", 
			    filename, line);
			fclose(file);
			free(readbuff);
			return -1;
		}
		if((end-data) < sizeof("SSAAAATTCC") - 1) {
			fprintf(stderr, "%s:%d hexfile corrput\n", 
			    filename, line);
			fclose(file);
			free(readbuff);
			return -1;
		}
			
		/* pack nibbles into bytes */
		ptr1 = ptr2 = data;
		while(ptr2 != end) {
			*ptr1 = *ptr2++ << 4;
			*ptr1++ |= *ptr2++;
		}
		/* verify checksum */
		checksum = 0;
		ptr2 = data;
		while(ptr2 < ptr1) {
			checksum += *ptr2++;
		}
		if(checksum != 0x00) {
			fprintf(stderr, "%s:%d hexfile corrupt\n", 
			    filename, line);
			fclose(file);
			free(readbuff);
			return -1;
		}

		size = *data++;
		drlo = *data++ << 8;
		drlo |= *data++;
		type = *data++;

		if(size != ptr1 - data - 1) {
			fprintf(stderr, "%s:%d hexfile corrupt", 
			    filename, line);
			fclose(file);
			free(readbuff);
			return -1;
		}

		switch(type) {
		case 0x00:	/* data record */
			for(dri=0; dri<size; dri++) {
				address = sba ?
					(sba<<4)+((drlo+dri)%0x10000) :
				        (((lba<<16)|drlo)+dri)&0xffffffff;
				if(address >= buffsize) {
					fprintf(stderr, "%s:%d memory out "
					    "of range\n", filename, line);
					fclose(file);
					free(readbuff);
					return -1;
				}
				if(buff[address] != fill) {
					fprintf(stderr, "%s:%d overlapping "
					    "data, addr %04x\n", filename, 
					    line, address);
					fclose(file);
					free(readbuff);
					return -1;
				}
				buff[address] = data[dri];
			}
			break;
		case 0x01:	/* end-of-file record */
			if(drlo != 0x0000 || size != 0x00) {
				fprintf(stderr, "%s:%d hexfile corrupt\n", 
				    filename, line);
				fclose(file);
				free(readbuff);
				return -1;
			}
			return 0;
		case 0x02:	/* extended segment address record */
			if(drlo != 0x0000 || size != 0x02) {
				fprintf(stderr, "%s:%d hexfile corrupt\n", 
				    filename, line);
				fclose(file);
				free(readbuff);
				return -1;
			}
			sba = drlo;
			lba = 0;
			break;
		case 0x03:	/* start segment address record */
			if(drlo != 0x0000 || size != 0x04) {
				fprintf(stderr, "%s:%d hexfile corrupt\n", 
				    filename, line);
				fclose(file);
				free(readbuff);
				return -1;
			}
			break;
		case 0x04:	/* extended linear address record */
			if(drlo != 0x0000 || size != 0x02) {
				fprintf(stderr, "%s:%d hexfile corrupt\n",
				    filename, line);
				fclose(file);
				free(readbuff);
				return -1;
			}
			lba = drlo;
			sba = 0;
			break;
		case 0x05:	/* start linear address record */
			if(drlo != 0x0000 || size != 0x04) {
				fprintf(stderr, "%s:%d hexfile corrupt\n", 
				    filename, line);
				fclose(file);
				free(readbuff);
				return -1;
			}
			break;
		default:
			fprintf(stderr, "%s:%d hexfile corrupt\n", 
			    filename, line);
			fclose(file);
			free(readbuff);
			return -1;
		}
	}

	fclose(file);	
	free(readbuff);

	return 0;
}

/**************************************************************
 * This will save the data in *buff a file in intel hexfile 
 * format.
 */

int wr_hexfile(uint8_t *buff, size_t buffsize, size_t offset, 
    const char *filename)
{
	FILE *file;
	uint8_t size, dri, type, checksum;
	uint16_t drlo, lba;

	lba = 0;

	assert(buff != NULL);
	assert(filename != NULL);

	file = fopen(filename, "wb");
	if(file == NULL) {
		perror("fopen");
		return -1;
	}

	while(buffsize > 0) {
		if((offset >> 16) != lba) {
			size = 2;
			drlo = 0x0000;
			type = 0x04;
			lba = offset >> 16;
			checksum = -(size + (drlo >> 8) + (drlo & 0xff) 
			    + type + (lba>>8) + (lba & 0xff));
			fprintf(file, ":%02X%04X%02X%04X%02X\n", 
			    size, drlo, type, lba, checksum);
			continue;	
		}

		size = offset % 16 ? offset % 16 : 16;
		size = buffsize < size ? buffsize : size;
		drlo = offset & 0xffff;
		type = 0x00;
		checksum = size + (drlo >> 8) + (drlo & 0xff) + type;
		fprintf(file, ":%02X%04X%02X", size, drlo, type);
		for(dri = 0; dri < size; dri++) {
			fprintf(file, "%02X", buff[dri]);
			checksum += buff[dri];
		}
		checksum = -checksum;
		fprintf(file, "%02X\n", checksum);
		offset += size;
		buff += size;
		buffsize -= size;
	}

	size = 0;
	drlo = 0x0000;
	type = 0x01;
	checksum = -(size + (drlo >> 8) + (drlo & 0xff) + type);
	fprintf(file, ":%02X%04X%02X%02X\n", size, drlo, type, checksum);
	fclose(file);

	return 0;
}

/**************************************************************/

