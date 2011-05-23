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

typedef struct KBDstate {
	char 	kq[QUEUE_SIZE]; // keyboard queue
	int 	kread, kwrite; // indexes into the queue
	int 	kcount; // number of elements in queue

	char mq[QUEUE_SIZE];
	int	mread, mwrite;
	int	mcount;

	u8	mstatus;
	u8	mres;
	u8	msample;

	u8 	mode;
	u8 	status;
	u32 	write_cmd;
} KBDstate;

static KBDstate state;

static struct kvm *self;

static void kbd_update_irq(void)
{
	u8 klevel, mlevel = 0;

	state.status &= ~KBD_OBF;
	state.status &= ~AUX_OBF;

	if (state.kcount == 0) {
		state.status &= ~KBD_OBF; // unset output buffer full bit
		klevel = 0;
	} else {
		state.status |= KBD_OBF;
		klevel = 1;
	}

	if (klevel == 0 && state.mcount != 0) {
		state.status |= KBD_OBF;
		state.status |= AUX_OBF;
		mlevel = 1;
	}

	kvm__irq_line(self, KBD_IRQ, klevel);
	kvm__irq_line(self, AUX_IRQ, mlevel);
}

static void mouse_queue(u8 c)
{
	if (state.mcount >= QUEUE_SIZE)
		return;

	state.mq[state.mwrite++] = c;
	if (state.mwrite == QUEUE_SIZE)
		state.mwrite = 0;

	state.mcount++;
	kbd_update_irq();
}

static void kbd_queue(u8 c)
{
	if (state.kcount >= QUEUE_SIZE)
		return;

	state.kq[state.kwrite++] = c;
	if (state.kwrite == QUEUE_SIZE)
		state.kwrite = 0;

	state.kcount++;
	kbd_update_irq();
}

void kbd_write_command(u32 addr, u32 val)
{
	switch (val) {
		case CMD_READ_MODE:
			kbd_queue(state.mode);
			break;
		case CMD_WRITE_MODE:
		case CMD_WRITE_AUX:
		case CMD_WRITE_AUX_BUF:
			state.write_cmd = val;
			break;
		case CMD_TEST_AUX:
			// 0x0 means we're a normal PS/2 mouse
			mouse_queue(0x0);
			break;
		case CMD_DISABLE_AUX:
			state.mode |= MODE_DISABLE_AUX;
			break;
		case CMD_ENABLE_AUX:
			state.mode &= ~MODE_DISABLE_AUX;
			break;
		default:
			break;
	}
}

u32 kbd_read_data(void)
{
	u32 ret;
	int i;

	if (state.kcount != 0) {
		ret = state.kq[state.kread++];
		if (state.kread == QUEUE_SIZE)
			state.kread = 0;
		state.kcount--;
		kvm__irq_line(self, KBD_IRQ, 0);
		kbd_update_irq();
	} else if (state.mcount > 0) {
		ret = state.mq[state.mread++];
		if (state.mread == QUEUE_SIZE)
			state.mread = 0;
		state.mcount--;
		kvm__irq_line(self, AUX_IRQ, 0);
		kbd_update_irq();
	} else if (state.kcount == 0) {
		i = state.kread - 1;
		if (i < 0)
			i = QUEUE_SIZE;
		ret = state.kq[i];
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
			state.mode = val;
			kbd_update_irq();
			break;
		case CMD_WRITE_AUX_BUF:
			mouse_queue(val);
			mouse_queue(RESPONSE_ACK);
			break;
		case CMD_WRITE_AUX:
			mouse_queue(RESPONSE_ACK);
			switch (val) {
				case 0xe6:
					// set scaling = 1:1
					state.mstatus &= ~AUX_SCALING_FLAG;
					break;
				case 0xe8:
					// set resolution
					break;
				case 0xe9:
					mouse_queue(state.mstatus);
					mouse_queue(state.mres);
					mouse_queue(state.msample);
					break;
				case 0xf2:
					// send ID
					mouse_queue(0x00);
					break;
				case 0xf3:
					// set sample rate
					state.msample = val;
					break;
				case 0xf4:
					state.mstatus |= AUX_ENABLE_REPORTING;
					break;
				case 0xf5:
					state.mstatus &= ~AUX_ENABLE_REPORTING;
					break;
				case 0xf6:
					// set defaults
					break;
				case 0xff:
					// reset
					state.mstatus = 0x0;
					state.mres = AUX_DEFAULT_RESOLUTION;
					state.msample = AUX_DEFAULT_SAMPLE;
					break;
				default:
					break;
			}
			break;
		case 0:
			// Just send the ID
			kbd_queue(RESPONSE_ACK);
			kbd_queue(0xab);
			kbd_queue(0x41);
			kbd_update_irq();
			break;
		default:
			/* Yeah whatever */
			break;
	}
	state.write_cmd = 0;
}

static void kbd_reset(void)
{
	state.status = KBD_STATUS_SYS | KBD_STATUS_A2 | KBD_STATUS_INH; // 0x1c
	state.mode = KBD_MODE_KBD_INT | KBD_MODE_SYS; // 0x3
	state.kread = state.kwrite = state.kcount = 0;
	state.mread = state.mwrite = state.mcount = 0;
	state.mstatus = 0x0;
	state.mres = AUX_DEFAULT_RESOLUTION;
	state.msample = AUX_DEFAULT_SAMPLE;
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

void kbd_handle_key(rfbBool down, rfbKeySym key, rfbClientPtr cl)
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

void kbd__init(struct kvm *kvm)
{
	self = kvm;
	ioport__register(0x60, &kbd_ops, 2);
	ioport__register(0x64, &kbd_ops, 2);
	kbd_reset();
	kvm__irq_line(kvm, 1, 0); // kbd = irq 1
	kvm__irq_line(kvm, 12, 0); // mouse = irq 12
}

static int xlast, ylast = -1;

void kbd_handle_ptr(int buttonMask,int x,int y,rfbClientPtr cl)
{
	int dx, dy;
	char b1 = 0x8;

	b1 |= buttonMask;

	if (xlast >= 0 && ylast >= 0) {
		dx = x - xlast;
		dy = ylast - y;

		if (dy > 255)
			b1 |= 0x80;
		if (dx > 255)
			b1 |= 0x40;

		if (dy < 0)
			b1 |= 0x20;
		if (dx < 0)
			b1 |= 0x10;

		mouse_queue(b1);
		mouse_queue(dx);
		mouse_queue(dy);
	}

	xlast = x;
	ylast = y;
	rfbDefaultPtrAddEvent(buttonMask, x, y, cl);
}
