/*
 * PS/2 keyboard for KVM. The majority of the logic in here comes straight
 * from QEMU; I condensed the keyboard parts of pckbd.c and ps2.c into this
 * single file to provide just one PS/2 keyboard.
 * Since it comes largely from QEMU, I have left the original header below.
 * John Floren (2011)
 */

/*
 * QEMU PC keyboard emulation
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
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

void kbd_update_kbd_irq(struct kvm*, int);
void kbd_queue(struct kvm *, int);
void ps2_write_keyboard(struct kvm *self, int val);
void ps2_keyboard_set_translation(int mode);

typedef struct {
	int32_t write_cmd; /* if non zero, write data to port 60 is expected */
	uint8_t status;
	uint8_t mode;
	uint8_t outport;
	/* Bitmask of devices with data available.  */
	uint8_t pending;
	void *kbd;
	void *mouse;

	int translate;
	int scan_enabled;
	int scancode_set;
	uint32_t mask;
} KBDState;

KBDState state = { .mode = 0x14, };

#define PS2_QUEUE_SIZE 128

typedef struct {
	int	count;
	char	data[PS2_QUEUE_SIZE];
	int rptr, wptr;
} PS2Queue;

PS2Queue kbdqueue;


/* update irq and KBD_STAT_[MOUSE_]OBF */
/* XXX: not generating the irqs if KBD_MODE_DISABLE_KBD is set may be
   incorrect, but it avoids having to simulate exact delays */
static void kbd_update_irq(struct kvm *self)
{
	KBDState *s = &state;
	int irq_kbd_level;

	irq_kbd_level = 0;
	s->status &= ~(KBD_STAT_OBF);
	s->outport &= ~(KBD_OUT_OBF);
	if (s->pending) {
		s->status |= KBD_STAT_OBF;
		s->outport |= KBD_OUT_OBF;
		/* kbd data takes priority over aux data.  */
			if ((s->mode & KBD_MODE_KBD_INT) &&
				!(s->mode & KBD_MODE_DISABLE_KBD))
				irq_kbd_level = 1;
	}
	kvm__irq_line(self, 1, irq_kbd_level);
}

void kbd_update_kbd_irq(struct kvm *self, int level)
{
	KBDState *s = &state;

	if (level)
		s->pending |= KBD_PENDING_KBD;
	else
		s->pending &= ~KBD_PENDING_KBD;
	kbd_update_irq(self);
}

void kbd_queue(struct kvm *self, int b)
{
	if (kbdqueue.count >= PS2_QUEUE_SIZE)
		return;
	kbdqueue.data[kbdqueue.wptr] = b;
	if (++kbdqueue.wptr == PS2_QUEUE_SIZE)
		kbdqueue.wptr = 0;
	kbdqueue.count++;
	// update_irqs?
	kbd_update_kbd_irq(self, 1);
}

uint32_t kbd_read_data(struct kvm *self)
{
	PS2Queue *q = &kbdqueue;
	int val, index;

	if (q->count == 0) {
		/* NOTE: if no data left, we return the last keyboard one
		   (needed for EMM386) */
		/* XXX: need a timer to do things correctly */
		index = q->rptr - 1;
		if (index < 0)
			index = PS2_QUEUE_SIZE - 1;
		val = q->data[index];
	} else {
		val = q->data[q->rptr];
		if (++q->rptr == PS2_QUEUE_SIZE)
			q->rptr = 0;
		q->count--;
		/* reading deasserts IRQ */
		kbd_update_kbd_irq(self, 0);
		/* reassert IRQs if data left */
		kbd_update_kbd_irq(self, q->count != 0);
	}
	return val;
}

