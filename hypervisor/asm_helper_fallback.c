#include <linux/types.h>
#include <asm/msr.h>

#include "asm_helper.h"

u64 hb_rdmsr(u32 msr_index)
{
	u32 lo, hi;

	native_rdmsr(msr_index, lo, hi);
	return ((u64)hi << 32) | lo;
}
