/*
 *  Fault Injection Test harness (FI)
 *  Copyright (C) Intel Crop.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 *
 */

/*  Id: pf_in.c,v 1.1.1.1 2002/11/12 05:56:32 brlock Exp
 *  Copyright by Intel Crop., 2002
 *  Louis Zhuang (louis.zhuang@intel.com)
 *
 *  Bjorn Steinbrink (B.Steinbrink@gmx.de), 2007
 */

#include <linux/module.h>
#include <linux/ptrace.h> /* struct pt_regs */
#include "pf_in.h"

#ifdef __i386__
/* IA32 Manual 3, 2-1 */
static unsigned char prefix_codes[] = {
	0xF0, 0xF2, 0xF3, 0x2E, 0x36, 0x3E, 0x26, 0x64,
	0x65, 0x2E, 0x3E, 0x66, 0x67
};
/* IA32 Manual 3, 3-432*/
static unsigned int reg_rop[] = {
	0x8A, 0x8B, 0xB60F, 0xB70F, 0xBE0F, 0xBF0F
};
static unsigned int reg_wop[] = { 0x88, 0x89 };
static unsigned int imm_wop[] = { 0xC6, 0xC7 };
/* IA32 Manual 3, 3-432*/
static unsigned int rw8[] = { 0x88, 0x8A, 0xC6 };
static unsigned int rw32[] = {
	0x89, 0x8B, 0xC7, 0xB60F, 0xB70F, 0xBE0F, 0xBF0F
};
static unsigned int mw8[] = { 0x88, 0x8A, 0xC6, 0xB60F, 0xBE0F };
static unsigned int mw16[] = { 0xB70F, 0xBF0F };
static unsigned int mw32[] = { 0x89, 0x8B, 0xC7 };
static unsigned int mw64[] = {};
#else /* not __i386__ */
static unsigned char prefix_codes[] = {
	0x66, 0x67, 0x2E, 0x3E, 0x26, 0x64, 0x65, 0x36,
	0xF0, 0xF3, 0xF2,
	/* REX Prefixes */
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f
};
/* AMD64 Manual 3, Appendix A*/
static unsigned int reg_rop[] = {
	0x8A, 0x8B, 0xB60F, 0xB70F, 0xBE0F, 0xBF0F
};
static unsigned int reg_wop[] = { 0x88, 0x89 };
static unsigned int imm_wop[] = { 0xC6, 0xC7 };
static unsigned int rw8[] = { 0xC6, 0x88, 0x8A };
static unsigned int rw32[] = {
	0xC7, 0x89, 0x8B, 0xB60F, 0xB70F, 0xBE0F, 0xBF0F
};
/* 8 bit only */
static unsigned int mw8[] = { 0xC6, 0x88, 0x8A, 0xB60F, 0xBE0F };
/* 16 bit only */
static unsigned int mw16[] = { 0xB70F, 0xBF0F };
/* 16 or 32 bit */
static unsigned int mw32[] = { 0xC7 };
/* 16, 32 or 64 bit */
static unsigned int mw64[] = { 0x89, 0x8B };
#endif /* not __i386__ */

static int skip_prefix(unsigned char *addr, int *shorted, int *enlarged,
								int *rexr)
{
	int i;
	unsigned char *p = addr;
	*shorted = 0;
	*enlarged = 0;
	*rexr = 0;

restart:
	for (i = 0; i < ARRAY_SIZE(prefix_codes); i++) {
		if (*p == prefix_codes[i]) {
			if (*p == 0x66)
				*shorted = 1;
#ifdef __amd64__
			if ((*p & 0xf8) == 0x48)
				*enlarged = 1;
			if ((*p & 0xf4) == 0x44)
				*rexr = 1;
#endif
			p++;
			goto restart;
		}
	}

	return (p - addr);
}

static int get_opcode(unsigned char *addr, unsigned int *opcode)
{
	int len;

	if (*addr == 0x0F) {
		/* 0x0F is extension instruction */
		*opcode = *(unsigned short *)addr;
		len = 2;
	} else {
		*opcode = *addr;
		len = 1;
	}

	return len;
}

#define CHECK_OP_TYPE(opcode, array, type) \
	for (i = 0; i < ARRAY_SIZE(array); i++) { \
		if (array[i] == opcode) { \
			rv = type; \
			goto exit; \
		} \
	}

enum reason_type get_ins_type(unsigned long ins_addr)
{
	unsigned int opcode;
	unsigned char *p;
	int shorted, enlarged, rexr;
	int i;
	enum reason_type rv = OTHERS;

	p = (unsigned char *)ins_addr;
	p += skip_prefix(p, &shorted, &enlarged, &rexr);
	p += get_opcode(p, &opcode);

