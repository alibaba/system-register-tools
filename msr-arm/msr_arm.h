#ifndef __MSR_ARM_H__
#define __MSR_ARM_H__

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/smp.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <asm/cpufeature.h>
#include <asm/cpu.h>
#include <asm/sysreg.h>
#include <asm/insn.h>
#include <asm/fixmap.h>
#include <asm/traps.h>
#include <asm/atomic.h>
#include <linux/printk.h>
#include <linux/version.h>

/* the opcode of mrs/msr/sys instruction */
#define AARCH64_MRS_INSN (0xd5200000)
#define AARCH64_MSR_INSN (0xd5000000)
#define AARCH64_SYS_INSN (0xd5080000)
#define INSN_REG_MASK (0x1f)

#define PSR_MODE_MASK_ALL (0x0)
#define PSR_MODE_ALL_EL (0x0)

/*
 * the max number of each code
 * in system registers
 */
#define MAX_OP0 3
#define MAX_OP1 7
#define MAX_OP2 7
#define MAX_CN 15
#define MAX_CM 15

extern atomic_t msr_flags;
extern atomic_t mrs_flags;

struct aarch64_insn_patch {
	void		**text_addrs;
	u32		*new_insns;
	int		insn_cnt;
	atomic_t	cpu_count;
};

struct msr {
	union {
		struct {
			u32 l;
			u32 h;
		};
		u64 q;
	};
};

struct msr_info {
	u32 msr_no;
	u32 opt;
	struct msr reg;
	struct msr *msrs;
	int err;
};


int aarch64_insn_patch_text_smc(void *addrs[], u32 insns[], int cnt);
int aarch64_modify_read_text(u32 opcode);
int aarch64_modify_write_text(u32 opcode);

u32 *get_read_insn_addr(void);
u32 *get_write_insn_addr(void);

int rdmsr_safe_on_cpu_aarch64(unsigned int cpu, u32 opt, u32 *l, u32 *h);
int wrmsr_safe_on_cpu_aarch64(unsigned int cpu, u32 opt, u32 l, u32 h);

/*
 * In ARMv8-A, A64 instructions have a fixed length of 32 bits and are
 * always little-endian.
 */
int aarch64_insn_read_smc(void *addr, u32 *insnp);

u32 aarch64_insn_mrs_gen(u32 op0, u32 op1, u32 crn,
	u32 crm, u32 op2, u32 rt);

u32 aarch64_insn_msr_gen(u32 op0, u32 op1, u32 crn,
	u32 crm, u32 op2, u32 rt);

u32 aarch64_insn_other_gen(u32 op0, u32 op1, u32 crn,
	u32 crm, u32 op2, u32 rt);

int aarch64_register_check(u32 regcode);

void register_undef_hook_el1(struct undef_hook *hook);
void unregister_undef_hook_el1(struct undef_hook *hook);

/* search for the specify function pointer from kernel space */
unsigned long get_func_from_kallsyms(char *func);

#endif

