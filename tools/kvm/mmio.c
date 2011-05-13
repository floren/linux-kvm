#include "kvm/kvm.h"

#include <stdio.h>
#include <string.h>
#include <linux/types.h>

/*
static const char *to_direction(u8 is_write)
{
	if (is_write)
		return "write";

	return "read";
}
*/

u8 videomem[2000000];

bool kvm__emulate_mmio(struct kvm *kvm, u64 phys_addr, u8 *data, u32 len, u8 is_write)
{
//	u32 ptr;
		if (is_write) {
//			ptr = phys_addr - 0xd0000000;
//			fprintf(stderr, "phys_addr = %p, videomem = %p, ptr = %x\n", (void*)phys_addr, videomem, ptr);
			memcpy(&videomem[phys_addr - 0xd0000000], data, len);
		} else {
//			ptr = guest_flat_to_host(pd & TARGET_PAGE_MASK) +
//				(addr & ~TARGET_PAGE_MASK);
			//ptr = guest_flat_to_host(self, phys_addr);
//			memcpy(data, ptr, len);
		}

	return true;
}
