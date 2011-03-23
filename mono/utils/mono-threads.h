/*
 * mono-threads.h: Low-level threading
 *
 * Author:
 *	Rodrigo Kumpera (kumpera@gmail.com)
 *
 * (C) 2011 Novell, Inc
 */

#ifndef _MONO_THREADS_H_
#define _MONO_THREADS_H_

#include <mono/utils/mono-semaphore.h>
#include <mono/utils/mono-stack-unwinding.h>

#include <pthread.h>
#include <glib.h>

typedef struct _MonoThreadInfo MonoThreadInfo;

typedef pthread_t MonoNativeThreadId;

#define mono_native_thread_id_get pthread_self
#define mono_native_thread_id_equals pthread_equal

#ifndef THREAD_INFO_TYPE
#define THREAD_INFO_TYPE MonoThreadInfo
#endif

enum {
	STATE_RUNNING,
	STATE_SHUTING_DOWN,
	STATE_DEAD
};

struct _MonoThreadInfo {
	THREAD_INFO_TYPE *next; /*next thread in the hashtable*/
	MonoNativeThreadId tid; /*threading kit id  (pthread_t on posix)*/
	guint32 small_id; /*Used by hazard pointers */
	int thread_state; /*must only be changed by the owner thread*/

	/* suspend machinery, fields protected by the suspend_lock */
	pthread_mutex_t suspend_lock;
	int suspend_count;
	MonoSemType suspend_semaphore;
	MonoSemType resume_semaphore;
	MonoSemType finish_resume_semaphore;

	/*Only needed on posix, only valid if the thread finished suspending*/
	MonoThreadUnwindState suspend_state;

	/*async call machinery, thread MUST be suspended for this to be usable*/
	void (*async_target)(void*);
	void *user_data;
};

typedef struct {
	void* (*thread_register)(THREAD_INFO_TYPE *info, void *baseaddr);
	void (*thread_unregister)(THREAD_INFO_TYPE *info);
	void (*thread_attach)(THREAD_INFO_TYPE *info);
} MonoThreadInfoCallbacks;

typedef void (*mono_thread_info_setup_async_callback) (MonoContext *ctx, void (*async_cb)(void *fun), gpointer user_data);

typedef struct {
	void (*setup_async_callback) (MonoContext *ctx, void (*async_cb)(void *fun), gpointer user_data);
	gboolean (*thread_state_init_from_sigctx) (MonoThreadUnwindState *state, void *sigctx);
} MonoThreadInfoRuntimeCallbacks;

static inline gpointer
list_pointer_unmask (gpointer p)
{
	return (gpointer)((uintptr_t)p & ~(uintptr_t)0x3);
}

static inline uintptr_t
list_pointer_get_mark (gpointer n)
{
	return (uintptr_t)n & 0x1;
}

/*
Requires the world to be stoped
*/
#define FOREACH_THREAD(thread) {\
	thread = (typeof(thread))*mono_thread_info_list_head ();	\
	for (; thread; thread = (typeof(thread)) list_pointer_unmask (((MonoThreadInfo*)(thread))->next))	\
		if (!list_pointer_get_mark (((MonoThreadInfo*)(thread))->next)) {

#define END_FOREACH_THREAD }}

/*
 * @thread_info_size is sizeof (GcThreadInfo), a struct the GC defines to make it possible to have
 * a single block with info from both camps. 
 */
void
mono_threads_init (MonoThreadInfoCallbacks *callbacks, size_t thread_info_size) MONO_INTERNAL;

void
mono_threads_runtime_init (MonoThreadInfoRuntimeCallbacks *callbacks) MONO_INTERNAL;

/*FIXME figure out windows and boehm (eg, use this in place of mono_gc_pthread_create family)*/
int
mono_threads_pthread_create (pthread_t *new_thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) MONO_INTERNAL;

THREAD_INFO_TYPE *
mono_thread_info_attach (void *baseptr) MONO_INTERNAL;

/*Doesn't care about the GC lock*/
THREAD_INFO_TYPE *
mono_thread_info_current (void) MONO_INTERNAL;

MonoThreadInfo*
mono_thread_info_suspend_sync (MonoNativeThreadId tid) MONO_INTERNAL;

gboolean
mono_thread_info_resume (MonoNativeThreadId tid) MONO_INTERNAL;

void
mono_thread_info_setup_async_call (MonoThreadInfo *info, void (*target_func)(void*), void *user_data) MONO_INTERNAL;

MonoThreadInfo**
mono_thread_info_list_head (void) MONO_INTERNAL;
#endif /* _MONO_THREADS_H_ */