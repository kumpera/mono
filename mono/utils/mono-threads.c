/*
 * mono-threads.c: Low-level threading
 *
 * Author:
 *	Rodrigo Kumpera (kumpera@gmail.com)
 *
 * (C) 2011 Novell, Inc
 */

#include <mono/utils/mono-compiler.h>
#include <mono/utils/mono-semaphore.h>
#include <mono/utils/mono-threads.h>
#include <mono/utils/hazard-pointer.h>
#include <mono/metadata/gc-internal.h>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/threads-types.h>

#include <pthread.h>
#include <errno.h>

/*FIXME make this possibly larger and allow for some rehashing*/

typedef struct {
	void *(*start_routine) (void *);
	void *arg;
	int flags;
	MonoSemType registered;
} ThreadStartInfo;

static int thread_info_size;
static MonoThreadInfoCallbacks threads_callbacks;
static MonoThreadInfoRuntimeCallbacks runtime_callbacks;
static pthread_key_t thread_info_key;
static MonoThreadInfo *thread_list;

static void
suspend_signal_handler (int _dummy, siginfo_t *info, void *context);
static void
free_thread_info (gpointer mem);

static inline gpointer
mask (gpointer n, uintptr_t bit)
{
	return (gpointer)(((uintptr_t)n) | bit);
}

static gpointer
get_hazardous_pointer_with_mask (gpointer *pp, MonoThreadHazardPointers *hp, int hazard_index)
{
	gpointer p;

	for (;;) {
		/* Get the pointer */
		p = *pp;
		/* If we don't have hazard pointers just return the
		   pointer. */
		if (!hp)
			return p;
		/* Make it hazardous */
		mono_hazard_pointer_set (hp, hazard_index, list_pointer_unmask (p));
		/* Check that it's still the same.  If not, try
		   again. */
		if (*pp != p) {
			mono_hazard_pointer_clear (hp, hazard_index);
			continue;
		}
		break;
	}

	return p;
}

static inline void
mono_hazard_pointer_clear_all (MonoThreadHazardPointers *hp, int retain)
{
	if (retain != 0)
		mono_hazard_pointer_clear (hp, 0);
	if (retain != 1)
		mono_hazard_pointer_clear (hp, 1);
	if (retain != 2)
		mono_hazard_pointer_clear (hp, 2);
}

MonoThreadInfo*
mono_thread_info_current (void)
{
	return pthread_getspecific (thread_info_key);
}

static gboolean
list_find (MonoThreadInfo **head, MonoThreadHazardPointers *hp, MonoNativeThreadId id)
{
	MonoThreadInfo *cur, *next;
	MonoThreadInfo **prev;
	MonoNativeThreadId cur_id;

try_again:
	prev = head;
	mono_hazard_pointer_set (hp, 2, prev);

	cur = list_pointer_unmask (get_hazardous_pointer ((gpointer*)prev, hp, 1));

	while (1) {
		if (cur == NULL)
			return FALSE;
		next = get_hazardous_pointer_with_mask ((gpointer*)&cur->next, hp, 0);
		cur_id = cur->tid;

		if (*prev != cur)
			goto try_again;

		if (!list_pointer_get_mark (next)) {
			if (cur_id >= id)
				return cur_id == id;

			prev = &cur->next;
			mono_hazard_pointer_set (hp, 2, cur);
		} else {
			next = list_pointer_unmask (next);
			if (InterlockedCompareExchangePointer ((volatile gpointer*)prev, next, cur) == next)
				mono_thread_hazardous_free_or_queue (cur, free_thread_info);
			else
				goto try_again;
		}
		cur = list_pointer_unmask (next);
		mono_hazard_pointer_set (hp, 1, cur);
	}
}

static gboolean
list_insert (MonoThreadInfo **head, MonoThreadHazardPointers *hp, MonoThreadInfo *info)
{
	MonoThreadInfo *cur, **prev;
	/*We must do a store barrier before inserting 
	to make sure all values in @node are globally visible.*/
	mono_memory_barrier ();

	while (1) {
		if (list_find (head, hp, info->tid))
			return FALSE;
		cur = mono_hazard_pointer_get_val (hp, 1);
		prev = mono_hazard_pointer_get_val (hp, 2);

		info->next = cur;
		mono_hazard_pointer_set (hp, 0, info);
		if (InterlockedCompareExchangePointer ((volatile gpointer*)prev, info, cur) == cur)
			return TRUE;
	}
}

static gboolean
list_remove (MonoThreadInfo **head, MonoThreadHazardPointers *hp, MonoThreadInfo *info)
{
	MonoThreadInfo *cur, **prev, *next;
	while (1) {
		if (!list_find (head, hp, info->tid))
			return FALSE;

		next = mono_hazard_pointer_get_val (hp, 0);
		cur = mono_hazard_pointer_get_val (hp, 1);
		prev = mono_hazard_pointer_get_val (hp, 2);

		if (InterlockedCompareExchangePointer ((volatile gpointer*)&cur->next, mask (next, 1), next) != next)
			continue;
		if (InterlockedCompareExchangePointer ((volatile gpointer*)prev, next, cur) == cur)
			mono_thread_hazardous_free_or_queue (info, free_thread_info);
		else
			list_find (head, hp, info->tid);
		return TRUE;
	}
}

