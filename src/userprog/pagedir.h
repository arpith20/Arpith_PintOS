#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#ifdef VM
#include "vm/struct.h"
#endif
#include <stdint.h>
#include <stdbool.h>

uint32_t *pagedir_create(void);
void pagedir_destroy(uint32_t *pd);
bool pagedir_set_page(uint32_t *pd, void *upage, void *kpage, bool rw);
void *pagedir_get_page(uint32_t *pd, const void *upage);
void pagedir_clear_page(uint32_t *pd, void *upage);
bool pagedir_is_dirty(uint32_t *pd, const void *upage);
void pagedir_set_dirty(uint32_t *pd, const void *upage, bool dirty);
bool pagedir_is_accessed(uint32_t *pd, const void *upage);
void pagedir_set_accessed(uint32_t *pd, const void *upage, bool accessed);
void pagedir_activate(uint32_t *pd);

#ifdef VM
void *pagedir_op_page(uint32_t *pd, void *uaddr, void *vm_page);
#endif

#endif /* userprog/pagedir.h */