	CHECK_OP_TYPE(opcode, reg_rop, REG_READ);
	CHECK_OP_TYPE(opcode, reg_wop, REG_WRITE);
	CHECK_OP_TYPE(opcode, imm_wop, IMM_WRITE);

exit:
	return rv;
}
#undef CHECK_OP_TYPE

static unsigned int get_ins_reg_width(unsigned long ins_addr)
{
	unsigned int opcode;
	unsigned char *p;
	int i, shorted, enlarged, rexr;

	p = (unsigned char *)ins_addr;
	p += skip_prefix(p, &shorted, &enlarged, &rexr);
	p += get_opcode(p, &opcode);

	for (i = 0; i < ARRAY_SIZE(rw8); i++)
		if (rw8[i] == opcode)
			return 1;

	for (i = 0; i < ARRAY_SIZE(rw32); i++)
		if (rw32[i] == opcode)
			return (shorted ? 2 : (enlarged ? 8 : 4));

	printk(KERN_ERR "mmiotrace: Unknown opcode 0x%02x\n", opcode);
	return 0;
}

unsigned int get_ins_mem_width(unsigned long ins_addr)
{
	unsigned int opcode;
	unsigned char *p;
	int i, shorted, enlarged, rexr;

	p = (unsigned char *)ins_addr;
	p += skip_prefix(p, &shorted, &enlarged, &rexr);
	p += get_opcode(p, &opcode);

	for (i = 0; i < ARRAY_SIZE(mw8); i++)
		if (mw8[i] == opcode)
			return 1;

	for (i = 0; i < ARRAY_SIZE(mw16); i++)
		if (mw16[i] == opcode)
			return 2;

	for (i = 0; i < ARRAY_SIZE(mw32); i++)
		if (mw32[i] == opcode)
			return shorted ? 2 : 4;

	for (i = 0; i < ARRAY_SIZE(mw64); i++)
		if (mw64[i] == opcode)
			return shorted ? 2 : (enlarged ? 8 : 4);

	printk(KERN_ERR "mmiotrace: Unknown opcode 0x%02x\n", opcode);
	return 0;
}

/*
 * Define register ident in mod/rm byte.
 * Note: these are NOT the same as in ptrace-abi.h.
 */
enum {
	arg_AL = 0,
	arg_CL = 1,
	arg_DL = 2,
	arg_BL = 3,
	arg_AH = 4,
	arg_CH = 5,
	arg_DH = 6,
	arg_BH = 7,

	arg_AX = 0,
	arg_CX = 1,
	arg_DX = 2,
	arg_BX = 3,
	arg_SP = 4,
	arg_BP = 5,
	arg_SI = 6,
	arg_DI = 7,
#ifdef __amd64__
	arg_R8  = 8,
	arg_R9  = 9,
	arg_R10 = 10,
	arg_R11 = 11,
	arg_R12 = 12,
	arg_R13 = 13,
	arg_R14 = 14,
	arg_R15 = 15
#endif
};

static unsigned char *get_reg_w8(int no, struct pt_regs *regs)
{
	unsigned char *rv = NULL;

	switch (no) {
	case arg_AL:
		rv = (unsigned char *)&regs->ax;
		break;
	case arg_BL:
		rv = (unsigned char *)&regs->bx;
		break;
	case arg_CL:
		rv = (unsigned char *)&regs->cx;
		break;
	case arg_DL:
		rv = (unsigned char *)&regs->dx;
		break;
	case arg_AH:
		rv = 1 + (unsigned char *)&regs->ax;
		break;
	case arg_BH:
		rv = 1 + (unsigned char *)&regs->bx;
		break;
	case arg_CH:
		rv = 1 + (unsigned char *)&regs->cx;
		break;
	case arg_DH:
		rv = 1 + (unsigned char *)&regs->dx;
		break;
#ifdef __amd64__
	case arg_R8:
		rv = (unsigned char *)&regs->r8;
		break;
	case arg_R9:
		rv = (unsigned char *)&regs->r9;
		break;
	case arg_R10:
		rv = (unsigned char *)&regs->r10;
		break;
	case arg_R11:
		rv = (unsigned char *)&regs->r11;
		break;
	case arg_R12:
		rv = (unsigned char *)&regs->r12;
		break;
	case arg_R13:
		rv = (unsigned char *)&regs->r13;
		break;
	case arg_R14:
		rv = (unsigned char *)&regs->r14;
		break;
	case arg_R15:
		rv = (unsigned char *)&regs->r15;
		break;
#endif
	default:
		printk(KERN_ERR "mmiotrace: Error reg no# %d\n", no);
		break;
	}
	return rv;
}

