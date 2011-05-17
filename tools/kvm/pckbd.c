#include "kvm/read-write.h"
#include "kvm/ioport.h"
#include "kvm/mutex.h"
#include "kvm/util.h"
#include "kvm/term.h"
#include "kvm/kvm.h"
#include "kvm/pckbd.h"
#include <rfb/keysym.h>
#include <rfb/rfb.h>
#include <stdint.h>

#define CMD_READ_MODE	0x20
#define CMD_WRITE_MODE	0x60

#define RESPONSE_ACK		0xFA

#define QUEUE_SIZE			128

typedef struct KBDstate {
	char 	q[QUEUE_SIZE]; // keyboard queue
	int 	read, write; // indexes into the queue
	int 	count; // number of elements in queue

	u8 	mode;
	u8 	status;
	u32 	write_cmd;
} KBDstate;

static KBDstate state;

static struct kvm *self;

static void kbd_update_irq(void)
{
	u8 level;
	if (!state.count) {
		state.status &= 0xfe; // unset output buffer full bit
		level = 0;
	} else {
		state.status |= 0x01;
		level = 1;
	}
	kvm__irq_line(self, 1, level);
}

static void kbd_queue(u8 c)
{
	if (state.count >= QUEUE_SIZE)
		return;

	state.q[state.write++] = c;
	if (state.write == QUEUE_SIZE)
		state.write = 0;

	state.count++;
	kbd_update_irq();
}

void kbd_write_command(u32 addr, u32 val)
{
	switch (val) {
		case CMD_READ_MODE:
			kbd_queue(state.mode);
			break;
		case CMD_WRITE_MODE:
			state.write_cmd = val;
			break;
		default:
			break;
	}
}

u32 kbd_read_data(void)
{
	u32 ret;
	int i;

	if (state.count == 0) {
		i = state.read - 1;
		if (i < 0)
			i = QUEUE_SIZE;
		ret = state.q[i];
	} else {
		ret = state.q[state.read++];
		if (state.read == QUEUE_SIZE)
			state.read = 0;
		state.count--;
		kvm__irq_line(self, 1, 0);
		kbd_update_irq();
	}
	return ret;
}

u32 kbd_read_status(void)
{
	return (u32)state.status;
}

void kbd_write_data(u32 addr, u32 val)
{
	switch (state.write_cmd) {
		case CMD_WRITE_MODE:
			/* I have no idea why this works but it does... wtf? */
			kbd_queue(RESPONSE_ACK);
			kbd_queue(0xab);
			kbd_queue(0x41);
			kbd_update_irq();
			break;
		default:
			/* Yeah whatever */
			kbd_queue(RESPONSE_ACK);
			break;
	}
}

static void kbd_reset(void)
{
	state.status = 0x18;
	state.mode = 0x01;
	state.read = state.write = state.count = 0;
}

static char letters[26] = {
	0x1c, 0x32, 0x21, 0x23, 0x24, /* a-e */
	0x2b, 0x34, 0x33, 0x43, 0x3b, /* f-j */
	0x42, 0x4b, 0x3a, 0x31, 0x44, /* k-o */
	0x4d, 0x15, 0x2d, 0x1b, 0x2c, /* p-t */
	0x3c, 0x2a, 0x1d, 0x22, 0x35, /* u-y */
	0x1a,
};

static char num[10] = {
	0x45, 0x16, 0x1e, 0x26, 0x2e, 0x23, 0x36, 0x3d, 0x3e, 0x46,
};

