// SPDX-License-Identifier: GPL-2.0
/*
 * ARM system register access device
 *
 * This device is accessed by read() to the appropriate register number
 * and then read/write in chunks of 8 bytes.  A larger size means multiple
 * reads or writes of the same register.
 *
 * This driver uses /dev/cpu/%d/msr where %d is the minor number, and on
 * an SMP box will direct the access to CPU %d.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/smp.h>
#include <linux/major.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <asm/cpufeature.h>
#include <asm/cpu.h>
#include <asm/sysreg.h>
#include <asm/insn.h>
#include <asm/traps.h>
#include <asm/mmu.h>
#include <asm/barrier.h>
#include <linux/printk.h>
#include "msr_arm.h"

#define AARCH64_INSN_SF_BIT	BIT(31)
#define AARCH64_INSN_N_BIT	BIT(22)
#define AARCH64_INSN_LSL_12	BIT(22)

static struct class *msr_class;
static enum cpuhp_state cpuhp_msr_state;

static int hookers_mrs(struct pt_regs *regs, u32 insn)
{
	/* judge whether to jump */
	if (atomic_read(&mrs_flags) &&
		(regs->pc == (u64)get_read_insn_addr())) {
		/* skip undef instruction and jump */
		regs->pc += 2*AARCH64_INSN_SIZE;
		pr_warn("MSR: undef exception!\n");

		return 0;
	} else {
		/* must be return 1 */
		return 1;
	}
}

static int hookers_msr(struct pt_regs *regs, u32 insn)
{
	/* judge whether to jump */
	if (atomic_read(&msr_flags) &&
		(regs->pc == (u64)get_write_insn_addr())) {
		/* skip undef instruction and jump */
		regs->pc += 2*AARCH64_INSN_SIZE;
		pr_warn("MSR: undef exception!\n");

		return 0;
	} else {
		/* must be return 1 */
		return 1;
	}
}

static struct undef_hook mrs_hook = {
	.instr_mask = 0xfff00000,
	.instr_val  = 0xd5300000,
	.pstate_mask = PSR_MODE_MASK_ALL,
	.pstate_val = PSR_MODE_ALL_EL,
	.fn = hookers_mrs,
};

static struct undef_hook msr_hook = {
	.instr_mask = 0xfff00000,
	.instr_val  = 0xd5100000,
	.pstate_mask = PSR_MODE_MASK_ALL,
	.pstate_val = PSR_MODE_ALL_EL,
	.fn = hookers_msr,
};

/*
 * Before reading and writing register, modify the instruction on
 * corresponding address
 */
static long msr_insn_smc(unsigned int ioc, unsigned long arg)
{
	u32 insnp = 0, insn = 0;
	u32 reg = arg;
	int err = 0, ret = 0;
	unsigned int op0, op1, cn, cm, op2;

	op0 = sys_reg_Op0(reg);
	op1 = sys_reg_Op1(reg);
	cn  = sys_reg_CRn(reg);
	cm  = sys_reg_CRm(reg);
	op2 = sys_reg_Op2(reg);
	err = aarch64_register_check(reg);
	if (err != 0) {
		/* illegal register */
		return err;
	}
	switch (ioc) {
	case 0x00:
		ret = aarch64_insn_read_smc((void *)get_read_insn_addr(),
				&insnp);
		insn = aarch64_insn_mrs_gen(op0, op1, cn, cm, op2,
				insnp & INSN_REG_MASK);
		err = aarch64_modify_read_text(insn);
		break;
	case 0x01:
		ret = aarch64_insn_read_smc((void *)get_write_insn_addr(),
				&insnp);
		insn = aarch64_insn_msr_gen(op0, op1, cn, cm, op2,
				insnp & INSN_REG_MASK);
		err = aarch64_modify_write_text(insn);
		break;
	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

/*
 * In ARMv8-A, A64 instructions have a fixed length of 32 bits
 * and are always little-endian.
 */
int aarch64_insn_read_smc(void *addr, u32 *insnp)
{
	int ret;
	__le32 val;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
	ret = copy_from_kernel_nofault(&val, addr, AARCH64_INSN_SIZE);
#else
	ret = probe_kernel_read(&val, addr, AARCH64_INSN_SIZE);
#endif
	if (!ret)
		*insnp = le32_to_cpu(val);

	return ret;
}

static ssize_t msr_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	u32 __user *tmp = (u32 __user *) buf;
	u32 data[2];
	u32 reg = *ppos;
	int cpu = iminor(file_inode(file));
	int err = 0;
	ssize_t bytes = 0;

	err = msr_insn_smc(0x00, reg);
	if (err != 0) {
		/* illegal register */
		return err;
	}

	if (count % 8)
		return -EINVAL;	/* Invalid chunk size */
	for (; count; count -= 8) {
		err = rdmsr_safe_on_cpu_aarch64(cpu, 1, &data[0], &data[1]);
		if (err)
			break;
		if (copy_to_user(tmp, &data, 8)) {
			printk(KERN_INFO"copy error\n");
			err = -EFAULT;
			break;
		}
		tmp += 2;
		bytes += 8;
	}

	return bytes ? bytes : err;
}

static ssize_t msr_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	const u32 __user *tmp = (const u32 __user *)buf;
	u32 data[2];
	u32 reg = *ppos;
	int cpu = iminor(file_inode(file));
	int err = 0;
	ssize_t bytes = 0;

	err = msr_insn_smc(0x01, reg);
	if (err != 0) {
		/* illegal register */
		return err;
	}

	if (count % 8)
		return -EINVAL;	/* Invalid chunk size */

	if (copy_from_user(&data, tmp, 8)) {
		err = -EFAULT;
		return err;
	}
	err = wrmsr_safe_on_cpu_aarch64(cpu, 1, data[0], data[1]);
	if (err)
		return err;
	bytes += 8;

	return bytes ? bytes : err;
}

