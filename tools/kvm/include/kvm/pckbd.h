#include <rfb/keysym.h>
#include <rfb/rfb.h>
extern void kbd_write_command(u32, u32);
extern u32 kbd_read_data(void);
extern u32 kbd_read_status(void);
extern void kbd_write_data(u32, u32);
extern void dokey(rfbBool, rfbKeySym, rfbClientPtr);
extern void kbd_init(struct kvm *kvm);

#define CMD_READ_MODE	0x20
#define CMD_WRITE_MODE	0x60

#define RESPONSE_ACK		0xFA

#define QUEUE_SIZE			128