void dokey(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
	char tosend = 0; // set it to 0 at first

	if (key >= 0x41 && key <= 0x5a)
		key += 0x20; // convert to lowercase

	if (key >= 0x61 && key <= 0x7a) 	// a-z
		tosend = letters[key - 0x61];

	if (key >= 0x30 && key <= 0x39)
		tosend = num[key - 0x30];


	/* I apologize for this, but the ASCII tables and keyboard scan codes
	 * did not want to play nicely, this seemed the best way to handle things for now */
	switch (key) {
		case XK_BackSpace:
			tosend = 0x66;
			break;
		case XK_Tab:
			tosend = 0x0d;
			break;
		case XK_Return:
			tosend = 0x5a;
			break;
		case XK_Escape:
			tosend = 0x76;
			break;
		case XK_Insert:
			kbd_queue(0xe0);
			tosend = 0x70;
		case XK_Delete:
			kbd_queue(0xe0);
			tosend = 0x71;
			break;
		case XK_Up:
			kbd_queue(0xe0);
			tosend = 0x75;
			break;
		case XK_Down:
			kbd_queue(0xe0);
			tosend = 0x72;
			break;
		case XK_Left:
			kbd_queue(0xe0);
			tosend = 0x6b;
			break;
		case XK_Right:
			kbd_queue(0xe0);
			tosend = 0x74;
			break;
		case XK_Page_Up:
			kbd_queue(0xe0);
			tosend = 0x7d;
			break;
		case XK_Page_Down:
			kbd_queue(0xe0);
			tosend = 0x7a;
			break;
		case XK_Home:
			kbd_queue(0xe0);
			tosend = 0x6c;
			break;
		case XK_End:
			tosend = 0x69;
			break;
		case XK_Shift_L:
			tosend = 0x12;
			break;
		case XK_Shift_R:
			tosend = 0x59;
			break;
		case XK_Control_R:
			kbd_queue(0xe0);
		case XK_Control_L:
			tosend = 0x14;
			break;
		case XK_Alt_R:
			kbd_queue(0xe0);
		case XK_Alt_L:
			tosend = 0x11;
			break;
		case XK_quoteleft:
			tosend = 0x0e;
			break;
		case XK_minus:
			tosend = 0x4e;
			break;
		case XK_equal:
			tosend = 0x55;
			break;
		case XK_bracketleft:
			tosend = 0x54;
			break;
		case XK_bracketright:
			tosend = 0x5b;
			break;
		case XK_backslash:
			tosend = 0x5d;
			break;
		case XK_Caps_Lock:
			tosend = 0x58;
			break;
		case XK_semicolon:
			tosend = 0x4c;
			break;
		case XK_quoteright:
			tosend = 0x52;
			break;
		case XK_comma:
			tosend = 0x41;
			break;
		case XK_period:
			tosend = 0x49;
			break;
		case XK_slash:
			tosend = 0x4a;
			break;
		case XK_space:
			tosend = 0x29;
			break;

		/* This is where I handle the shifted characters. They don't really map nicely the way A-Z maps to a-z, so I'm doing it manually */
		case XK_exclam:
			tosend = 0x16;
			break;
		case XK_quotedbl:
			tosend = 0x52;
			break;
		case XK_numbersign:
			tosend = 0x26;
			break;
		case XK_dollar:
			tosend = 0x25;
			break;
		case XK_percent:
			tosend = 0x2e;
			break;
		case XK_ampersand:
			tosend = 0x3d;
			break;
		case XK_parenleft:
			tosend = 0x46;
			break;
		case XK_parenright:
			tosend = 0x45;
			break;
		case XK_asterisk:
			tosend = 0x3e;
			break;
		case XK_plus:
			tosend = 0x55;
			break;
		case XK_colon:
			tosend = 0x4c;
			break;
		case XK_less:
			tosend = 0x41;
			break;
		case XK_greater:
			tosend = 0x49;
			break;
		case XK_question:
			tosend = 0x4a;
			break;
		case XK_at:
			tosend = 0x1e;
			break;
		case XK_asciicircum:
			tosend = 0x36;
			break;
		case XK_underscore:
			tosend = 0x4e;
			break;
		case XK_braceleft:
			tosend = 0x54;
			break;
		case XK_braceright:
			tosend = 0x5b;
			break;
		case XK_bar:
			tosend = 0x5d;
			break;
		case XK_asciitilde:
			tosend = 0x0e;
			break;
		default:
			break;
	}

	if (!down && tosend != 0x0)
		kbd_queue(0xf0);

	if (tosend)
		kbd_queue(tosend);
}

static bool kbd_in(struct kvm *self, uint16_t port, void *data, int size, uint32_t count)
{
	uint32_t result;
	if (port == 0x64) {
		result = kbd_read_status();
		ioport__write8(data, (char)result);
	} else {
		result = kbd_read_data();
		ioport__write32(data, result);
	}
	return true;
}

static bool kbd_out(struct kvm *self, u16 port, void *data, int size, u32 count)
{
	if (port == 0x64) {
		kbd_write_command((u32)data, *((u32*)data));
	} else {
		kbd_write_data((u32)data, *((u32*)data));
	}
	return true;
}

static struct ioport_operations kbd_ops = {
	.io_in		= kbd_in,
	.io_out		= kbd_out,
};

void kbd_init(struct kvm *kvm)
{
	self = kvm;
	ioport__register(0x60, &kbd_ops, 2);
	ioport__register(0x64, &kbd_ops, 2);
	kbd_reset();
	kvm__irq_line(kvm, 1, 0);
}
