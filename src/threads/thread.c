#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
#include "devices/timer.h"

/* Random value for struct thread's `magic' member.
 Used to detect stack overflow.  See the big comment at the top
 of thread.h for details. */
#define THREAD_MAGIC 0xdeadbeef //LOL :D

/* List of processes in THREAD_READY state, that is, processes
 that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
 when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/************************************/
/* List of all the threads that are sleeping and waiting for the timer to expire*/
static struct list alarm_blocked_threads;
static bool initialised = false;
/************************************/

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
{
	void *eip; /* Return address. */
	thread_func *function; /* Function to call. */
	void *aux; /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks; /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks; /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
 If true, use multi-level feedback queue scheduler.
 Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *running_thread(void);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static bool is_thread(struct thread *) UNUSED;
static void *alloc_frame(struct thread *, size_t size);
static void schedule(void);
void thread_schedule_tail(struct thread *prev);
static tid_t allocate_tid(void);

/* Initializes the threading system by transforming the code
 that's currently running into a thread.  This can't work in
 general and it is possible in this case only because loader.S
 was careful to put the bottom of the stack at a page boundary.

 Also initializes the run queue and the tid lock.

 After calling this function, be sure to initialize the page
 allocator before trying to create any threads with
 thread_create().

 It is not safe to call thread_current() until this function
 finishes. */
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	lock_init(&tid_lock);

	/*********************************/
	//Arpith's Implementation
	list_init(&alarm_blocked_threads); //this list contains the list of all threads blocked by timer_sleep
	/*********************************/

	list_init(&ready_list);
	list_init(&all_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();

	/************************************/
	initial_thread->sleep_end_tick = 0;

	initialised = true;
	/************************************/
}

/* Starts preemptive thread scheduling by enabling interrupts.
 Also creates the idle thread. */
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
 Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	if (thread_mlfqs)
	{
		mlfqs_computations(t);
	}

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pagedir != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	//Wake up any threads sleeping
	//Wakes up all threads whose timer has expired
	if (initialised)
	{
		thread_wake();
	}

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
 PRIORITY, which executes FUNCTION passing AUX as the argument,
 and adds it to the ready queue.  Returns the thread identifier
 for the new thread, or TID_ERROR if creation fails.

 If thread_start() has been called, then the new thread may be
 scheduled before thread_create() returns.  It could even exit
 before thread_create() returns.  Contrariwise, the original
 thread may run for any amount of time before the new thread is
 scheduled.  Use a semaphore or some other form of
 synchronization if you need to ensure ordering.

 The code provided sets the new thread's `priority' member to
 PRIORITY, but no actual priority scheduling is implemented.
 Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char *name, int priority, thread_func *function,
		void *aux)
{
	struct thread *cur = thread_current();
	struct thread *t;
	struct kernel_thread_frame *kf;
	struct switch_entry_frame *ef;
	struct switch_threads_frame *sf;
	tid_t tid;
	struct dir *working_directory = NULL;

	ASSERT(function != NULL);

	/**********************************************/
	//determines the validity of input arguments
	ASSERT(PRI_MIN<=priority && priority<=PRI_MAX);

#ifdef P4FILESYS
	//for project 4
	//Determines the current working directory
	working_directory = cur->working_dir;
#endif
	/**********************************************/

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* Stack frame for kernel_thread(). */
	kf = alloc_frame(t, sizeof *kf);
	kf->eip = NULL;
	kf->function = function;
	kf->aux = aux;

	/* Stack frame for switch_entry(). */
	ef = alloc_frame(t, sizeof *ef);
	ef->eip = (void (*)(void)) kernel_thread;

	/* Stack frame for switch_threads(). */
	sf = alloc_frame(t, sizeof *sf);
	sf->eip = switch_entry;
	sf->ebp = 0;

	/* Add to run queue. */
	thread_unblock(t);

	//If newly created thread has higher priority than the current thread under
	//execution, then yield it and hopefully[:)] run the new thread
	//--
	//In case of Advanced scheduler, do not yield the current thread
	//immediately, instead let the scheduler decide at a later stage
	if (!thread_mlfqs)
	{
		if (priority > thread_current()->priority)
		{
			thread_yield();
		}
	}

#ifdef USERPROG

	sema_init(&t->sema_process_wait, 0);
	sema_init(&t->sema_process_load, 0);
	sema_init(&t->sema_process_exit, 0);

	list_init(&t->files);

	t->return_status = RET_STATUS_OK;	//Initialize return status with 0;

	t->process_exited = false;
	t->process_waited = false;
	t->parent = thread_current();
#ifdef VM
	list_init(&t->mmap_files);
	list_init(&t->children);
	if (cur != initial_thread)
	list_push_front(&cur->children, &t->child_elem);
#endif
#ifdef P4FILESYS
	//restore the working directory
	if (working_directory != NULL)
		t->working_dir = dir_reopen(working_directory);
#endif

#endif

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
 again until woken by thread_unblock().

 This function must be called with interrupts turned off.  It
 is usually a better idea to use one of the synchronization
 primitives in synch.h. */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);

	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
 This is an error if T is not blocked.  (Use thread_yield() to
 make the running thread ready.)

 This function does not preempt the running thread.  This can
 be important: if the caller had disabled interrupts itself,
 it may expect that it can atomically unblock a thread and
 update other data. */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));

	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);
	list_push_back(&ready_list, &t->elem);
	t->status = THREAD_READY;
	intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
 This is running_thread() plus a couple of sanity checks.
 See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	 If either of these assertions fire, then your thread may
	 have overflowed its stack.  Each thread has less than 4 kB
	 of stack, so a few big automatic arrays or moderate
	 recursion can cause stack overflow. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
 returns to the caller. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef VM
	struct list_elem *e;
	struct thread *cur = NULL;
	struct list cur_c;

	cur = thread_current();
	cur_c = cur->children;

	if (cur != NULL)
	{
		for (e = list_begin(&thread_current()->children);
				e != list_end(&thread_current()->children); e = list_next(e))
		{
			if (list_entry(e, struct thread, child_elem)->process_exited)
			sema_up(
					&(list_entry(e, struct thread, child_elem))->sema_process_exit);
			else
			{
				list_entry(e, struct thread, child_elem)->parent = NULL;
				list_remove(
						&(list_entry(e, struct thread, child_elem))->child_elem);
			}

		}
	}