/*
If return non null Hazard Pointer 1 holds the return value.
*/
static MonoThreadInfo*
mono_thread_info_lookup (MonoNativeThreadId id)
{
	MonoThreadHazardPointers *hp = mono_hazard_pointer_get ();

	if (!list_find (&thread_list, hp, id)) {
		mono_hazard_pointer_clear_all (hp, -1);
		return NULL;
	} 

	mono_hazard_pointer_clear_all (hp, 1);
	return mono_hazard_pointer_get_val (hp, 1);
}

static gboolean
mono_thread_info_insert (MonoThreadInfo *info)
{
	MonoThreadHazardPointers *hp = mono_hazard_pointer_get ();

	if (!list_insert (&thread_list, hp, info)) {
		mono_hazard_pointer_clear_all (hp, -1);
		return FALSE;
	} 

	mono_hazard_pointer_clear_all (hp, -1);
	return TRUE;
}

static gboolean
mono_thread_info_remove (MonoThreadInfo *info)
{
	/*TLS is gone by now, so we can't rely on it to retrieve hp*/
	MonoThreadHazardPointers *hp = mono_hazard_pointer_get_by_id (info->small_id);
	printf ("removing info %p\n", info);
	gboolean res = list_remove (&thread_list, hp, info);
	mono_hazard_pointer_clear_all (hp, -1);
	return res;
}

static void
free_thread_info (gpointer mem)
{
	MonoThreadInfo *info = mem;

	MONO_SEM_DESTROY (&info->resume_semaphore);
	MONO_SEM_DESTROY (&info->suspend_semaphore);
	MONO_SEM_DESTROY (&info->finish_resume_semaphore);
	pthread_mutex_destroy (&info->suspend_lock);

	g_free (info);
}

static void*
register_thread (MonoThreadInfo *info, gpointer baseptr)
{
	info->tid = pthread_self ();
	info->small_id = mono_thread_small_id_alloc ();
	info->thread_state = STATE_RUNNING;

	printf ("registering info %p tid %p small id %x\n", info, info->tid, info->small_id);
	pthread_mutex_init (&info->suspend_lock, NULL);
	MONO_SEM_INIT (&info->suspend_semaphore, 0);
	MONO_SEM_INIT (&info->resume_semaphore, 0);
	MONO_SEM_INIT (&info->finish_resume_semaphore, 0);

	if (threads_callbacks.thread_register) {
		if (threads_callbacks.thread_register (info, baseptr) == NULL) {
			g_warning ("thread registation failed\n");
			g_free (info);
			return NULL;
		}
	}

	pthread_setspecific (thread_info_key, info);

	/*If this fail it means a given thread is been registered twice, which doesn't make sense. */
	g_assert (mono_thread_info_insert (info));
	return info;
}

static void
unregister_thread (void *arg)
{
	MonoThreadInfo *info = arg;
	int small_id = info->small_id;
	g_assert (info);

	printf ("unregistering info %p\n", info);

	pthread_mutex_lock (&info->suspend_lock);
	info->thread_state = STATE_SHUTING_DOWN;
	mono_memory_barrier (); /*signal we began to cleanup.*/

	/* If a delegate is passed to native code and invoked on a thread we dont
	 * know about, the jit will register it with mono_jit_thead_attach, but
	 * we have no way of knowing when that thread goes away.  SGen has a TSD
	 * so we assume that if the domain is still registered, we can detach
	 * the thread
	 */
	if (threads_callbacks.thread_unregister)
		threads_callbacks.thread_unregister (info);

	pthread_mutex_unlock (&info->suspend_lock);

	g_assert (mono_thread_info_remove (info));
	mono_thread_small_id_free (small_id);

	info->thread_state = STATE_DEAD;
	mono_memory_barrier ();
}

static void*
inner_start_thread (void *arg)
{
	ThreadStartInfo *start_info = arg;
	MonoThreadInfo* info;
	void *t_arg = start_info->arg;
	void *(*start_func) (void*) = start_info->start_routine;
	void *result;
	int post_result;

	info = g_malloc0 (thread_info_size);

	register_thread (info, &post_result);

	post_result = MONO_SEM_POST (&(start_info->registered));
	g_assert (!post_result);
	result = start_func (t_arg);
	g_assert (!mono_domain_get ());

	return result;
}

MonoThreadInfo*
mono_thread_info_attach (void *baseptr)
{
	MonoThreadInfo *info = pthread_getspecific (thread_info_key);
	if (!info) {
		info = g_malloc0 (thread_info_size);
		if (!register_thread (info, baseptr))
			return NULL;
	} else if (threads_callbacks.thread_attach) {
		threads_callbacks.thread_attach (info);
	}
	return info;
}

static void
mono_posix_add_signal_handler (int signo, gpointer handler)
{
	/*FIXME, move the code from mini to utils and do the right thing!*/
	struct sigaction sa;
	struct sigaction previous_sa;

	sa.sa_sigaction = handler;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;

	g_assert (sigaction (signo, &sa, &previous_sa) != -1);
}


