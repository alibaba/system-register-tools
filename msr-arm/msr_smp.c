// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/completion.h>
#include "msr_arm.h"

static DEFINE_RAW_SPINLOCK(msr_lock);
/* record the address of reading or writing */
static u32 *rd_tp;
static u32 *wr_tp;

atomic_t msr_flags = ATOMIC_INIT(0);
EXPORT_SYMBOL(msr_flags);
atomic_t mrs_flags = ATOMIC_INIT(0);
EXPORT_SYMBOL(mrs_flags);

struct msr_info_completion {
	struct msr_info		msr;
	struct completion	done;
};

/* kernel function which not export */
int aarch64_insn_patch_text_smc(void *addrs[], u32 insns[], int cnt)
{
	/* kernel function which not export */
	int (*_aarch64_insn_patch_text)(void *addrs[], u32 insns[], int cnt);
	_aarch64_insn_patch_text =
		(void *)get_func_from_kallsyms("aarch64_insn_patch_text");
	return _aarch64_insn_patch_text(addrs, insns, cnt);
}

/*
 * Self-modify code for label of read address.
 */
int aarch64_modify_read_text(u32 opcode)
{
	void *addrs[1];

	addrs[0] = rd_tp;
	/*
	 * call aarch64_insn_patch_text to modify
	 * the opcode
	 */
	return aarch64_insn_patch_text_smc(addrs, &opcode, 1);
}
EXPORT_SYMBOL(aarch64_modify_read_text);

/*
 * Self-modify code for label of write address.
 */
int aarch64_modify_write_text(u32 opcode)
{
	void *addrs[1];

	addrs[0] = wr_tp;
	/*
	 * call aarch64_insn_patch_text to modify
	 * the opcode
	 */
	return aarch64_insn_patch_text_smc(addrs, &opcode, 1);
}
EXPORT_SYMBOL(aarch64_modify_write_text);

/*
 * return a address of read or write label
 */
u32 *get_read_insn_addr()
{
	/* TODO: rd_tp on each cpu */
	return rd_tp;
}
EXPORT_SYMBOL(get_read_insn_addr);

u32 *get_write_insn_addr()
{
	return wr_tp;
}
EXPORT_SYMBOL(get_write_insn_addr);

/*
 * Read data from register
 *
 * At runtime, the "mrs xyz, xyz" instruction will be modified through rd_tp
 * address.
 */
static noinline int rdmsr_safe_aarch64(u32 opt, u32 *data0, u32 *data1)
{
	/* reg is encoded by op0,op1,cn... */
	u32 err = 0;
	unsigned long __val = 0;
	unsigned long __pc_addr = 0;

	barrier();
	raw_spin_lock(&msr_lock);
	atomic_add(1, &mrs_flags);
	/*
	 * On the first execution, opt=0, will NOT execute "mrs xyz, xyz"
	 * instruction and ONLY initializes rd_tp value.
	 * In addition, "mrs xyz, xyz" instruction will be modified before running.
	 */
	asm volatile("mov %3, 0\n\t"
			"cmp %4, 0\n\t"
			"b.eq 1f\n\t"
			"mrs %0, MIDR_EL1\n\t"
			"b 1f\n\t"
			"mov %3, 1\n\t" /* Execute only when an exception occurs */
			"1:adr %1, .\n\t"
			"sub %1, %1, 12\n\t"
			"mov %2, %1\n\t"
			: "=r"(__val), "=r"(__pc_addr), "=r"(rd_tp), "=&r"(err)
			: "r"(opt));

	atomic_sub(1, &mrs_flags);
	raw_spin_unlock(&msr_lock);

	if ((data0 == NULL) && (data1 == NULL)) {
		/* init read or write address in somewhere */
		return 0;
	}

	*data0 = __val;
	*data1 = __val >> 32;
	if (err == 1) {
		/* undef instruction occurred */
		goto rd_error;
	}
	/* be successful */
	return 0;
rd_error:
	return -1;
}

/*
 * Write data to register
 *
 * At runtime, the "msr xyz, xyz" instruction will be modified through wr_tp
 * address.
 */
