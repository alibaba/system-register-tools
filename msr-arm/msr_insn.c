/*
 * ARMv8 related functions and macros.
 */
#include <linux/module.h>
#include <linux/kallsyms.h>
#include "msr_arm.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
/*
 * The 'kallsyms_lookup_name' maybe not export from kernel.
 */
#define KPROBE_LOOKUP 1
#include <linux/kprobes.h>
static struct kprobe kp = {
	    .symbol_name = "kallsyms_lookup_name"
};

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
#endif

/*
 *  * get the function from kallsyms
 *   */
unsigned long get_func_from_kallsyms(char *func)
{
	/* mainly for finding patch_text (/proc/kallsyms) */
	unsigned long addr;

#ifdef KPROBE_LOOKUP
	kallsyms_lookup_name_t kallsyms_lookup_name;
	register_kprobe(&kp);
	kallsyms_lookup_name = (kallsyms_lookup_name_t) kp.addr;
	addr = kallsyms_lookup_name(func);
	unregister_kprobe(&kp);
	addr = kallsyms_lookup_name(func);
#else
	addr = kallsyms_lookup_name(func);
#endif
	return addr;
}

/*
 * ARMv8 ARM reserves the following encoding for system registers:
 * (Ref: ARMv8 ARM, Section: "System instruction class encoding overview",
 *  C5.2, version:ARM DDI 0487A.f)
 *	[20-19] : Op0
 *	[18-16] : Op1
 *	[15-12] : CRn
 *	[11-8]  : CRm
 *	[7-5]   : Op2
 *
 * make MSR/MRS instruction
 */
u32 aarch64_insn_mrs_gen(u32 op0, u32 op1, u32 crn, u32 crm, u32 op2, u32 rt)
{
	return (AARCH64_MRS_INSN | sys_reg(op0, op1, crn, crm, op2) | rt);
}
EXPORT_SYMBOL(aarch64_insn_mrs_gen);

u32 aarch64_insn_msr_gen(u32 op0, u32 op1, u32 crn, u32 crm, u32 op2, u32 rt)
{
	return (AARCH64_MSR_INSN | sys_reg(op0, op1, crn, crm, op2) | rt);
}
EXPORT_SYMBOL(aarch64_insn_msr_gen);

/*
 * make SYS instruction
 */
u32 aarch64_insn_other_gen(u32 op0, u32 op1, u32 crn, u32 crm, u32 op2, u32 rt)
{
	return (AARCH64_SYS_INSN | sys_reg(1, op1, crn, crm, op2) | rt);
}
EXPORT_SYMBOL(aarch64_insn_other_gen);

/*
 * check the regcode legal
 */
int aarch64_register_check(u32 reg)
{
	unsigned int op0 = 0, op1 = 0, cn = 0, cm = 0, op2 = 0;
	u32 max_reg;
	int ret;

	max_reg = sys_reg(MAX_OP0, MAX_OP1, MAX_CN, MAX_CM, MAX_OP2);
	if (reg & ~max_reg) {
		/* illegal regcode */
		ret = -EFAULT;
		return ret;
	}

	op0 = sys_reg_Op0(reg);
	op1 = sys_reg_Op1(reg);
	cn = sys_reg_CRn(reg);
	cm = sys_reg_CRm(reg);
	op2 = sys_reg_Op2(reg);

	/*
	 * for system registers, their value of op0: 0b10 or 0b11.
	 */
	if ((op0 != 2) && (op0 != 3)) {
		/* NOT support */
		return -EFAULT;
	}

	if ((op0 <= MAX_OP0) && (op1 <= MAX_OP1) && (op2 <= MAX_OP2)
			&& (cn <= MAX_CN) && (cm <= MAX_CM)) {
		/* legal regcode */
		ret = 0;
	} else {
		/* illegal regcode */
		ret = -EFAULT;
	}
	return ret;
}
EXPORT_SYMBOL(aarch64_register_check);
