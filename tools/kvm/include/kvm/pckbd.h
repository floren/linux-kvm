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
#define AUX_IRQ		12

#define CMD_READ_MODE	0x20
#define CMD_WRITE_MODE	0x60
#define CMD_WRITE_AUX_BUF 0xD3
#define CMD_WRITE_AUX	0xD4
#define CMD_TEST_AUX	0xA9
#define CMD_DISABLE_AUX	0xA7
#define CMD_ENABLE_AUX	0xA8

#define RESPONSE_ACK		0xFA

#define KBD_OBF		0x01;
#define AUX_OBF		0x20;

#define MODE_DISABLE_AUX	0x20

#define AUX_ENABLE_REPORTING	0x20
#define AUX_SCALING_FLAG		0x10
#define AUX_DEFAULT_RESOLUTION	0x2
#define AUX_DEFAULT_SAMPLE		100

#define KBD_STATUS_SYS	0x4
#define KBD_STATUS_A2		0x8
#define KBD_STATUS_INH	0x10

#define KBD_MODE_KBD_INT	0x01
#define KBD_MODE_SYS		0x02

#define QUEUE_SIZE			128