#endif
#ifdef USERPROG
	process_exit();
#endif
#ifdef VM
	if (cur != NULL && cur->parent != NULL)
	if (cur->parent != initial_thread)
	list_remove(&cur->child_elem);
#endif

	/* Remove thread from all threads list, set our status to dying,
	 and schedule another process.  That process will destroy us
	 when it calls thread_schedule_tail(). */
	intr_disable();
	list_remove(&thread_current()->allelem);
	thread_current()->status = THREAD_DYING;
	schedule();
	NOT_REACHED ()
	;
}

/* Yields the CPU.  The current thread is not put to sleep and
 may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void)
{
	struct thread *cur = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable();
	if (cur != idle_thread)
		list_push_back(&ready_list, &cur->elem);
	cur->status = THREAD_READY;
	schedule();
	intr_set_level(old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
 This function must be called with interrupts off. */
void thread_foreach(thread_action_func *func, void *aux)
{
	struct list_elem *e;

	ASSERT(intr_get_level() == INTR_OFF);

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, allelem);
		func(t, aux);
	}
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{
	//thread_current()->priority = new_priority; //original code
	set_priority(thread_current(), new_priority, true);
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice)
{
	enum intr_level original_interrupt_state = intr_disable();
	struct thread *t = thread_current();
	struct list_elem *e;

	validate_data(&nice, 2);

	t->nice = nice;

	t->priority = ((PRI_MAX * (1 << 14)) - (t->recent_cpu / 4)
			- (t->nice * (1 << 14) * 2)) / (1 << 14);
	validate_data(&t->priority, 1);

	//Yield the current thread immediately if it's priority becomes less than
	//some other thread in ready list
	for (e = list_begin(&ready_list); e != list_end(&ready_list);
			e = list_next(e))
	{
		struct thread *t2 = list_entry(e, struct thread, elem);
		ASSERT(is_thread(t2));

		if (t2->priority > t->priority)
		{
			thread_yield();
		}

	}

	intr_set_level(original_interrupt_state);
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
	return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
	enum intr_level original_interrupt_state = intr_disable();
	int value = ((load_avg * 100) + (1 << 14) / 2) / (1 << 14);
	intr_set_level(original_interrupt_state);
	return value;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
	struct thread *t = thread_current();
	enum intr_level original_interrupt_state = intr_disable();
	int value = ((t->recent_cpu * 100) + (1 << 14) / 2) / (1 << 14);
	intr_set_level(original_interrupt_state);
	return value;
}

/* Idle thread.  Executes when no other thread is ready to run.

 The idle thread is initially put on the ready list by
 thread_start().  It will be scheduled once initially, at which
 point it initializes idle_thread, "up"s the semaphore passed
 to it to enable thread_start() to continue, and immediately
 blocks.  After that, the idle thread never appears in the
 ready list.  It is returned by next_thread_to_run() as a
 special case when the ready list is empty. */
