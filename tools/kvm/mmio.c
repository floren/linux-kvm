#include "kvm/kvm.h"

#include <stdio.h>
#include <string.h>

/*
static const char *to_direction(uint8_t is_write)
{
	if (is_write)
		return "write";

	return "read";
}
*/

uint8_t videomem[2000000];

bool kvm__emulate_mmio(struct kvm *self, uint64_t phys_addr, uint8_t *data, uint32_t len, uint8_t is_write)
{
	uint32_t ptr;

		if (is_write) {
			ptr = phys_addr - 0xd0000000;
//			fprintf(stderr, "phys_addr = %p, videomem = %p, ptr = %p\n", (void*)phys_addr, videomem, ptr);
			memcpy(&videomem[ptr], data, len);
		} else {
//			ptr = guest_flat_to_host(pd & TARGET_PAGE_MASK) +
//				(addr & ~TARGET_PAGE_MASK);
			//ptr = guest_flat_to_host(self, phys_addr);
//			memcpy(data, ptr, len);
		}

	return true;
}
