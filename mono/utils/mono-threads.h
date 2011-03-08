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
#include <mono/utils/mono-context.h>

#include <pthread.h>

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
	int thread_state; /*must only be changed by the owner thread*/

	/* suspend machinery, fields protected by the suspend_lock */
	pthread_mutex_t suspend_lock;
	int suspend_count;
	MonoSemType suspend_semaphore;
	MonoSemType resume_semaphore;

	/*Only needed on posix, only valid if the thread finished suspending*/
	MonoContext thread_context;
	void *domain;

	/*async call machinery, thread MUST be suspended for this to be usable*/
	void (*async_target)(void*);
	void *user_data;
};

typedef void* (*mono_thread_info_register_callback)(THREAD_INFO_TYPE *info, void *baseaddr);
typedef void (*mono_thread_info_callback)(THREAD_INFO_TYPE *info);

typedef struct {
	mono_thread_info_register_callback thread_register;
	mono_thread_info_callback thread_unregister;
	mono_thread_info_callback thread_attach;
} MonoThreadInfoCallbacks;


typedef void (*mono_thread_info_setup_async_callback) (MonoContext *ctx, void (*async_cb)(void *fun), gpointer user_data);

typedef struct {
	mono_thread_info_setup_async_callback setup_async_callback;
} MonoThreadInfoRuntimeCallbacks;

#define THREAD_HASH_SIZE 11

/*
Don't use this struct directly, use the macro below.
The thread_table struct is protected by the gc lock.
*/
extern THREAD_INFO_TYPE *thread_table [THREAD_HASH_SIZE] MONO_INTERNAL;

/*Assumes gc lock is held.*/
#define FOREACH_THREAD(thread) {\
	int __i;	\
	for (__i = 0; __i < THREAD_HASH_SIZE; ++__i)	\
		for ((thread) = thread_table [__i]; (thread); (thread) = ((MonoThreadInfo *)thread)->next) {

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


/*GC lock must NOT to be held*/
THREAD_INFO_TYPE *
mono_thread_info_lookup (MonoNativeThreadId id) MONO_INTERNAL;


/*GC lock must be held*/
THREAD_INFO_TYPE *
mono_thread_info_lookup_unsafe (MonoNativeThreadId id) MONO_INTERNAL;

/*Doesn't care about the GC lock*/
THREAD_INFO_TYPE *
mono_thread_info_current (void) MONO_INTERNAL;

MonoThreadInfo*
mono_thread_info_suspend_sync (MonoNativeThreadId tid) MONO_INTERNAL;

gboolean
mono_thread_info_resume (MonoNativeThreadId tid) MONO_INTERNAL;

void
mono_thread_info_setup_async_call (MonoThreadInfo *info, void (*target_func)(void*), void *user_data) MONO_INTERNAL;

#endif /* _MONO_THREADS_H_ */