/*
 * Before reading and writing register, modify the instruction on
 * corresponding address
 */
static long msr_ioctl(struct file *file, unsigned int ioc,
	unsigned long arg)
{
	u32 insnp = 0, insn = 0;
	u32 reg = arg;
	int err = 0, ret = 0;
	unsigned int op0, op1, cn, cm, op2;

	op0 = sys_reg_Op0(reg);
	op1 = sys_reg_Op1(reg);
	cn  = sys_reg_CRn(reg);
	cm  = sys_reg_CRm(reg);
	op2 = sys_reg_Op2(reg);
	err = aarch64_register_check(reg);
	if (err != 0) {
		/* illegal register */
		return err;
	}
	switch (ioc) {
	case 0x00:
		ret = aarch64_insn_read_smc((void *)get_read_insn_addr(),
				&insnp);
		insn = aarch64_insn_mrs_gen(op0, op1, cn, cm, op2,
				insnp & INSN_REG_MASK);
		err = aarch64_modify_read_text(insn);
		break;
	case 0x01:
		ret = aarch64_insn_read_smc((void *)get_write_insn_addr(),
				&insnp);
		insn = aarch64_insn_msr_gen(op0, op1, cn, cm, op2,
				insnp & INSN_REG_MASK);
		err = aarch64_modify_write_text(insn);
		break;
	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

static int msr_open(struct inode *inode, struct file *file)
{
	unsigned int cpu = iminor(file_inode(file));
	/* TODO */
	struct cpuinfo_arm64 *c;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	if (cpu >= nr_cpu_ids || !cpu_online(cpu))
		return -ENXIO;	/* No such CPU */

	c = &per_cpu(cpu_data, cpu);
	return 0;
}

/*
 * File operations we support
 */
static const struct file_operations msr_fops = {
	.owner = THIS_MODULE,
	.llseek = no_seek_end_llseek,
	.read = msr_read,
	.write = msr_write,
	.open = msr_open,
	.unlocked_ioctl = msr_ioctl,
	.compat_ioctl = msr_ioctl,
};

static int msr_device_create(unsigned int cpu)
{
	struct device *dev;

	dev = device_create(msr_class, NULL, MKDEV(MSR_MAJOR, cpu), NULL,
			    "msr%d", cpu);
	return PTR_ERR_OR_ZERO(dev);
}

static int msr_device_destroy(unsigned int cpu)
{
	device_destroy(msr_class, MKDEV(MSR_MAJOR, cpu));
	return 0;
}

static char *msr_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "cpu/%u/msr", MINOR(dev->devt));
}

static int __init msr_init(void)
{
	int err;

	if (__register_chrdev(MSR_MAJOR, 0, NR_CPUS, "cpu/msr", &msr_fops)) {
		pr_err("unable to get major %d for msr\n", MSR_MAJOR);
		return -EBUSY;
	}
	msr_class = class_create(THIS_MODULE, "msr");
	if (IS_ERR(msr_class)) {
		err = PTR_ERR(msr_class);
		goto out_chrdev;
	}
	msr_class->devnode = msr_devnode;

	/* TODO: set callback function for hotplug */
	err  = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "arm/msr:online",
				 msr_device_create, msr_device_destroy);
	if (err < 0)
		goto out_class;
	cpuhp_msr_state = err;

	/*
	 * register two hooks to block undef instruction exception
	 * in EL1
	 */
	register_undef_hook_el1(&mrs_hook);
	register_undef_hook_el1(&msr_hook);

	/*
	 * Note: it is absolutely necessary to initialize the physical address of
	 * MSR and MRS instructions.
	 */
	err = rdmsr_safe_on_cpu_aarch64(0, 0, NULL, NULL);
	err = wrmsr_safe_on_cpu_aarch64(0, 0, 0, 0);
	return 0;

out_class:
	class_destroy(msr_class);
out_chrdev:
	__unregister_chrdev(MSR_MAJOR, 0, NR_CPUS, "cpu/msr");
	return err;
}
module_init(msr_init);

static void __exit msr_exit(void)
{
	/* unregister the hook of MSR and MRS instruction. */
	unregister_undef_hook_el1(&mrs_hook);
	unregister_undef_hook_el1(&msr_hook);
	cpuhp_remove_state(cpuhp_msr_state);
	class_destroy(msr_class);
	__unregister_chrdev(MSR_MAJOR, 0, NR_CPUS, "cpu/msr");
}

module_exit(msr_exit)

MODULE_AUTHOR("Rongwei Wang <rongwei.wang@linux.alibaba.com>");
MODULE_DESCRIPTION("ARM system register driver");
MODULE_LICENSE("GPL");