u32 kbd_read_status(void)
{
	KBDState *s = &state;
	int val;
	val = s->status;
	return val;
}

	
void kbd_write_command(struct kvm *self, uint32_t addr, uint32_t val)
{
	KBDState *s = &state;

	/* Bits 3-0 of the output port P2 of the keyboard controller may be pulsed
	 * low for approximately 6 micro seconds. Bits 3-0 of the KBD_CCMD_PULSE
	 * command specify the output port bits to be pulsed.
	 * 0: Bit should be pulsed. 1: Bit should not be modified.
	 * The only useful version of this command is pulsing bit 0,
	 * which does a CPU reset.
	 */
	if((val & KBD_CCMD_PULSE_BITS_3_0) == KBD_CCMD_PULSE_BITS_3_0) {
		if(!(val & 1))
			val = KBD_CCMD_RESET;
		else
			val = KBD_CCMD_NO_OP;
	}

	switch(val) {
	case KBD_CCMD_READ_MODE:
	printf("KBD_CCMD_READ_MODE\n");
		kbd_queue(self, s->mode);
		break;
	case KBD_CCMD_WRITE_MODE:
	case KBD_CCMD_WRITE_OBUF:
	case KBD_CCMD_WRITE_AUX_OBUF:
	case KBD_CCMD_WRITE_MOUSE:
	case KBD_CCMD_WRITE_OUTPORT:
		s->write_cmd = val;
		break;
	case KBD_CCMD_MOUSE_DISABLE:
		s->mode |= KBD_MODE_DISABLE_MOUSE;
		break;
	case KBD_CCMD_MOUSE_ENABLE:
		s->mode &= ~KBD_MODE_DISABLE_MOUSE;
		break;
	case KBD_CCMD_TEST_MOUSE:
		kbd_queue(self, 0x00);
		break;
	case KBD_CCMD_SELF_TEST:
		s->status |= KBD_STAT_SELFTEST;
		kbd_queue(self, 0x55);
		break;
	case KBD_CCMD_KBD_TEST:
		kbd_queue(self, 0x00);
		break;
	case KBD_CCMD_KBD_DISABLE:
		s->mode |= KBD_MODE_DISABLE_KBD;
		kbd_update_irq(self);
		break;
	case KBD_CCMD_KBD_ENABLE:
		s->mode &= ~KBD_MODE_DISABLE_KBD;
		kbd_update_irq(self);
		break;
	case KBD_CCMD_READ_INPORT:
		kbd_queue(self, 0x00);
		break;
	case KBD_CCMD_READ_OUTPORT:
		kbd_queue(self, s->outport);
		break;
	case KBD_CCMD_ENABLE_A20:
//		if (s->a20_out) {
//			qemu_irq_raise(*s->a20_out);
//		}
		s->outport |= KBD_OUT_A20;
		break;
	case KBD_CCMD_DISABLE_A20:
//		if (s->a20_out) {
//			qemu_irq_lower(*s->a20_out);
//		}
		s->outport &= ~KBD_OUT_A20;
		break;
	case KBD_CCMD_RESET:
//		qemu_system_reset_request();
		break;
	case KBD_CCMD_NO_OP:
		/* ignore that */
		break;
	default:
		fprintf(stderr, "qemu: unsupported keyboard cmd=0x%02x\n", val);
		break;
	}
}

/*
   keycode is expressed as follow:
   bit 7	- 0 key pressed, 1 = key released
   bits 6-0 - translated scancode set 2
 */
static void ps2_put_keycode(struct kvm *self, int keycode)
{
	KBDState *s = &state;

	/* XXX: add support for scancode set 1 */
	if (!s->translate && keycode < 0xe0 && s->scancode_set > 1) {
		if (keycode & 0x80) {
			kbd_queue(self, 0xf0);
		}
		if (s->scancode_set == 2) {
			keycode = ps2_raw_keycode[keycode & 0x7f];
		} else if (s->scancode_set == 3) {
			keycode = ps2_raw_keycode_set3[keycode & 0x7f];
		}
	  }
	kbd_queue(self, keycode);
}

void ps2_write_keyboard(struct kvm *self, int val)
{
	KBDState *s = &state;
	switch(state.write_cmd) {
	default:
	case -1:
		switch(val) {
		case 0x00:
			kbd_queue(self, KBD_REPLY_ACK);
			break;
		case 0x05:
			kbd_queue(self, KBD_REPLY_RESEND);
			break;
		case KBD_CMD_GET_ID:
			kbd_queue(self, KBD_REPLY_ACK);
			/* We emulate a MF2 AT keyboard here */
			kbd_queue(self, KBD_REPLY_ID);
			if (s->translate)
				kbd_queue(self, 0x41);
			else
				kbd_queue(self, 0x83);
			break;
		case KBD_CMD_ECHO:
			kbd_queue(self, KBD_CMD_ECHO);
			break;
		case KBD_CMD_ENABLE:
			s->scan_enabled = 1;
			kbd_queue(self, KBD_REPLY_ACK);
			break;
		case KBD_CMD_SCANCODE:
		case KBD_CMD_SET_LEDS:
		case KBD_CMD_SET_RATE:
			state.write_cmd = val;
			kbd_queue(self, KBD_REPLY_ACK);
			break;
		case KBD_CMD_RESET_DISABLE:
//			ps2_reset_keyboard(s);
			s->scan_enabled = 0;
			kbd_queue(self, KBD_REPLY_ACK);
			break;
		case KBD_CMD_RESET_ENABLE:
//			ps2_reset_keyboard(s);
			s->scan_enabled = 1;
			kbd_queue(self, KBD_REPLY_ACK);
			break;
		case KBD_CMD_RESET:
//			ps2_reset_keyboard(s);
			kbd_queue(self, KBD_REPLY_ACK);
			kbd_queue(self, KBD_REPLY_POR);
			break;
		default:
			kbd_queue(self, KBD_REPLY_ACK);
			break;
		}
		break;

	case KBD_CMD_SCANCODE:
		if (val == 0) {
			if (s->scancode_set == 1)
				ps2_put_keycode(self, 0x43);
			else if (s->scancode_set == 2)
				ps2_put_keycode(self, 0x41);
			else if (s->scancode_set == 3)
				ps2_put_keycode(self, 0x3f);
		} else {
			if (val >= 1 && val <= 3)
				s->scancode_set = val;
			kbd_queue(self, KBD_REPLY_ACK);
		}
		state.write_cmd = -1;
		break;

	case KBD_CMD_SET_LEDS:
//		kbd_put_ledstate(val);
		kbd_queue(self, KBD_REPLY_ACK);
		state.write_cmd = -1;
		break;
	case KBD_CMD_SET_RATE:
		kbd_queue(self, KBD_REPLY_ACK);
		state.write_cmd = -1;
		break;
	}
}

