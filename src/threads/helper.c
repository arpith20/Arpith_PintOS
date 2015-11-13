#include "threads/thread.h"
#include "threads/synch.h"
#include <stdbool.h>
#include <stddef.h>

bool sort_helper(const struct list_elem *a, const struct list_elem *b,
		void *aux)
{
	if (aux)
	{

	}
	ASSERT(a != NULL && b!=NULL);
	struct thread *a_t = list_entry(a, struct thread, elem);
	struct thread *b_t = list_entry(b, struct thread, elem);

	return (a_t->priority > b_t->priority);

}

bool lock_priority_less_helper(const struct list_elem *a,
		const struct list_elem *b, void *aux)
{
	if (aux)
	{

	}
	ASSERT(a != NULL && b!=NULL);
	struct lock *a_t = list_entry(a, struct lock, lock_holder_elem);
	struct lock *b_t = list_entry(b, struct lock, lock_holder_elem);

	return (a_t->priority > b_t->priority);

}

