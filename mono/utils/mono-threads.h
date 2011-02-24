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

typedef struct _MonoThreadInfo MonoThreadInfo;

typedef pthread_t MonoNativeThreadId;

#define mono_native_thread_id_get pthread_self
#define mono_native_thread_id_equals pthread_equal

#ifdef INSIDE_GC
typedef struct _SgenThreadInfo SgenThreadInfo;
#define THREAD_TYPE SgenThreadInfo
#else
#define THREAD_TYPE MonoThreadInfo
#endif

struct _MonoThreadInfo {
	THREAD_TYPE *next; /*next thread in the hashtable*/
	MonoNativeThreadId tid; /*threading kit id  (pthread_t on posix)*/
};

typedef void (*mono_thread_info_register_callback)(THREAD_TYPE *info, void *baseaddr);
typedef void (*mono_thread_info_callback)(THREAD_TYPE *info);

typedef struct {
	/*called when a new thread is created or first attached, done inside the GC lock */
	mono_thread_info_register_callback thread_register;
	/*called when the thread is about to die, done inside the GC lock */
	mono_thread_info_callback thread_unregister;
	/*called when the thread is about to die, done outside of the GC lock */
	mono_thread_info_callback thread_unregister_unlocked;
	/*called when attaching subsequent times, done inside the GC lock */
	mono_thread_info_callback thread_attach;
} MonoThreadInfoCallbacks;

#define THREAD_HASH_SIZE 11

/*
Don't use this struct directly, use the macro below.
The thread_table struct is protected by the gc lock.
*/
extern THREAD_TYPE *thread_table [THREAD_HASH_SIZE] MONO_INTERNAL;

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

/*FIXME figure out windows and boehm (eg, use this in place of mono_gc_pthread_create family)*/
int
mono_threads_pthread_create (pthread_t *new_thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) MONO_INTERNAL;

THREAD_TYPE *
mono_thread_info_attach (void *baseptr) MONO_INTERNAL;


/*GC lock must NOT to be held*/
THREAD_TYPE *
mono_thread_info_lookup (MonoNativeThreadId id) MONO_INTERNAL;


/*GC lock must be held*/
THREAD_TYPE *
mono_thread_info_lookup_unsafe (MonoNativeThreadId id) MONO_INTERNAL;

THREAD_TYPE *
mono_thread_info_current (void) MONO_INTERNAL;

#endif /* _MONO_THREADS_H_ */