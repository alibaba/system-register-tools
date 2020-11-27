#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include "version.h"
#include "sysreg.h"

extern const char *program;
void aarch64_read_register(int fd, uint64_t *data, int size, uint32_t reg);
void aarch64_printf(int mode, uint64_t data, unsigned int highbit,
		unsigned int lowbit);

/* support aarch64 */
void aarch64_read_register(int fd, uint64_t *data, int size, uint32_t reg)
{
	uint64_t data_read;

	if (pread(fd, &data_read, sizeof data_read, reg) != sizeof(data_read)) {
		perror("rdasr:pread");
		fprintf(stderr,
			"This register is UNDEFINED or unreadable currently!\n");
		exit(127);
	}
	*data = data_read;
}

void aarch64_printf(int mode, uint64_t data,unsigned int highbit,
		unsigned int lowbit)
{
	unsigned int bits;
	int width;
	char *pat;

	bits = highbit-lowbit+1;
	if (bits < 64) {
		/* Show only part of register */
		data >>= lowbit;
		data &= (1ULL << bits)-1;
	}

	pat = NULL;
	width = 1; /* Default */

	switch(mode) {
	case OF_HEX:
		pat = "0x%*llx\n";
		break;
	case OF_CHX:
		pat = "%*llX\n";
		break;
	case OF_DEC:
		pat = "%*llu\n";
		break;
	case OF_OCT:
		pat = "%*llo\n";
		break;
	case OF_HEX | OF_C:
		pat = "0x%*llx\n";
		break;
	case OF_CHX | OF_C:
		pat = "0x%*llX\n";
		break;
	case OF_OCT | OF_C:
		pat = "0%*llo\n";
		break;
	case OF_DEC | OF_C:
	case OF_DEC | OF_FILL:
	case OF_DEC | OF_FILL | OF_C:
		pat = "%*lluU\n";
		break;
	case OF_HEX | OF_FILL:
		pat = "0x%0*llx\n";
		width = (bits+3) / 4;
		break;
	case OF_CHX | OF_FILL:
		pat = "%0*llX\n";
		width = (bits+3) / 4;
		break;
	case OF_OCT | OF_FILL:
		pat = "%0*llo\n";
		width = (bits+2) / 3;
		break;
	case OF_HEX | OF_FILL | OF_C:
		pat = "0x%0*llx\n";
		width = (bits+3) / 4;
		break;
	case OF_CHX | OF_FILL | OF_C:
		pat = "0x%0*llX\n";
		width = (bits+3) / 4;
		break;
	case OF_OCT | OF_FILL | OF_C:
		pat = "0%0*llo\n";
		width = (bits+2) / 3;
		break;
	case OF_RAW:
	case OF_RAW | OF_FILL:
		fwrite(&data, sizeof(data), 1, stdout);
		break;
	case OF_RAW | OF_C:
	case OF_RAW | OF_FILL | OF_C:
		{
			unsigned char *p = (unsigned char *)&data;
			int i;
			for ( i = 0 ; i < sizeof data ; i++ ) {
				printf("%s0x%02x", i?",":"{", (unsigned int)(*p++));
			}
			printf("}\n");
		}
		break;
	default:
		fprintf(stderr,
			"%s: Impossible case, line %d\n", program, __LINE__);
		exit(127);
	}

	if (width < 1) {
		width = 1;
	}

	if (pat) {
		printf(pat, width, data);
	}
}