static void idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;
	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.

		 The `sti' instruction disables interrupts until the
		 completion of the next instruction, so these two
		 instructions are executed atomically.  This atomicity is
		 important; otherwise, an interrupt could be handled
		 between re-enabling interrupts and waiting for the next
		 one to occur, wasting as much as one clock tick worth of
		 time.

		 See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		 7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread(void)
{
	uint32_t *esp;

	/* Copy the CPU's stack pointer into `esp', and then round that
	 down to the start of a page.  Because `struct thread' is
	 always at the beginning of a page and the stack pointer is
	 somewhere in the middle, this locates the curent thread. */
	asm ("mov %%esp, %0" : "=g" (esp));
	return pg_round_down(esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool is_thread(struct thread *t)
{
	return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
 NAME. */
static void init_thread(struct thread *t, const char *name, int priority)
{
	enum intr_level old_level;

	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->stack = (uint8_t *) t + PGSIZE;
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	/*******************************/
	t->priority_original = priority;
	t->sleep_end_tick = 0;
	list_init(&t->thread_locks);
	t->required_lock = NULL;

	t->nice = 0;
	/*******************************/

	old_level = intr_disable();
	list_push_back(&all_list, &t->allelem);
	intr_set_level(old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
 returns a pointer to the frame's base. */
static void *
alloc_frame(struct thread *t, size_t size)
{
	/* Stack data is always allocated in word-size units. */
	ASSERT(is_thread(t));
	ASSERT(size % sizeof(uint32_t) == 0);

	t->stack -= size;
	return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
 return a thread from the run queue, unless the run queue is
 empty.  (If the running thread can continue running, then it
 will be in the run queue.)  If the run queue is empty, return
 idle_thread. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
//	else
//		return list_entry(list_pop_front(&ready_list), struct thread, elem);

	/**********************************/
	//this function returns the function with maximum priority in ready list
	return thread_with_max_priority();
	/**********************************/
}

/* Completes a thread switch by activating the new thread's page
 tables, and, if the previous thread is dying, destroying it.

 At this function's invocation, we just switched from thread
 PREV, the new thread is already running, and interrupts are
 still disabled.  This function is normally invoked by
 thread_schedule() as its final action before returning, but
 the first time a thread is scheduled it is called by
 switch_entry() (see switch.S).

 It's not safe to call printf() until the thread switch is
 complete.  In practice that means that printf()s should be
 added at the end of the function.

 After this function and its caller returns, the thread switch
 is complete. */
void thread_schedule_tail(struct thread *prev)
{
	struct thread *cur = running_thread();

	ASSERT(intr_get_level() == INTR_OFF);

	/* Mark us as running. */
	cur->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate();
#endif

	/* If the thread we switched from is dying, destroy its struct
	 thread.  This must happen late so that thread_exit() doesn't
	 pull out the rug under itself.  (We don't free
	 initial_thread because its memory was not obtained via
	 palloc().) */
	if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
	{
		ASSERT(prev != cur);
		palloc_free_page(prev);
	}
}

/* Schedules a new process.  At entry, interrupts must be off and
 the running process's state must have been changed from
 running to some other state.  This function finds another
 thread to run and switches to it.

 It's not safe to call printf() until thread_schedule_tail()
 has completed. */
static void schedule(void)
{
	struct thread *cur = running_thread();
	struct thread *next = next_thread_to_run();
	struct thread *prev = NULL;

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(cur->status != THREAD_RUNNING);
	ASSERT(is_thread(next));

	if (cur != next)
		prev = switch_threads(cur, next);
	thread_schedule_tail(prev);
}

/* Returns a tid to use for a new thread. */
static tid_t allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

/* Offset of `stack' member within `struct thread'.
 Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof(struct thread, stack);

/*******************************************************************/
// All Functions implemented as part of the assignment is placed here
void thread_sleep(int64_t start, int64_t ticks)
{
	ASSERT(!intr_context());
	struct thread *current_thread = thread_current();
	current_thread->sleep_end_tick = start + ticks;
	list_push_back(&alarm_blocked_threads, &current_thread->alarmsleepelem);
	thread_block();
}

inline void thread_wake()
{

	ASSERT(intr_get_level() == INTR_OFF);

	int64_t current_ticks = timer_ticks();
	struct list_elem *e;

	for (e = list_begin(&alarm_blocked_threads);
			e != list_end(&alarm_blocked_threads); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, alarmsleepelem);
		ASSERT(is_thread(t));
		if (t->sleep_end_tick <= current_ticks)
		{
			t->sleep_end_tick = 0;
			list_remove(&t->alarmsleepelem);
			thread_unblock(t);
			intr_yield_on_return();
		}
	}
}

