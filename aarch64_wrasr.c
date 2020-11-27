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

void aarch64_write_register(int fd, uint64_t data, int size, uint32_t reg);

/* support aarch64 */
void aarch64_write_register(int fd, uint64_t data, int size, uint32_t reg)
{
	if ( pwrite(fd, &data, sizeof(data), reg) != sizeof(data) ) {
		perror("wrasr:pwrite");
		exit(127);
	}
}
