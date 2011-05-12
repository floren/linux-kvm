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

/* Keyboard Replies */
#define KBD_REPLY_POR		0xAA	/* Power on reset */
#define KBD_REPLY_ID		0xAB	/* Keyboard ID */
#define KBD_REPLY_ACK		0xFA	/* Command ACK */
#define KBD_REPLY_RESEND	0xFE	/* Command NACK, send the cmd again */

/*	Keyboard Controller Commands */
#define KBD_CCMD_READ_MODE	0x20	/* Read mode bits */
#define KBD_CCMD_WRITE_MODE	0x60	/* Write mode bits */
#define KBD_CCMD_GET_VERSION	0xA1	/* Get controller version */
#define KBD_CCMD_MOUSE_DISABLE	0xA7	/* Disable mouse interface */
#define KBD_CCMD_MOUSE_ENABLE	0xA8	/* Enable mouse interface */
#define KBD_CCMD_TEST_MOUSE	0xA9	/* Mouse interface test */
#define KBD_CCMD_SELF_TEST	0xAA	/* Controller self test */
#define KBD_CCMD_KBD_TEST	0xAB	/* Keyboard interface test */
#define KBD_CCMD_KBD_DISABLE	0xAD	/* Keyboard interface disable */
#define KBD_CCMD_KBD_ENABLE	0xAE	/* Keyboard interface enable */
#define KBD_CCMD_READ_INPORT	0xC0	/* read input port */
#define KBD_CCMD_READ_OUTPORT	0xD0	/* read output port */
#define KBD_CCMD_WRITE_OUTPORT	0xD1	/* write output port */
#define KBD_CCMD_WRITE_OBUF	0xD2
#define KBD_CCMD_WRITE_AUX_OBUF	0xD3	/* Write to output buffer as if
					   initiated by the auxiliary device */
#define KBD_CCMD_WRITE_MOUSE	0xD4	/* Write the following byte to the mouse */
#define KBD_CCMD_DISABLE_A20	0xDD	/* HP vectra only ? */
#define KBD_CCMD_ENABLE_A20	 0xDF	/* HP vectra only ? */
#define KBD_CCMD_PULSE_BITS_3_0 0xF0	/* Pulse bits 3-0 of the output port P2. */
#define KBD_CCMD_RESET		  0xFE	/* Pulse bit 0 of the output port P2 = CPU reset. */
#define KBD_CCMD_NO_OP		  0xFF	/* Pulse no bits of the output port P2. */

/* Keyboard Commands */
#define KBD_CMD_SET_LEDS	0xED	/* Set keyboard leds */
#define KBD_CMD_ECHO	 	0xEE
#define KBD_CMD_SCANCODE	0xF0	/* Get/set scancode set */
#define KBD_CMD_GET_ID 			0xF2	/* get keyboard ID */
#define KBD_CMD_SET_RATE	0xF3	/* Set typematic rate */
#define KBD_CMD_ENABLE		0xF4	/* Enable scanning */
#define KBD_CMD_RESET_DISABLE	0xF5	/* reset and disable scanning */
#define KBD_CMD_RESET_ENABLE   	0xF6	/* reset and enable scanning */
#define KBD_CMD_RESET		0xFF	/* Reset */

/* Keyboard Replies */
#define KBD_REPLY_POR		0xAA	/* Power on reset */
#define KBD_REPLY_ACK		0xFA	/* Command ACK */
#define KBD_REPLY_RESEND	0xFE	/* Command NACK, send the cmd again */

/* Status Register Bits */
#define KBD_STAT_OBF 		0x01	/* Keyboard output buffer full */
#define KBD_STAT_IBF 		0x02	/* Keyboard input buffer full */
#define KBD_STAT_SELFTEST	0x04	/* Self test successful */
#define KBD_STAT_CMD		0x08	/* Last write was a command write (0=data) */
#define KBD_STAT_UNLOCKED	0x10	/* Zero if keyboard locked */
#define KBD_STAT_MOUSE_OBF	0x20	/* Mouse output buffer full */
#define KBD_STAT_GTO 		0x40	/* General receive/xmit timeout */
#define KBD_STAT_PERR 		0x80	/* Parity error */

/* Controller Mode Register Bits */
#define KBD_MODE_KBD_INT	0x01	/* Keyboard data generate IRQ1 */
#define KBD_MODE_MOUSE_INT	0x02	/* Mouse data generate IRQ12 */
#define KBD_MODE_SYS 		0x04	/* The system flag (?) */
#define KBD_MODE_NO_KEYLOCK	0x08	/* The keylock doesn't affect the keyboard if set */
#define KBD_MODE_DISABLE_KBD	0x10	/* Disable keyboard interface */
#define KBD_MODE_DISABLE_MOUSE	0x20	/* Disable mouse interface */
#define KBD_MODE_KCC 		0x40	/* Scan code conversion to PC format */
#define KBD_MODE_RFU		0x80

/* Output Port Bits */
#define KBD_OUT_RESET		   0x01	/* 1=normal mode, 0=reset */
#define KBD_OUT_A20			 0x02	/* x86 only */
#define KBD_OUT_OBF			 0x10	/* Keyboard output buffer full */
#define KBD_OUT_MOUSE_OBF	   0x20	/* Mouse output buffer full */