static noinline int wrmsr_safe_aarch64(u32 opt, u32 data0, u32 data1)
{
	unsigned long __val = 0;
	unsigned long __pc_addr = 0;
	u64 data = 0;
	int err = 0;

	data = data1;
	data = (data << 32) | (data0);
	__val = data;
	barrier();
	raw_spin_lock(&msr_lock);
	atomic_add(1, &msr_flags);
	/*
	 * On the first execution, opt=0, will NOT execute "msr xyz, xyz"
	 * instruction and ONLY initializes wr_tp value.
	 * In addition, "msr xyz, xyz" instruction will be modified before running.
	 */
	asm volatile("mov %2, 0\n\t"
			"cmp %4, 0\n\t"
			"b.eq 1f\n\t"
			"msr TCR_EL1, %3\n\t"
			"b 1f\n\t"
			"mov %2, 1\n\t" /* exec when exception occurred */
			"1:adr %0, .\n\t"
			"sub %0, %0, 12\n\t"
			"mov %1, %0\n\t"
			: "=r"(__pc_addr), "=r"(wr_tp), "=&r"(err)
			: "rZ"(__val), "r"(opt));
	atomic_sub(1, &msr_flags);
	raw_spin_unlock(&msr_lock);
	if (err == 1) {
		/* undef instruction occurred */
		goto wr_error;
	}
	/* be successful */
	return 0;
wr_error:
	return -1;
}

/*
 * These "safe" variants are slower and should be used when the target MSR
 * may not actually exist.
 */
static void __rdmsr_safe_on_cpu_aarch64(void *info)
{
	struct msr_info_completion *rv = info;

	rv->msr.err = rdmsr_safe_aarch64(rv->msr.opt, &rv->msr.reg.l,
			&rv->msr.reg.h);
	complete(&rv->done);
}

static void __wrmsr_safe_on_cpu_aarch64(void *info)
{
	struct msr_info *rv = info;

	rv->err = wrmsr_safe_aarch64(rv->opt, rv->reg.l, rv->reg.h);
}

int rdmsr_safe_on_cpu_aarch64(unsigned int cpu, u32 opt, u32 *l, u32 *h)
{
	struct msr_info_completion rv;
	call_single_data_t csd = {
		.func	= __rdmsr_safe_on_cpu_aarch64,
		.info	= &rv,
	};
	int err;

	memset(&rv, 0, sizeof(rv));
	init_completion(&rv.done);
	rv.msr.opt = opt;

	err = smp_call_function_single_async(cpu, &csd);
	if (!err) {
		wait_for_completion(&rv.done);
		err = rv.msr.err;
	}

	if ((l != NULL) && (h != NULL)) {
		*l = rv.msr.reg.l;
		*h = rv.msr.reg.h;
	}

	return err;
}
EXPORT_SYMBOL(rdmsr_safe_on_cpu_aarch64);

int wrmsr_safe_on_cpu_aarch64(unsigned int cpu, u32 opt, u32 l, u32 h)
{
	int err;
	struct msr_info rv;

	memset(&rv, 0, sizeof(rv));

	rv.opt = opt;
	rv.reg.l = l;
	rv.reg.h = h;
	err = smp_call_function_single(cpu,
			__wrmsr_safe_on_cpu_aarch64, &rv, 1);

	return err ? err : rv.err;
}
EXPORT_SYMBOL(wrmsr_safe_on_cpu_aarch64);

/*
 * register a hook function for msr
 */
void register_undef_hook_el1(struct undef_hook *hook)
{
	void (*_register_undef_hook)(struct undef_hook *hook);
	_register_undef_hook =
		(void *)get_func_from_kallsyms("register_undef_hook");
	_register_undef_hook(hook);
}
EXPORT_SYMBOL(register_undef_hook_el1);

/*
 * unregister the hook function for msr
 */
void unregister_undef_hook_el1(struct undef_hook *hook)
{
	void (*_unregister_undef_hook)(struct undef_hook *hook);
	_unregister_undef_hook =
		(void *)get_func_from_kallsyms("unregister_undef_hook");
	_unregister_undef_hook(hook);
}
EXPORT_SYMBOL(unregister_undef_hook_el1);

