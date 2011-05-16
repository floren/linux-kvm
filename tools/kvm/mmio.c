#include "kvm/kvm.h"

#include <stdio.h>
#include <stdlib.h>

#include <linux/types.h>
#include <linux/rbtree.h>

#define MMIO_NODE(n) container_of(n, struct mmio_mapping, node)

struct mmio_mapping {
       u64             start;
       u64             end;
       void            (*kvm_mmio_callback_fn)(u64 addr, u8 *data, u32 len, u8 is_write);
       struct rb_node  node;

       /* This is used to store max end address, used by the interval rb tree */
       u64             max_high;
};

static struct rb_root mmio_tree = RB_ROOT;

static void update_node_max_high(struct rb_node *node, void *arg)
{
       u64 high_left = 0, high_right = 0;
       struct mmio_mapping *data = MMIO_NODE(node);

       if (node->rb_left)
               high_left       = MMIO_NODE(node->rb_left)->end;
       if (node->rb_right)
               high_right      = MMIO_NODE(node->rb_right)->end;

       data->max_high = (high_left > high_right) ? high_left : high_right;
}

/* Find lowest match, Check for overlap */
static struct mmio_mapping *search(struct rb_root *root, u64 addr)
{
       struct rb_node *node = root->rb_node;
       struct rb_node *lowest = NULL;

       while (node) {
               struct mmio_mapping *cur = MMIO_NODE(node);
               if (node->rb_left && (MMIO_NODE(node->rb_left)->max_high > addr)) {
                       node = node->rb_left;
               } else if (cur->start <= addr && cur->end > addr) {
                       lowest = node;
                       break;
               } else if (addr > cur->start) {
                       node = node->rb_right;
               } else {
                       break;
               }
       }

       if (lowest == NULL)
               return NULL;

       return container_of(lowest, struct mmio_mapping, node);
}

static int insert(struct rb_root *root, struct mmio_mapping *data)
{
       struct rb_node **new = &(root->rb_node), *parent = NULL;

       /* Figure out where to put new node */
       while (*new) {
               struct mmio_mapping *this       = MMIO_NODE(*new);
               int result                      = data->start - this->start;

               parent = *new;
               if (result < 0)
                       new = &((*new)->rb_left);
               else if (result > 0)
                       new = &((*new)->rb_right);
               else
                       return 0;
       }

       /* Add new node and rebalance tree. */
       rb_link_node(&data->node, parent, new);
       rb_insert_color(&data->node, root);

       rb_augment_insert(*new, update_node_max_high, NULL);
       return 1;
}

bool kvm__register_mmio(u64 phys_addr, u64 phys_addr_len, void (*kvm_mmio_callback_fn)(u64 addr, u8 *data, u32 len, u8 is_write))
{
       struct mmio_mapping *mmio;

       mmio = malloc(sizeof(*mmio));
       if (mmio == NULL)
               return false;

       *mmio = (struct mmio_mapping) {
               .start = phys_addr,
               .end = phys_addr + phys_addr_len,
               .kvm_mmio_callback_fn = kvm_mmio_callback_fn,
       };

       return insert(&mmio_tree, mmio);
}

bool kvm__deregister_mmio(u64 phys_addr)
{
       struct rb_node *deepest;
       struct mmio_mapping *mmio;

       mmio = search(&mmio_tree, phys_addr);
       if (mmio == NULL)
               return false;

       deepest = rb_augment_erase_begin(&mmio->node);
       rb_erase(&mmio->node, &mmio_tree);
       rb_augment_erase_end(deepest, update_node_max_high, NULL);
       free(mmio);
       return true;
}

static const char *to_direction(u8 is_write)
{
	if (is_write)
		return "write";

	return "read";
}

 bool kvm__emulate_mmio(struct kvm *kvm, u64 phys_addr, u8 *data, u32 len, u8 is_write)
 {
       struct mmio_mapping *mmio = search(&mmio_tree, phys_addr);

       if (mmio)
               mmio->kvm_mmio_callback_fn(phys_addr, data, len, is_write);
       else
               fprintf(stderr, "Warning: Ignoring MMIO %s at %016llx (length %u)\n",
                       to_direction(is_write), phys_addr, len);

       return true;
 }


/*
u8 videomem[2000000];

bool kvm__emulate_mmio(struct kvm *self, u64 phys_addr, u8 *data, u32 len, u8 is_write)
{
//	u32 ptr;
		if (is_write) {
//			ptr = phys_addr - 0xd0000000;
//			fprintf(stderr, "phys_addr = %p, videomem = %p, ptr = %x\n", (void*)phys_addr, videomem, ptr);
			memcpy(&videomem[phys_addr - 0xd0000000], data, len);
		} else {
//			ptr = guest_flat_to_host(pd & TARGET_PAGE_MASK) +
//				(addr & ~TARGET_PAGE_MASK);
			//ptr = guest_flat_to_host(self, phys_addr);
//			memcpy(data, ptr, len);
		}

	return true;
}
*/