void
mono_threads_init (MonoThreadInfoCallbacks *callbacks, size_t info_size)
{
	threads_callbacks = *callbacks;
	thread_info_size = info_size;
	pthread_key_create (&thread_info_key, unregister_thread);

	mono_posix_add_signal_handler (SIGUSR2, suspend_signal_handler);

	mono_thread_smr_init ();
}

void
mono_threads_runtime_init (MonoThreadInfoRuntimeCallbacks *callbacks)
{
	runtime_callbacks = *callbacks;
}

int
mono_threads_pthread_create (pthread_t *new_thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
	ThreadStartInfo *start_info;
	int result;

	start_info = g_malloc0 (sizeof (ThreadStartInfo));
	if (!start_info)
		return ENOMEM;
	MONO_SEM_INIT (&(start_info->registered), 0);
	start_info->arg = arg;
	start_info->start_routine = start_routine;

	result = pthread_create (new_thread, attr, inner_start_thread, start_info);
	if (result == 0) {
		while (MONO_SEM_WAIT (&(start_info->registered)) != 0) {
			/*if (EINTR != errno) ABORT("sem_wait failed"); */
		}
	}
	MONO_SEM_DESTROY (&(start_info->registered));
	g_free (start_info);
	return result;
}

static void
suspend_signal_handler (int _dummy, siginfo_t *info, void *context)
{
	MonoThreadInfo *current = mono_thread_info_current ();

	printf ("in suspend sighandler\n");

	g_assert (runtime_callbacks.thread_state_init_from_sigctx (&current->suspend_state, context));
	MONO_SEM_POST (&current->suspend_semaphore);
	while (MONO_SEM_WAIT (&current->resume_semaphore) != 0) {
		/*if (EINTR != errno) ABORT("sem_wait failed"); */
	}
	MONO_SEM_POST (&current->resume_semaphore);

	if (current->async_target) {
		MonoContext tmp = current->suspend_state.ctx;
		runtime_callbacks.setup_async_callback (&tmp, current->async_target, current->user_data);
		mono_monoctx_to_sigctx (&tmp, context);
	}
	MONO_SEM_POST (&current->finish_resume_semaphore);
	printf ("RESUMING %p\n", current->async_target);
}

void
mono_thread_info_setup_async_call (MonoThreadInfo *info, void (*target_func)(void*), void *user_data)
{
	g_assert (info->suspend_count);
	/*FIXME this is a bad assert, we should prove proper locking and fail if one is already set*/
	g_assert (!info->async_target);
	info->async_target = target_func;
	/* This is not GC tracked */
	info->user_data = user_data;
}

/*
The return value is only valid until a matching mono_thread_info_resume is called
*/
MonoThreadInfo*
mono_thread_info_suspend_sync (MonoNativeThreadId tid)
{
	MonoThreadHazardPointers *hp = mono_hazard_pointer_get ();	
	MonoThreadInfo *info = mono_thread_info_lookup (tid); /*info on HP1*/
	if (!info)
		return NULL;

	pthread_mutex_lock (&info->suspend_lock);

	/*thread is on the process of detaching*/
	if (info->thread_state > STATE_RUNNING) {
		mono_hazard_pointer_clear (hp, 1);
		return NULL;
	}

	printf ("suspend %x IN COUNT %d\n", tid, info->suspend_count);

	if (info->suspend_count) {
		++info->suspend_count;
		mono_hazard_pointer_clear (hp, 1);
		pthread_mutex_unlock (&info->suspend_lock);
		return info;
	}

	pthread_kill (tid, SIGUSR2);
	while (MONO_SEM_WAIT (&info->suspend_semaphore) != 0) {
		g_assert (errno == EINTR);
	}
	++info->suspend_count;
	pthread_mutex_unlock (&info->suspend_lock);
	mono_hazard_pointer_clear (hp, 1);

	return info;
}

gboolean
mono_thread_info_resume (MonoNativeThreadId tid)
{
	MonoThreadHazardPointers *hp = mono_hazard_pointer_get ();	
	MonoThreadInfo *info = mono_thread_info_lookup (tid); /*info on HP1*/
	if (!info)
		return FALSE;

	pthread_mutex_lock (&info->suspend_lock);

	printf ("resume %x IN COUNT %d\n",tid, info->suspend_count);

	if (info->suspend_count <= 0) {
		pthread_mutex_unlock (&info->suspend_lock);
		mono_hazard_pointer_clear (hp, 1);
		return FALSE;
	}

	/*
	 * The theory here is that if we manage to suspend the thread it means it did not
	 * start cleanup since it take the same lock. 
	*/
	g_assert (info->tid);

	if (--info->suspend_count == 0) {
		MONO_SEM_POST (&info->resume_semaphore);
		while (MONO_SEM_WAIT (&info->finish_resume_semaphore) != 0) {
			g_assert (errno == EINTR);
		}
	}


	pthread_mutex_unlock (&info->suspend_lock);
	mono_hazard_pointer_clear (hp, 1);

	return TRUE;
}

MonoThreadInfo**
mono_thread_info_list_head (void)
{
	return &thread_list;
}