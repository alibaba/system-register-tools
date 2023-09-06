/*
 * rdasr.c
 *
 * Utility to read data from AArch64 system register.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "version.h"
#include "sysreg.h"

struct option long_options[] = {
	{"help",				0, 0, 'h'},
	{"version",				0, 0, 'V'},
	{"capital-hexadecimal", 0, 0, 'X'},
	{"unsigned-decimal",	0, 0, 'd'},
	{"octal",				0, 0, 'o'},
	{"zero-fill",			0, 0, '0'},
	{"zero-pad",			0, 0, '0'},
	{"register",			0, 0, 'r'},
	{"processor",			1, 0, 'p'},
	{"bitfield",			1, 0, 'f'},
	{"test",				0, 0, 't'},
	{0, 0, 0, 0}
};

const char *program;
struct register_t *global_register;
char *TABLEPATH; /* the path for register table */
char *TABLENAME = ".regstable";
struct register_t reg_table[MAX_REG];

void usage(void)
{
	fprintf(stderr,
	  "Usage: %s [options] register\n"
	  "  --help         -h  Print this help\n"
	  "  --version      -V  Print current version\n"
	  "  --capital-hex  -X  Hexadecimal output (upper case)\n"
	  "  --decimal      -d  Unsigned decimal output\n"
	  "  --octal        -o  Octal output\n"
	  "  --zero-pad     -0  Output leading zeroes\n"
	  "  --processor #  -p  Select processor number (default 0)\n"
	  "  --bitfield h:l -f  Output bits [h:l] only\n"
	  "  --register #   -r  Specify register name\n"
	  "  --test #       -t  test all register in register table file\n"
	  "\n"
	  "  example:\n"
	  "  Read data from the specify register:\n"
	  "\n"
	  "		$rdasr -p0 -r MPIDR_EL1\n"
	  "		0x80000002\n"
	  "\n"
	  "  Write data to the specify register:\n"
	  "\n"
	  "		$wrasr -p0 -r MPIDR_EL1 <data>\n"
	  "		0x80000006\n",
	  program);
}

/*
 * traverse regs.table file and rearch for destination register
 */
int search(void)
{
	char name[20], sys_name[20];
	int op0, op1, cn, cm, op2;
	unsigned int len = 0;
	FILE *fp;

	if ((fp = fopen(TABLEPATH, "r")) == NULL) {
		printf("%s: No such file or can not read!\n", TABLEPATH);
		return -1;
	}

	while (!feof(fp)) {
		fscanf(fp, "%s %s", name, sys_name);
		if (strcmp(name, global_register->name) == 0) {
			/* find destination register */
			len = strlen(sys_name);
			op0 = atoi(&sys_name[1]);
			op1 = atoi(&sys_name[3]);
			op2 = atoi(&sys_name[len - 1]);
			cn = atoi(&sys_name[6]);
			cm = (sys_name[len-4] == 'c') ? atoi(&sys_name[len-3]):
				(atoi(&sys_name[len-4]));
			global_register->regcode = sys_reg(op0, op1, cn, cm, op2);
			break;
		}
	}

	fclose(fp);
	if (strcmp(name, global_register->name) != 0) {
		/* Not find destination register */
		printf("No target register found in %s!\n", TABLEPATH);
		fprintf(stderr,
			"Note: If you don't care about the system downtime,\
			you can directly enter the register name in the form\
			of S<op0>_op1_cn_cm_op2 !\n");
		exit(127);
	}

	return 0;
}

void islegal(struct register_t *regs)
{
	int op0, op1, cn, cm, op2;

	op0 = sys_reg_Op0(regs->regcode);
	op1 = sys_reg_Op1(regs->regcode);
	cn = sys_reg_CRn(regs->regcode);
	cm = sys_reg_CRm(regs->regcode);
	op2 = sys_reg_Op2(regs->regcode);

	if ((op0 <= MAX_OP0) && (op1 <= MAX_OP1)
		&& (op2 <= MAX_OP2) && (cn <= MAX_CN) && (cm <= MAX_CM)) {
		/* legal */
		return;
	}
	else {
		/* illegal */
		perror("register name error\n");
		exit(127);
	}
}

/*
 * Test all registers in regs.table file.
 */
int test_all(void)
{
	int i = 0;
	int count = 0, stat_undef = 0, stat_succ = 0;
	int fd;
	char name[20], sys_name[20];
	uint64_t data;
	int op0, op1, cn, cm, op2;
	unsigned int len = 0;
	FILE *fp;

	if ((fp = fopen(TABLEPATH, "r")) == NULL) {
		printf("%s: No such file or can not read!\n", TABLEPATH);
		return -1;
	}

	while (!feof(fp)) {
		fscanf(fp, "%s %s", name, sys_name);
		/* find destination register */
		len = strlen(sys_name);
		op0 = atoi(&sys_name[1]);
		op1 = atoi(&sys_name[3]);
		op2 = atoi(&sys_name[len - 1]);
		cn = atoi(&sys_name[6]);
		cm = (sys_name[len-4] == 'c') ? atoi(&sys_name[len-3]):
			(atoi(&sys_name[len-4]));

		reg_table[i].regcode = sys_reg(op0, op1, cn, cm, op2);
		reg_table[i].name = strdup(name);
		i++;
		count++;
	}
	fclose(fp);

	fd = open("/dev/cpu/0/msr", O_RDWR);
	if (fd < 0) {
		if (errno == ENXIO) {
			fprintf(stderr, "rdasr: No CPU 0\n");
			fprintf(stderr, "please enter CPU number: 0~%d\n",
					get_nprocs()-1);
			exit(2);
		}
		else if (errno == EIO) {
			fprintf(stderr, "rdasr: CPU 0 doesn't support MSRs\n");
			exit(3);
		}
		else {
			perror("rdasr:open");
			fprintf(stderr, "please enter CPU number: 0~%d\n",
					get_nprocs()-1);
			exit(127);
		}
	}

	/* read */
	for (i=0; i< count; i++) {
		printf("%3d: %-20s: ", i, reg_table[i].name);
		if (pread(fd, &data, sizeof data, reg_table[i].regcode)
				!= sizeof(data)) {
			stat_undef++;
			printf("UNDEFINED or unreadable!\n");
			continue;
		}
		stat_succ++;
		printf("0x%lx\n", data);
	}

	printf("\nThe test ended and no system exception occurred!\n");
	printf("Undefined or unreadable registers: %d\n", stat_undef);
	printf("Readable registers: %d\n", stat_succ);
	free(TABLEPATH);
	close(fd);
	return 0;
}