/* Mouse Commands */
#define AUX_SET_SCALE11		0xE6	/* Set 1:1 scaling */
#define AUX_SET_SCALE21		0xE7	/* Set 2:1 scaling */
#define AUX_SET_RES		0xE8	/* Set resolution */
#define AUX_GET_SCALE		0xE9	/* Get scaling factor */
#define AUX_SET_STREAM		0xEA	/* Set stream mode */
#define AUX_POLL		0xEB	/* Poll */
#define AUX_RESET_WRAP		0xEC	/* Reset wrap mode */
#define AUX_SET_WRAP		0xEE	/* Set wrap mode */
#define AUX_SET_REMOTE		0xF0	/* Set remote mode */
#define AUX_GET_TYPE		0xF2	/* Get type */
#define AUX_SET_SAMPLE		0xF3	/* Set sample rate */
#define AUX_ENABLE_DEV		0xF4	/* Enable aux device */
#define AUX_DISABLE_DEV		0xF5	/* Disable aux device */
#define AUX_SET_DEFAULT		0xF6
#define AUX_RESET		0xFF	/* Reset aux device */
#define AUX_ACK			0xFA	/* Command byte ACK. */

#define MOUSE_STATUS_REMOTE	 0x40
#define MOUSE_STATUS_ENABLED	0x20
#define MOUSE_STATUS_SCALE21	0x10

#define KBD_PENDING_KBD		 1
#define KBD_PENDING_AUX		 2

/* Table to convert from PC scancodes to raw scancodes.  */
static const unsigned char ps2_raw_keycode[128] = {
  0, 118,  22,  30,  38,  37,  46,  54,  61,  62,  70,  69,  78,  85, 102,  13,
 21,  29,  36,  45,  44,  53,  60,  67,  68,  77,  84,  91,  90,  20,  28,  27,
 35,  43,  52,  51,  59,  66,  75,  76,  82,  14,  18,  93,  26,  34,  33,  42,
 50,  49,  58,  65,  73,  74,  89, 124,  17,  41,  88,   5,   6,   4,  12,   3,
 11,   2,  10,   1,   9, 119, 126, 108, 117, 125, 123, 107, 115, 116, 121, 105,
114, 122, 112, 113, 127,  96,  97, 120,   7,  15,  23,  31,  39,  47,  55,  63,
 71,  79,  86,  94,   8,  16,  24,  32,  40,  48,  56,  64,  72,  80,  87, 111,
 19,  25,  57,  81,  83,  92,  95,  98,  99, 100, 101, 103, 104, 106, 109, 110
};
static const unsigned char ps2_raw_keycode_set3[128] = {
  0,   8,  22,  30,  38,  37,  46,  54,  61,  62,  70,  69,  78,  85, 102,  13,
 21,  29,  36,  45,  44,  53,  60,  67,  68,  77,  84,  91,  90,  17,  28,  27,
 35,  43,  52,  51,  59,  66,  75,  76,  82,  14,  18,  92,  26,  34,  33,  42,
 50,  49,  58,  65,  73,  74,  89, 126,  25,  41,  20,   7,  15,  23,  31,  39,
 47,   2,  63,  71,  79, 118,  95, 108, 117, 125, 132, 107, 115, 116, 124, 105,
114, 122, 112, 113, 127,  96,  97,  86,  94,  15,  23,  31,  39,  47,  55,  63,
 71,  79,  86,  94,   8,  16,  24,  32,  40,  48,  56,  64,  72,  80,  87, 111,
 19,  25,  57,  81,  83,  92,  95,  98,  99, 100, 101, 103, 104, 106, 109, 110
};

typedef struct KBDState {
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

uint32_t kbd_read_status()
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

void kbd_reset()
{
	KBDState *s = &state;

	s->mode = KBD_MODE_KBD_INT | KBD_MODE_MOUSE_INT;
	s->status = KBD_STAT_CMD | KBD_STAT_UNLOCKED;
	s->outport = KBD_OUT_RESET | KBD_OUT_A20;
}

static struct kvm *self;	

void dokey(rfbBool down, rfbKeySym key,rfbClientPtr cl)
{
	printf("read key %x\n", key);
	kbd_queue(self, 0x1c);
}

static bool kbd_in(struct kvm *self, uint16_t port, void *data, int size, uint32_t count)
{
	uint32_t result;
	if (port == 0x64) {
		result = kbd_read_status();
		ioport__write8(data, (char)result);
	} else {
		result = kbd_read_data(self);
		printf("read 0x%x from port 0x%x\n", result, port);
		ioport__write32(data, result);
	}
	return true;
}

static bool kbd_out(struct kvm *self, uint16_t port, void *data, int size, uint32_t count)
{
	char in;
	if (port == 0x64) {
		kbd_write_command(self, (uint32_t)data, *((uint32_t*)data));
	} else {
		kbd_write_data(self, (uint32_t)data, *((uint32_t*)data));
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
	self = kvm;
	ioport__register(0x60, &kbd_ops, 2);
	ioport__register(0x64, &kbd_ops, 2);
	kbd_reset();
	kvm__irq_line(kvm, 1, 0);
}