static unsigned long *get_reg_w32(int no, struct pt_regs *regs)
{
	unsigned long *rv = NULL;

	switch (no) {
	case arg_AX:
		rv = &regs->ax;
		break;
	case arg_BX:
		rv = &regs->bx;
		break;
	case arg_CX:
		rv = &regs->cx;
		break;
	case arg_DX:
		rv = &regs->dx;
		break;
	case arg_SP:
		rv = &regs->sp;
		break;
	case arg_BP:
		rv = &regs->bp;
		break;
	case arg_SI:
		rv = &regs->si;
		break;
	case arg_DI:
		rv = &regs->di;
		break;
#ifdef __amd64__
	case arg_R8:
		rv = &regs->r8;
		break;
	case arg_R9:
		rv = &regs->r9;
		break;
	case arg_R10:
		rv = &regs->r10;
		break;
	case arg_R11:
		rv = &regs->r11;
		break;
	case arg_R12:
		rv = &regs->r12;
		break;
	case arg_R13:
		rv = &regs->r13;
		break;
	case arg_R14:
		rv = &regs->r14;
		break;
	case arg_R15:
		rv = &regs->r15;
		break;
#endif
	default:
		printk(KERN_ERR "mmiotrace: Error reg no# %d\n", no);
	}

	return rv;
}

unsigned long get_ins_reg_val(unsigned long ins_addr, struct pt_regs *regs)
{
	unsigned int opcode;
	unsigned char mod_rm;
	int reg;
	unsigned char *p;
	int i, shorted, enlarged, rexr;
	unsigned long rv;

	p = (unsigned char *)ins_addr;
	p += skip_prefix(p, &shorted, &enlarged, &rexr);
	p += get_opcode(p, &opcode);
	for (i = 0; i < ARRAY_SIZE(reg_rop); i++)
		if (reg_rop[i] == opcode) {
			rv = REG_READ;
			goto do_work;
		}

	for (i = 0; i < ARRAY_SIZE(reg_wop); i++)
		if (reg_wop[i] == opcode) {
			rv = REG_WRITE;
			goto do_work;
		}

	printk(KERN_ERR "mmiotrace: Not a register instruction, opcode "
							"0x%02x\n", opcode);
	goto err;

do_work:
	mod_rm = *p;
	reg = ((mod_rm >> 3) & 0x7) | (rexr << 3);
	switch (get_ins_reg_width(ins_addr)) {
	case 1:
		return *get_reg_w8(reg, regs);

	case 2:
		return *(unsigned short *)get_reg_w32(reg, regs);

	case 4:
		return *(unsigned int *)get_reg_w32(reg, regs);

#ifdef __amd64__
	case 8:
		return *(unsigned long *)get_reg_w32(reg, regs);
#endif

	default:
		printk(KERN_ERR "mmiotrace: Error width# %d\n", reg);
	}

err:
	return 0;
}

unsigned long get_ins_imm_val(unsigned long ins_addr)
{
	unsigned int opcode;
	unsigned char mod_rm;
	unsigned char mod;
	unsigned char *p;
	int i, shorted, enlarged, rexr;
	unsigned long rv;

	p = (unsigned char *)ins_addr;
	p += skip_prefix(p, &shorted, &enlarged, &rexr);
	p += get_opcode(p, &opcode);
	for (i = 0; i < ARRAY_SIZE(imm_wop); i++)
		if (imm_wop[i] == opcode) {
			rv = IMM_WRITE;
			goto do_work;
		}

	printk(KERN_ERR "mmiotrace: Not an immediate instruction, opcode "
							"0x%02x\n", opcode);
	goto err;

do_work:
	mod_rm = *p;
	mod = mod_rm >> 6;
	p++;
	switch (mod) {
	case 0:
		/* if r/m is 5 we have a 32 disp (IA32 Manual 3, Table 2-2)  */
		/* AMD64: XXX Check for address size prefix? */
		if ((mod_rm & 0x7) == 0x5)
			p += 4;
		break;

	case 1:
		p += 1;
		break;

	case 2:
		p += 4;
		break;

	case 3:
	default:
		printk(KERN_ERR "mmiotrace: not a memory access instruction "
						"at 0x%lx, rm_mod=0x%02x\n",
						ins_addr, mod_rm);
	}

	switch (get_ins_reg_width(ins_addr)) {
	case 1:
		return *(unsigned char *)p;

	case 2:
		return *(unsigned short *)p;

	case 4:
		return *(unsigned int *)p;

#ifdef __amd64__
	case 8:
		return *(unsigned long *)p;
#endif

	default:
		printk(KERN_ERR "mmiotrace: Error: width.\n");
	}

err:
	return 0;
}