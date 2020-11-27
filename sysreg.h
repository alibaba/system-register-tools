#ifndef __SYSREG_TOOL_MSR_H__
#define __SYSREG_TOOL_MSR_H__

#include <stdio.h>
#include <stdbool.h>

#define MAX_LENGTH 20
#define MAX_LINE	1024
#define MAX_REG		1000

/* Output format */
#define OF_HEX		0x01
#define OF_DEC		0x02
#define OF_OCT		0x03
#define OF_RAW		0x04
#define OF_CHX		0x06
#define OF_MASK		0x0f
#define OF_FILL		0x40
#define OF_C	0x80

#define MAX_OP0 	3
#define MAX_OP1 	7
#define MAX_OP2 	7
#define MAX_CN 		15
#define MAX_CM 		15

/*
 * ARMv8 ARM reserves the following encoding for system registers:
 * (Ref: ARMv8 ARM, Section: "System instruction class encoding overview",
 *  C5.2, version:ARM DDI 0487A.f)
 *	[20-19] : Op0
 *	[18-16] : Op1
 *	[15-12] : CRn
 *	[11-8]  : CRm
 *	[7-5]   : Op2
 */
#define Op0_shift	19
#define Op0_mask	0x3
#define Op1_shift	16
#define Op1_mask	0x7
#define CRn_shift	12
#define CRn_mask	0xf
#define CRm_shift	8
#define CRm_mask	0xf
#define Op2_shift	5
#define Op2_mask	0x7

#define sys_reg(op0, op1, crn, crm, op2) \
	(((op0) << Op0_shift) | ((op1) << Op1_shift) | \
	 ((crn) << CRn_shift) | ((crm) << CRm_shift) | \
	 ((op2) << Op2_shift))

#define sys_insn	sys_reg

#define sys_reg_Op0(id)	(((id) >> Op0_shift) & Op0_mask)
#define sys_reg_Op1(id)	(((id) >> Op1_shift) & Op1_mask)
#define sys_reg_CRn(id)	(((id) >> CRn_shift) & CRn_mask)
#define sys_reg_CRm(id)	(((id) >> CRm_shift) & CRm_mask)
#define sys_reg_Op2(id)	(((id) >> Op2_shift) & Op2_mask)


struct register_t{
	bool type; /* 0:S<op0>_..., 1: ID_ */
	unsigned int length;
	/* union */
	char *name; /* eg. S<op0>_op1_cn_cm_op2 or ID_AA64DFR0_EL1 */
	unsigned int regcode; /* S<op0>_op1_cn_cm_op2 */
};

/* support aarch64 */
extern void aarch64_read_register(int fd, uint64_t *data, int size,
		uint32_t reg);
extern void aarch64_write_register(int fd, uint64_t data, int size,
		uint32_t reg);
extern void aarch64_printf(int mode, uint64_t data,unsigned int highbit,
		unsigned int lowbit);

#endif
