#ifndef KVM__DUMMY_VESA_H
#define KVM__DUMMY_VESA_H

#define VESA_WIDTH 640
#define VESA_HEIGHT 480

#define VESA_MEM_ADDR 0xd0000000
#define VESA_MEM_SIZE (4*VESA_WIDTH*VESA_HEIGHT)
#define VESA_BPP 32

struct kvm;

void dummy_vesa__init(struct kvm *self);

// Here's my framebuffer stuff
extern char videomem[VESA_MEM_SIZE];
void* dovnc(void*);

#endif //KVM__DUMMY_VESA_H
