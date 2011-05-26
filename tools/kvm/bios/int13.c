#include "kvm/segment.h"
#include "kvm/bios.h"
#include "kvm/bios-handler.h"
#include "kvm/util.h"
#include "kvm/disk-image.h"
#include <stdint.h>

static inline void outb(unsigned short port, unsigned char val)
{
	asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void put4(uint8_t a);
void put8(uint8_t a);
void put16(uint16_t a);
void put32(uint32_t a);

void put4(uint8_t a)
{
	if (a > 9)
		outb(0x3f8, a - 10 + 'a');
	else
		outb(0x3f8, a + '0');
}

void put8(uint8_t a)
{
	put4(a>>4);
	put4(a&0xf);
}

void put16(uint16_t a)
{
	put8(a>>8);
	put8(a);
}

void put32(uint32_t a)
{
	put16(a>>16);
	put16(a);
}

bioscall void int13_handler(struct int13_args *args)
{
	u8 ah;
	struct iovec *iov;

	//disk_image_read(

	ah = (args->eax & 0xff00) >> 8;

	put8(ah);
	outb(0x3f8, '\n');

	//while(1){}

	args->eflags &= 0xfffffffe;
	switch (ah) {
		case 0x00:
			// reset
			args->eax &= 0x00ff; // return status = 0x00 (sucess)
			break;
		case 0x02:
			// read sector from drive
			args->eax &= 0x00ff; // return status = 0x00 (sucess)
			// we need to grab bytes from the disk and put them into es:bx
			break;
		case 0x08:
			// read drive parameters
			args->eax &= 0x00ff; // return status = 0x00 (sucess)
			args->edx |= 0x0f00; // 16 heads thus dh = 15
			args->edx |= 0xff; // 1 drive
			args->ecx &= 0x0000;
			args->ecx |= 0xffff; // 1024 cylinders, 63 tracks
			break;
		case 0x41:
			// test presence of INT 13h extension
			//args->eflags |= 0x1;
			args->ebx |= 0xaa55;
			args->ecx &= 0x8; // we do nothing!!!
			break;
	}
}
