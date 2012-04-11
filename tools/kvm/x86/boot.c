#include "kvm/kvm.h"

#include "kvm/util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <fcntl.h>

#define BIOS_SP		0x8000

bool kvm__load_firmware(struct kvm *kvm, const char *firmware_filename, u32 load_addr, u32 entry_addr)
{
	struct stat st;
	void *p;
	int fd;
	int nr;

	fd = open(firmware_filename, O_RDONLY);
	if (fd < 0)
		return false;

	if (fstat(fd, &st))
		return false;

	if (st.st_size > MB_FIRMWARE_BIOS_SIZE)
		die("firmware image %s is too big to fit in memory (%lu KB).\n", firmware_filename, st.st_size / 1024);

	p = guest_real_to_host(kvm, (load_addr >> 4) & 0xf000, load_addr & 0xffff);

	while ((nr = read(fd, p, st.st_size)) > 0)
		p += nr;

	kvm->boot_selector	= (entry_addr >> 4) & 0xf000;
	kvm->boot_ip		=  entry_addr & 0xffff;
	kvm->boot_sp		= BIOS_SP;

	return true;
}
