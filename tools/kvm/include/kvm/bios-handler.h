#include <linux/types.h>

struct int13_args {
	u16	eflags;
	u32	eax;
	u32	ebx;
	u32	ecx;
	u32	edx;
	u32	esp;
	u32	ebp;
	u32	esi;
	u32	edi;
	u32	es;
};

void int13_handler(struct int13_args*);