/*
 * This func will parse the register name, and getting op0, op1, cn, cm
 * and op2.
 */
void parse(const char *regname)
{
	int status;
	int op0, op1, cn, cm, op2;
	unsigned int len = strlen(regname);

	global_register = (struct register_t *)malloc(sizeof(struct register_t));
	global_register->length = len;
	global_register->name = strdup(regname);

	/* check the format of register name */
	if ((regname[0] == 'S') && (regname[5] == 'c')) {
		/* for Sx_op1_cn_cm_op2 */
		global_register->type = 0;
		op0 = atoi(&regname[1]);
		op1 = atoi(&regname[3]);
		op2 = atoi(&regname[len - 1]);
		cn = atoi(&regname[6]);
		cm = (regname[len-4] == 'c') ? atoi(&regname[len-3]):
			(atoi(&regname[len-4]));
		global_register->regcode = sys_reg(op0, op1, cn, cm, op2);
	}
	else {
		/* similar to MPIDR_EL1 */
		global_register->type = 1;
		//TABLEPATH = getlogin();
		status = search();
		if (status == -1) {
			/* No target register found in regs.table */
			exit(127);
		}
	}

	islegal(global_register);
	printf("register: %s\n", global_register->name);
}

void init(void)
{
	char *home = getenv("HOME");
	TABLEPATH = (char *)malloc(strlen(home) + strlen(TABLENAME) + 1);
	sprintf(TABLEPATH, "%s/%s", home, TABLENAME);
}

int main(int argc, char *argv[])
{
	uint64_t data;
	int c, fd;
	int mode = OF_HEX | OF_C;
	int cpu = 0;
	unsigned int highbit = 63, lowbit = 0;
	unsigned long arg;
	char *endarg;
	char msr_file_name[64];

	program = argv[0];
	/* check the usage of program */
	if (argc < 2) {
		usage();
		exit(127);
	}
	init();
	/* delete r */
	while ((c = getopt_long(argc, argv, "htVXdou0p:f:r:", long_options,
					NULL)) != -1 ) {
		switch (c) {
		case 'h':
			usage();
			exit(0);
		case 'V':
			fprintf(stderr,
				"%s: %s\n", program, VERSION_STRING);
			exit(0);
		case 'X':
			mode = (mode & ~OF_MASK) | OF_CHX;
			break;
		case 'o':
			mode = (mode & ~OF_MASK) | OF_OCT;
			break;
		case 'r':
			/* parse the register name */
			if (strlen(optarg) == 0) {
				usage();
				exit(127);
			}
			parse(optarg);
			break;
		case 'd':
			mode = (mode & ~OF_MASK) | OF_DEC;
			break;
		case '0':
			mode |= OF_FILL;
			break;
		case 'p':
			arg = strtoul(optarg, &endarg, 0);
			if (*endarg || arg > 255) {
				usage();
				exit(127);
			}
			cpu = (int)arg;
			break;
		case 'f':
		{
			/* ONLY show specific bits. */
			if (sscanf(optarg, "%u:%u", &highbit, &lowbit) != 2 ||
				highbit > 63 || lowbit > highbit) {
				fprintf(stderr, "-f: invalid highbit or lowbit\n");
				usage();
				exit(127);
			}
		}
			break;
		case 't':
			test_all();
			exit(0);
		default:
			usage();
			exit(127);
		}
	}

	if ((optind != argc) && (argc < 3)) {
		/* Should have exactly one argument */
		usage();
		exit(127);
	}

	sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_RDWR);
	if (fd < 0) {
		if (errno == ENXIO) {
			fprintf(stderr, "rdasr: No CPU %d\n", cpu);
			fprintf(stderr, "please enter CPU number: 0~%d\n",
					get_nprocs()-1);
			exit(2);
		}
		else if (errno == EIO) {
			fprintf(stderr,
				"rdasr: CPU %d doesn't support MSRs\n", cpu);
			exit(3);
		}
		else {
			perror("rdasr:open");
			fprintf(stderr, "please enter CPU number: 0~%d\n",
					get_nprocs()-1);
			exit(127);
		}
	}
	/* read */
	aarch64_read_register(fd, &data, sizeof data, global_register->regcode);
	aarch64_printf(mode, data, highbit, lowbit);

	free(global_register);
	free(TABLEPATH);
	close(fd);
	exit(0);
}
