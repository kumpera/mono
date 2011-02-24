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
static pthread_key_t thread_info_key;
MonoThreadInfo *thread_table [THREAD_HASH_SIZE];

#define HASH_PTHREAD_T(id) (((unsigned int)(id) >> 4) * 2654435761u)
#define ARCH_THREAD_EQUALS(a,b) pthread_equal (a, b)

SgenThreadInfo*
mono_thread_info_current (void)
{
#ifdef HAVE_KW_THREAD
	return thread_info;
#else
	return pthread_getspecific (thread_info_key);
#endif
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
register_thread (void *arg)
{
	RegisterInfo *reginfo = arg;
	MonoThreadInfo *info = reginfo->info;
	int hash;

	info->tid = pthread_self ();
	
	if (threads_callbacks.thread_register)
		threads_callbacks.thread_register (info, reginfo->baseptr);

	hash = HASH_PTHREAD_T (info->tid) % THREAD_HASH_SIZE;
	info->next = thread_table [hash];
	thread_table [hash] = info;

	pthread_setspecific (thread_info_key, info);
	return NULL;
}

static void*
cleanup_thread (void *arg)
{
	int hash;
	MonoThreadInfo *info = arg, *p, *prev = NULL;

	if (threads_callbacks.thread_unregister)
		threads_callbacks.thread_unregister (info);


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
	if (threads_callbacks.thread_unregister_unlocked)
		threads_callbacks.thread_unregister_unlocked (info);

	mono_gc_invoke_with_gc_lock (cleanup_thread, info);
}

static void*
attach_thread (void *baseptr)
{
	MonoThreadInfo *info = mono_thread_info_lookup_unsafe (pthread_self ());
	RegisterInfo reginfo = { info, baseptr };
	if (!info) {
		reginfo.info = info = malloc (thread_info_size);
		memset (info, 0, thread_info_size);
		register_thread (&reginfo);
	} else if (threads_callbacks.thread_attach) {
		threads_callbacks.thread_attach (info);
	}
	return info;
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
	mono_gc_invoke_with_gc_lock (register_thread, &reginfo);

	post_result = MONO_SEM_POST (&(start_info->registered));
	g_assert (!post_result);
	result = start_func (t_arg);
	g_assert (!mono_domain_get ());

	return result;
}

MonoThreadInfo*
mono_thread_info_attach (void *baseptr)
{
	return mono_gc_invoke_with_gc_lock (attach_thread, baseptr);
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

void
mono_threads_init (MonoThreadInfoCallbacks *callbacks, size_t info_size)
{
	threads_callbacks = *callbacks;
	thread_info_size = info_size;
	pthread_key_create (&thread_info_key, unregister_thread);
}