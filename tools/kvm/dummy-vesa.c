#include "kvm/dummy-vesa.h"
#include "kvm/ioport.h"
#include "kvm/util.h"
#include "kvm/kvm.h"
#include "kvm/pci.h"
#include "kvm/kvm-cpu.h"
#include "kvm/irq.h"
#include "kvm/pckbd.h"

#include <rfb/rfb.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <unistd.h>

#define DUMMY_VESA_QUEUE_SIZE 128
#define DUMMY_VESA_IRQ	14

u8 videomem[VESA_MEM_SIZE];

struct vesa_device {
	pthread_mutex_t			mutex;
};

static bool dummy_vesa_pci_io_in(struct kvm *self, u16 port, void *data, int size, u32 count)
{
	return true;
}

static bool dummy_vesa_pci_io_out(struct kvm *self, u16 port, void *data, int size, u32 count)
{
	return true;
}

static struct ioport_operations dummy_vesa_io_ops = {
	.io_in	= dummy_vesa_pci_io_in,
	.io_out	= dummy_vesa_pci_io_out,
};

#define PCI_VENDOR_ID_REDHAT_QUMRANET		0x1af4
#define PCI_DEVICE_ID_DUMMY_VESA		0x1004
#define PCI_SUBSYSTEM_VENDOR_ID_REDHAT_QUMRANET	0x1af4
#define PCI_SUBSYSTEM_ID_DUMMY_VESA		0x0004

static struct pci_device_header dummy_vesa_pci_device = {
	.vendor_id		= PCI_VENDOR_ID_REDHAT_QUMRANET,
	.device_id		= PCI_DEVICE_ID_DUMMY_VESA,
	.header_type		= PCI_HEADER_TYPE_NORMAL,
	.revision_id		= 0,
	.class			= 0x030000,
	.subsys_vendor_id	= PCI_SUBSYSTEM_VENDOR_ID_REDHAT_QUMRANET,
	.subsys_id		= PCI_SUBSYSTEM_ID_DUMMY_VESA,
	.bar[0]			= IOPORT_DUMMY_VESA | PCI_BASE_ADDRESS_SPACE_IO,
	.bar[1]			= VESA_MEM_ADDR,
};


void vesa_mmio_callback(u64 addr, u8 *data, u32 len, u8 is_write) {
	if (is_write) {
		memcpy(&videomem[addr - VESA_MEM_ADDR], data, len);
	}
	return;
}

#define PCI_DUMMY_VESA_DEVNUM 4
void dummy_vesa__init(struct kvm *self)
{
	u8 dev,line,pin;

	int ret = -ENOSYS;
	struct kvm_coalesced_mmio_zone zone;

	if (irq__register_device(PCI_DEVICE_ID_DUMMY_VESA, &dev, &pin, &line) < 0)
		return;

	dummy_vesa_pci_device.irq_pin = pin;
	dummy_vesa_pci_device.irq_line = line;
	pci__register(&dummy_vesa_pci_device, dev);
	ioport__register(IOPORT_DUMMY_VESA, &dummy_vesa_io_ops, IOPORT_DUMMY_VESA_SIZE);

	zone.addr = VESA_MEM_ADDR;
	zone.size = VESA_MEM_SIZE;
	ret = ioctl(self->vm_fd, KVM_REGISTER_COALESCED_MMIO, &zone);
	kvm__register_mmio(VESA_MEM_ADDR, VESA_MEM_SIZE, &vesa_mmio_callback);
}

/*
 * This starts a VNC server to display the framebuffer.
 * It's not altogether clear this belongs here rather than in kvm-run.c
 */
void* dovnc(void* v)
{
	/* I make a fake argc and argv because the getscreen function seems to want it */
	int ac = 1;
	char **av;
	av = malloc(sizeof(char*));
	av[0] = malloc(sizeof(char)*3);
	rfbScreenInfoPtr server=rfbGetScreen(&ac,av,VESA_WIDTH,VESA_HEIGHT,8,3,4);
	server->frameBuffer=videomem;
	server->alwaysShared = TRUE;
	server->kbdAddEvent = dokey;
	server->ptrAddEvent = doptr;
	rfbInitServer(server);
	while (rfbIsActive(server)) {
		rfbMarkRectAsModified(server, 0, 0, VESA_WIDTH, VESA_HEIGHT);
		// This "6000" value is pretty much the result of experimentation
		// It seems that around this value, things update pretty smoothly
		rfbProcessEvents(server, server->deferUpdateTime*6000);
	}
	return NULL;
}


