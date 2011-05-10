#include "kvm/dummy-vesa.h"
#include "kvm/virtio-pci.h"
#include "kvm/disk-image.h"
#include "kvm/virtio.h"
#include "kvm/ioport.h"
#include "kvm/util.h"
#include "kvm/term.h"
#include "kvm/mutex.h"
#include "kvm/kvm.h"
#include "kvm/pci.h"
#include "kvm/kvm-cpu.h"

#include <rfb/rfb.h>

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <termios.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

#define DUMMY_VESA_QUEUE_SIZE 128
#define DUMMY_VESA_IRQ	14

char videomem[VESA_MEM_SIZE];

struct vesa_device {
	pthread_mutex_t			mutex;
};

static bool dummy_vesa_pci_io_in(struct kvm *self, uint16_t port, void *data, int size, uint32_t count)
{
	return true;
}

static bool dummy_vesa_pci_io_out(struct kvm *self, uint16_t port, void *data, int size, uint32_t count)
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
	.irq_pin		= 4,
	.irq_line		= DUMMY_VESA_IRQ,
};

#define PCI_DUMMY_VESA_DEVNUM 4
void dummy_vesa__init(struct kvm *self)
{
	int ret = -ENOSYS;
	struct kvm_coalesced_mmio_zone zone;

	pci__register(&dummy_vesa_pci_device, PCI_DUMMY_VESA_DEVNUM);
	ioport__register(IOPORT_DUMMY_VESA, &dummy_vesa_io_ops, IOPORT_DUMMY_VESA_SIZE);

	zone.addr = VESA_MEM_ADDR;
	zone.size = VESA_MEM_SIZE;
	ret = ioctl(self->vm_fd, KVM_REGISTER_COALESCED_MMIO, &zone);
}

/*
 * This starts a VNC server to display the framebuffer.
 * It's not altogether clear this belongs here rather than in kvm-run.c
 */
#include <rfb/keysym.h>
#include "pckbd.h"

static int kbd_char;
static char kbd_status;
static char kbd_command;

static void dokey(rfbBool down,rfbKeySym key,rfbClientPtr cl)
{
	kbd_char = key;
	printf("read key %x\n", key);
}

void kbd__inject_interrupt(struct kvm *self)
{
	kvm__irq_line(self, 1, 0);
	kvm__irq_line(self, 1, 1);
}

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
	rfbInitServer(server);
	while (rfbIsActive(server)) {
		rfbMarkRectAsModified(server, 0, 0, VESA_WIDTH, VESA_HEIGHT);
		rfbProcessEvents(server, server->deferUpdateTime*1000);
	}
	return NULL;
}

static bool kbd_in(struct kvm *self, uint16_t port, void *data, int size, uint32_t count)
{
	uint32_t result;
	if (port == 0x64) {
		//ioport__write8(data, 0xfa);
		//printf("saying status = 0x14\n");
		result = kbd_read_status();
		printf("read 0x%x from port 0x%x\n", result, port);
		ioport__write8(data, (char)result);
	} else {
		//ioport__write8(data, kbd_char);
		result = kbd_read_data(self);
		printf("read 0x%x from port 0x%x\n", result, port);
		ioport__write8(data, (char)result);
		//printf("durp reading from port 0x60\n");
	}
	//printf("kbd_in port = %x data = %x\n", port, *(uint32_t*)data);
	return true;
}

static bool kbd_out(struct kvm *self, uint16_t port, void *data, int size, uint32_t count)
{
	char in;
	if (port == 0x64) {
		//in = ioport__read8(data);
		kbd_write_command(self, (uint32_t)data, *((uint32_t*)data));
	}
	printf("kbd_out port = %x data = %x\n", port, *(uint32_t*)data);
	return true;
}

static struct ioport_operations kbd_ops = {
	.io_in		= kbd_in,
	.io_out		= kbd_out,
};

void kbd_init(struct kvm *kvm)
{
	ioport__register(0x60, &kbd_ops, 2);
	ioport__register(0x64, &kbd_ops, 2);
	kbd_reset();
	kvm__irq_line(kvm, 1, 0);
}