struct thread * thread_with_max_priority()
{
	int max_priority = -1;
	struct thread *max_thread = NULL;
	struct list_elem *max_elem = NULL;
	struct list_elem *e = NULL;

	for (e = list_begin(&ready_list); e != list_end(&ready_list);
			e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, elem);
		ASSERT(is_thread(t));

		if (t->priority > max_priority)
		{
			max_thread = t;
			max_elem = e;
			max_priority = t->priority;
		}

	}
	list_remove(max_elem);
	return max_thread;
}

void set_priority(struct thread *t, int new_priority, bool forced)
{
	/*Extract from PintOS documentation:
	 *
	 *Like the priority scheduler, the advanced scheduler chooses
	 *the thread to run based on priorities. However, the advanced
	 *the scheduler does not do priority donation
	 */
	if (!thread_mlfqs)
	{
		if (forced)
		{
			if (t->priority != t->priority_original)
				t->priority_original = new_priority;
			else
				t->priority_original = t->priority = new_priority;
		}
		else
		{
			t->priority = new_priority;
		}

		list_sort(&ready_list, sort_helper, NULL);
		struct thread *next = list_entry(list_begin(&ready_list), struct thread,
				elem);

		//ensures that we do not yield a process in ready queue (but not in execution)
		if (t == thread_current()) //setting priority of current thread
		{
			if (next->priority > new_priority)
			{
				thread_yield();
			}
		}
	}
}

//mlfqs computations
inline void mlfqs_computations(struct thread *t)
{
	if (initialised)
	{
		int64_t current_ticks = timer_ticks();
		fixed_point_real_increment(&t->recent_cpu, 1);
		if ((current_ticks % TIMER_FREQ) == 0)
		{
			calculate_all();
		}
		if ((current_ticks % 4) == 0)
		{
			calculate_thread_priority_mlqfs(t);
		}
	}
}

inline void calculate_all()
{
	calculate_load_avg();
	calculate_recent_cpu();
	calculate_priority_mlfqs();
}

inline void calculate_load_avg()
{
	int ready_threads, coefficient;

	ready_threads = list_size(&ready_list);
	if (thread_current() != idle_thread)
	{
		ready_threads = ready_threads + 1;
	}

	ready_threads = (ready_threads * (1 << 14)) / 60;

	coefficient = (59 * (1 << 14)) / 60;

	load_avg = (((int64_t) load_avg) * coefficient / (1 << 14)) + ready_threads;
}
inline void calculate_recent_cpu()
{
	ASSERT(intr_get_level() == INTR_OFF);

	struct list_elem *e;
	int coefficient;

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, allelem);
		ASSERT(is_thread(t));

		/*Note from the PintOS Documentation:
		 *You may need to think about the order of calculations in this formula.
		 *
		 *We recommend computing the coefficient of recent_cpu first,
		 *then multiplying.
		 *Some students have reported that multiplying load_avg by recent_cpu
		 *directly can cause overflow.
		 */
		coefficient = (((int64_t) (2 * load_avg)) * (1 << 14))
				/ (2 * load_avg + (1 * (1 << 14)));
		t->recent_cpu = (((int64_t) coefficient) * t->recent_cpu / (1 << 14))
				+ (t->nice * (1 << 14));

	}
}
inline void calculate_priority_mlfqs()
{
	ASSERT(intr_get_level() == INTR_OFF);

	struct list_elem *e;

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, allelem);
		ASSERT(is_thread(t));

		calculate_thread_priority_mlqfs(t);
	}
}

inline void calculate_thread_priority_mlqfs(struct thread *t)
{
	ASSERT(intr_get_level() == INTR_OFF);
	t->priority = (((PRI_MAX * (1 << 14)) - (t->recent_cpu / 4)
			- (t->nice * 2 * (1 << 14))) / (1 << 14));
	validate_data(&t->priority, 1);
}

inline void validate_data(int *data, int type) //1-priority; 2-nice value
{
	switch (type)
	{
	case 1: //validate priority
		if (*data > PRI_MAX)
			*data = PRI_MAX;
		if (*data < PRI_MIN)
			*data = PRI_MIN;
		break;
	case 2:	//validate nice value
		if (*data > 20)
			*data = 20;
		if (*data < (-20))
			*data = -20;
		break;
	default:
		ASSERT(false)
		;
	}
}

inline void fixed_point_real_increment(int *original, int value)
{
	*original = ((*original) + (value * (1 << 14)));
}

struct thread *
tid_to_thread(tid_t tid)
{
	struct list_elem *e;

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, allelem);

		ASSERT(is_thread(t));

		if (t->tid == tid)
			return t;
	}

	return NULL;
}
/*******************************************************************/
