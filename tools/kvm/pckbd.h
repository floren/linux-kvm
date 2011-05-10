extern void kbd_write_command(struct kvm*, uint32_t, uint32_t);
extern uint32_t kbd_read_data(struct kvm *);
extern uint32_t kbd_read_status(void);
extern void kbd_reset(void);
