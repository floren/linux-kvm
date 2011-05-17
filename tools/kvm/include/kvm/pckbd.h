#include <rfb/keysym.h>
#include <rfb/rfb.h>
extern void kbd_write_command(u32, u32);
extern u32 kbd_read_data(void);
extern u32 kbd_read_status(void);
extern void kbd_write_data(u32, u32);
extern void dokey(rfbBool, rfbKeySym, rfbClientPtr);
extern void kbd_init(struct kvm *kvm);

