#include <rfb/keysym.h>
#include <rfb/rfb.h>
extern void kbd_write_command(u32, u32);
extern u32 kbd_read_data(void);
extern u32 kbd_read_status(void);
extern void kbd_write_data(u32, u32);
extern void kbd_handle_key(rfbBool, rfbKeySym, rfbClientPtr);
extern void kbd__init(struct kvm *kvm);

extern void kbd_handle_ptr(int buttonMask,int x,int y,rfbClientPtr cl);

#define KBD_IRQ		1
#define MOUSE_IRQ		12

#define CMD_READ_MODE	0x20
#define CMD_WRITE_MODE	0x60
#define CMD_WRITE_AUX_BUF 0xD3
#define CMD_WRITE_MOUSE	0xD4
#define CMD_TEST_MOUSE	0xA9
#define CMD_DISABLE_MOUSE	0xA7
#define CMD_ENABLE_MOUSE	0xA8

#define RESPONSE_ACK		0xFA

#define KBD_OBF		0x01;
#define MOUSE_OBF		0x20;

#define MODE_DISABLE_MOUSE	0x20

#define MOUSE_ENABLE_REPORTING	0x20


#define QUEUE_SIZE			128