void ps2_keyboard_set_translation(int mode)
{
	state.translate = mode;
}

void kbd_write_data(struct kvm *self, uint32_t addr, uint32_t val)
{
	KBDState *s = &state;

	printf("data = 0x%x, write_cmd = 0x%x\n", val, s->write_cmd);

	switch(s->write_cmd) {
	case 0:
		ps2_write_keyboard(self, val);
		break;
	case KBD_CCMD_WRITE_MODE:
		s->mode = val;
		ps2_keyboard_set_translation((s->mode & KBD_MODE_KCC) != 0);
		/* ??? */
		kbd_update_irq(self);
		break;
	case KBD_CCMD_WRITE_OBUF:
		kbd_queue(self, val);
		break;
	case KBD_CCMD_WRITE_AUX_OBUF:
		kbd_queue(self, val);
		break;
	case KBD_CCMD_WRITE_OUTPORT:
//		outport_write(self, val);
		break;
/*
	case KBD_CCMD_WRITE_MOUSE:
		ps2_write_mouse(s->mouse, val);
		break;
*/
	default:
		break;
	}
	s->write_cmd = 0;
}

void kbd_reset(void)
{
	KBDState *s = &state;

	s->mode = KBD_MODE_KBD_INT | KBD_MODE_MOUSE_INT;
	s->status = KBD_STAT_CMD | KBD_STAT_UNLOCKED;
	s->outport = KBD_OUT_RESET | KBD_OUT_A20;
}

static struct kvm *self;	

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
	printf("read key %x\n", key);

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
			kbd_queue(self, 0xe0);
			tosend = 0x70;
		case XK_Delete:
			kbd_queue(self, 0xe0);
			tosend = 0x71;
			break;
		case XK_Up:
			kbd_queue(self, 0xe0);
			tosend = 0x75;
			break;
		case XK_Down:
			kbd_queue(self, 0xe0);
			tosend = 0x72;
			break;
		case XK_Left:
			kbd_queue(self, 0xe0);
			tosend = 0x6b;
			break;
		case XK_Right:
			kbd_queue(self, 0xe0);
			tosend = 0x74;
			break;
		case XK_Page_Up:
			kbd_queue(self, 0xe0);
			tosend = 0x7d;
			break;
		case XK_Page_Down:
			kbd_queue(self, 0xe0);
			tosend = 0x7a;
			break;
		case XK_Home:
			kbd_queue(self, 0xe0);
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
			kbd_queue(self, 0xe0);
		case XK_Control_L:
			tosend = 0x14;
			break;
		case XK_Alt_R:
			kbd_queue(self, 0xe0);
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
		kbd_queue(self, 0xf0);

	if (tosend)
		kbd_queue(self, tosend);
}

static bool kbd_in(struct kvm *self, uint16_t port, void *data, int size, uint32_t count)
{
	uint32_t result;
	if (port == 0x64) {
		result = kbd_read_status();
		ioport__write8(data, (char)result);
	} else {
		result = kbd_read_data(self);
		ioport__write32(data, result);
	}
	return true;
}

static bool kbd_out(struct kvm *self, u16 port, void *data, int size, u32 count)
{
	if (port == 0x64) {
		kbd_write_command(self, (u32)data, *((u32*)data));
	} else {
		kbd_write_data(self, (u32)data, *((u32*)data));
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
