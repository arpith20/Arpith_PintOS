#include "threads/thread.h"
#include "threads/synch.h"

inline bool sort_helper(const struct list_elem *a, const struct list_elem *b,
		void *aux)
{
	if (aux)
	{

	}
	ASSERT(a != NULL && b!=NULL);
	struct thread *a_t = list_entry(a, struct thread, elem);
	struct thread *b_t = list_entry(b, struct thread, elem);
	if (thread_mlfqs)
	{
		return (a_t->priority_mlfqs > b_t->priority_mlfqs);
	}
	else
	{
		return (a_t->priority > b_t->priority);
	}
}

inline bool lock_priority_less_helper(const struct list_elem *a,
		const struct list_elem *b, void *aux)
{
	if (aux)
	{

	}
	ASSERT(a != NULL && b!=NULL);
	struct lock *a_t = list_entry(a, struct lock, lock_holder_elem);
	struct lock *b_t = list_entry(b, struct lock, lock_holder_elem);
	if (thread_mlfqs)
	{
		return true; //CHANGE THIS
	}
	else
	{
		return (a_t->priority > b_t->priority);
	}
}


