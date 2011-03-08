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
#include <mono/metadata/gc-internal.h>
#include <mono/metadata/appdomain.h>

#include <pthread.h>
#include <errno.h>

/*FIXME make this possibly larger and allow for some rehashing*/

typedef struct {
	void *(*start_routine) (void *);
	void *arg;
	int flags;
	MonoSemType registered;
} ThreadStartInfo;

typedef struct {
	MonoThreadInfo *info;
	void *baseptr;
} RegisterInfo;

static int thread_info_size;
static MonoThreadInfoCallbacks threads_callbacks;
static MonoThreadInfoRuntimeCallbacks runtime_callbacks;
static pthread_key_t thread_info_key;
MonoThreadInfo *thread_table [THREAD_HASH_SIZE];

static void
suspend_signal_handler (int _dummy, siginfo_t *info, void *context);

#define HASH_PTHREAD_T(id) (((unsigned int)(id) >> 4) * 2654435761u)
#define ARCH_THREAD_EQUALS(a,b) pthread_equal (a, b)

SgenThreadInfo*
mono_thread_info_current (void)
{
	return pthread_getspecific (thread_info_key);
}

MonoThreadInfo*
mono_thread_info_lookup_unsafe (MonoNativeThreadId id)
{
	unsigned int hash = HASH_PTHREAD_T (id) % THREAD_HASH_SIZE;
	MonoThreadInfo *info;

	info = thread_table [hash];
	while (info && !ARCH_THREAD_EQUALS (info->tid, id)) {
		info = info->next;
	}
	return info;
}

MonoThreadInfo*
mono_thread_info_lookup (MonoNativeThreadId id)
{
	g_assert (sizeof (MonoNativeThreadId) == sizeof (MonoNativeThreadId));
	return mono_gc_invoke_with_gc_lock ((MonoGCLockedCallbackFunc)mono_thread_info_lookup_unsafe, (gpointer)id);
}

static void*
insert_into_table (void *arg)
{
	MonoThreadInfo *info = arg;
	int hash = HASH_PTHREAD_T (info->tid) % THREAD_HASH_SIZE;
	info->next = thread_table [hash];
	thread_table [hash] = info;
	return NULL;
}

static void*
remove_from_table (void *arg)
{
	int hash;
	MonoThreadInfo *info = arg, *p, *prev = NULL;

	hash = HASH_PTHREAD_T (info->tid) % THREAD_HASH_SIZE;

	p = thread_table [hash];
	while (p != info) {
		prev = p;
		p = p->next;
	}
	if (prev == NULL)
		thread_table [hash] = p->next;
	else
		prev->next = p->next;

	free (info);
	return NULL;
}

static void*
register_thread (void *arg)
{
	RegisterInfo *reginfo = arg;
	MonoThreadInfo *info = reginfo->info;

	info->tid = pthread_self ();
	pthread_mutex_init (&info->suspend_lock, NULL);
	MONO_SEM_INIT (&info->suspend_semaphore, 0);
	MONO_SEM_INIT (&info->resume_semaphore, 0);

	if (threads_callbacks.thread_register) {
		if (threads_callbacks.thread_register (info, reginfo->baseptr) == NULL) {
			printf ("register failed'\n");
			free (info);
			return NULL;
		}
	}

	mono_gc_invoke_with_gc_lock (insert_into_table, info);
	pthread_setspecific (thread_info_key, info);
	return info;
}

static void
unregister_thread (void *arg)
{
	MonoThreadInfo *info = arg;
	g_assert (info);
	/* If a delegate is passed to native code and invoked on a thread we dont
	 * know about, the jit will register it with mono_jit_thead_attach, but
	 * we have no way of knowing when that thread goes away.  SGen has a TSD
	 * so we assume that if the domain is still registered, we can detach
	 * the thread
	 */
	if (threads_callbacks.thread_unregister)
		threads_callbacks.thread_unregister (info);

	mono_gc_invoke_with_gc_lock (remove_from_table, info);
}

static void*
inner_start_thread (void *arg)
{
	RegisterInfo reginfo;
	ThreadStartInfo *start_info = arg;
	MonoThreadInfo* info;
	void *t_arg = start_info->arg;
	void *(*start_func) (void*) = start_info->start_routine;
	void *result;
	int post_result;

	info = malloc (thread_info_size);
	memset (info, 0, thread_info_size);

	reginfo.info = info;
	reginfo.baseptr = &post_result;
	register_thread (&reginfo);

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
	RegisterInfo reginfo = { info, baseptr };
	if (!info) {
		reginfo.info = info = malloc (thread_info_size);
		memset (info, 0, thread_info_size);
		if (!register_thread (&reginfo))
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

	start_info = malloc (sizeof (ThreadStartInfo));
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
	free (start_info);
	return result;
}

static void
suspend_signal_handler (int _dummy, siginfo_t *info, void *context)
{
	MonoThreadInfo *current = mono_thread_info_current ();

	printf ("in suspend sighandler\n");
	//current->thread_context_modified = FALSE;
	mono_sigctx_to_monoctx (context, &current->thread_context);


	/*FIXME Move this out and/or implement remote TLS read on all targets.*/
	current->domain = mono_domain_get ();

	MONO_SEM_POST (&current->suspend_semaphore);
	while (MONO_SEM_WAIT (&current->resume_semaphore) != 0) {
		/*if (EINTR != errno) ABORT("sem_wait failed"); */
	}

	if (current->async_target) {
		MonoContext tmp = current->thread_context;
		runtime_callbacks.setup_async_callback (&tmp, current->async_target, current->user_data);
		mono_monoctx_to_sigctx (&tmp, context);
	}
}

static void*
suspend_thread_sync (void *arg)
{
	MonoNativeThreadId tid = (MonoNativeThreadId)arg;
	MonoThreadInfo *info = mono_thread_info_lookup_unsafe (tid);
	if (!info)
		return NULL;

	pthread_mutex_lock (&info->suspend_lock);
	if (info->suspend_count) {
		++info->suspend_count;
		pthread_mutex_unlock (&info->suspend_lock);
		return info;
	}

	pthread_kill (tid, SIGUSR2);
	while (MONO_SEM_WAIT (&info->suspend_semaphore) != 0) {
		g_assert (errno == EINTR);
	}
	++info->suspend_count;
	pthread_mutex_unlock (&info->suspend_lock);

	return info;
}

static void*
resume_thread (void *arg)
{
	MonoNativeThreadId tid = (MonoNativeThreadId)arg;
	MonoThreadInfo *info = mono_thread_info_lookup_unsafe (tid);
	if (!info)
		return NULL;

	g_assert (info->suspend_count);
	pthread_mutex_lock (&info->suspend_lock);
	if (--info->suspend_count == 0)
		MONO_SEM_POST (&info->resume_semaphore);

	pthread_mutex_unlock (&info->suspend_lock);
	return info;
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

MonoThreadInfo*
mono_thread_info_suspend_sync (MonoNativeThreadId tid)
{
	return mono_gc_invoke_with_gc_lock (suspend_thread_sync, tid);
}

gboolean
mono_thread_info_resume (MonoNativeThreadId tid)
{
	return mono_gc_invoke_with_gc_lock (resume_thread, tid) != NULL;
}
