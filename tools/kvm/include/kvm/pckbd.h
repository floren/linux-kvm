#include <rfb/keysym.h>
#include <rfb/rfb.h>
extern void kbd_write_command(struct kvm*, u32, u32);
extern u32 kbd_read_data(struct kvm *);
extern u32 kbd_read_status(void);
extern void kbd_write_data(struct kvm*, u32, u32);
extern void kbd_reset(void);
extern void dokey(rfbBool, rfbKeySym, rfbClientPtr);