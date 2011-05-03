#include "kvm/segment.h"
#include "kvm/bios.h"
#include "kvm/util.h"
#include "kvm/dummy-vesa.h"
#include <stdint.h>

//static long cursor;

struct int10args {
	uint32_t	eax;
	uint32_t	ebx;
	uint32_t	ecx;
	uint32_t	edx;
	uint32_t	esp;
	uint32_t	ebp;
	uint32_t	esi;
	uint32_t	edi;
	uint32_t	es;
};


typedef struct {
        uint16_t off, seg;
} far_ptr;

#if 0
struct vesa_general_info {
        u32 signature;          /* 0 Magic number = "VESA" */
        u16 version;            /* 4 */
        far_ptr vendor_string;  /* 6 */
        u32 capabilities;       /* 10 */
        far_ptr video_mode_ptr; /* 14 */
        u16 total_memory;       /* 18 */

        u8 reserved[236];       /* 20 */
} __attribute__ ((packed));
#endif


struct vbeinfo {
	unsigned char signature[4];
	unsigned char version[2];
	unsigned char vendor_string[4];
	unsigned char capabilities[4];
	unsigned char video_mode_ptr[4];
	unsigned char total_memory[2];

	unsigned char reserved[236];
};


struct vminfo {
        uint16_t mode_attr;          /* 0 */
        uint8_t win_attr[2];         /* 2 */
        uint16_t win_grain;          /* 4 */
        uint16_t win_size;           /* 6 */
        uint16_t win_seg[2];         /* 8 */
        uint32_t win_scheme;     /* 12 */
        uint16_t logical_scan;       /* 16 */

        uint16_t h_res;              /* 18 */
        uint16_t v_res;              /* 20 */
        uint8_t char_width;          /* 22 */
        uint8_t char_height;         /* 23 */
        uint8_t memory_planes;       /* 24 */
        uint8_t bpp;                 /* 25 */
        uint8_t banks;               /* 26 */
        uint8_t memory_layout;       /* 27 */
        uint8_t bank_size;           /* 28 */
        uint8_t image_planes;        /* 29 */
        uint8_t page_function;       /* 30 */

        uint8_t rmask;               /* 31 */
        uint8_t rpos;                /* 32 */
        uint8_t gmask;               /* 33 */
        uint8_t gpos;                /* 34 */
        uint8_t bmask;               /* 35 */
        uint8_t bpos;                /* 36 */
        uint8_t resv_mask;           /* 37 */
        uint8_t resv_pos;            /* 38 */
        uint8_t dcm_info;            /* 39 */

        uint32_t lfb_ptr;            /* 40 Linear frame buffer address */
        uint32_t offscreen_ptr;      /* 44 Offscreen memory address */
        uint16_t offscreen_size;     /* 48 */

        uint8_t reserved[206];       /* 50 */
};

char oemstring[11] = "dummy vesa";
uint16_t modes[2] = { 0x0112, 0xffff };


//void int10putchar(struct int10args *args);
//void int10vesa(struct int10args *args);
void int10handler(struct int10args *args);

static inline void set_fs(uint16_t seg)
{
	asm volatile("movw %0,%%fs" : : "rm" (seg));
}

static inline uint8_t rdfs8(unsigned long addr)
{
	uint8_t v;

	asm volatile("addr32 movb %%fs:%1,%0" : "=q" (v) : "m" (*(uint8_t *)addr));

	return v;
}

static inline
void outb( unsigned short port, unsigned char val )
{
    asm volatile( "outb %0, %1"
                  : : "a"(val), "Nd"(port) );
}

/*
 * It's probably much more useful to make this print to the serial
 * line rather than print to a non-displayed VGA memory
 */
static inline void int10putchar(struct int10args *args)
{
	uint8_t al, ah;

	al = args->eax & 0xFF;
	ah = (args->eax & 0xFF00) >> 8;

	outb(0x3f8, al);
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

static inline void int10vesa(struct int10args *args)
{
	uint8_t al, ah;
	struct vbeinfo *destination;
	struct vminfo *vi;
	uint32_t u;

	al = args->eax;
	ah = args->eax >> 8;

	if (al == 00) {
		// Set controller info
		//destination = (struct vbeinfo*)(((args->es & 0xffff) << 4) + (args->edi & 0xffff));
		destination = (struct vbeinfo*)args->edi;
		destination->signature[0] = 'V';
		destination->signature[1] = 'E';
		destination->signature[2] = 'S';
		destination->signature[3] = 'A';
		destination->version[0] = 0x00;
		destination->version[1] = 0x02;
		u = (uint32_t)oemstring;
		destination->vendor_string[0] = u >> 24;
		destination->vendor_string[1] = u >> 16;
		destination->vendor_string[2] = u >> 8;
		destination->vendor_string[3] = u;
		destination->capabilities[0] = 0x10;
		destination->capabilities[1] = 0x00;
		u = (int)&modes;
		destination->video_mode_ptr[0] = u;
		destination->video_mode_ptr[1] = u >> 8;
		destination->video_mode_ptr[2] = 0x00;
		destination->video_mode_ptr[3] = 0xf0;
		destination->total_memory[0] = 0;
		destination->total_memory[1] = 20;
	} else if (al == 01) {
		vi = (struct vminfo*)args->edi;
		vi->mode_attr  = 0xd9; //11011011
		vi->logical_scan = VESA_WIDTH*4;
		vi->h_res = VESA_WIDTH;
		vi->v_res = VESA_HEIGHT;
		vi->bpp = VESA_BPP;
		vi->memory_layout = 6;
		vi->memory_planes = 1;
		vi->lfb_ptr = VESA_MEM_ADDR;
		vi->rmask = 8;
		vi->gmask = 8;
		vi->bmask = 8;
		vi->resv_mask = 8;
		vi->resv_pos = 24;
		vi->bpos = 16;
		vi->gpos = 8;
	}

	args->eax &= 0x0000;
	args->eax |= 0x004f;	// return success every time

}

/* Working in C is less painful that fiddling with assembly */
bioscall void int10handler(struct int10args *args)
{
	uint8_t ah;
	ah = (args->eax & 0xff00) >> 8;

	if (ah == 0x0e) {
		int10putchar(args);
	} else if (ah == 0x4f) {
		int10vesa(args);
	}
}
