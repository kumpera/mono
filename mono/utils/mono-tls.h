/*
 * mono-tls.h: Low-level TLS support
 *
 * Author:
 *	Rodrigo Kumpera (kumpera@gmail.com)
 *
 * Copyright 2011 Novell, Inc (http://www.novell.com)
 * Copyright 2011 Xamarin, Inc (http://www.xamarin.com)
 */

#ifndef __MONO_TLS_H__
#define __MONO_TLS_H__

#include <glib.h>

/*
Fast TLS entries used by the runtime.
Always add before TLS_KEY_NUM and dot not change the order.
*/
typedef enum {
	/* mono_thread_internal_current () */
	TLS_KEY_THREAD = 0,
	TLS_KEY_JIT_TLS = 1,
	/* mono_domain_get () */
	TLS_KEY_DOMAIN = 2,
	TLS_KEY_LMF = 3,
	TLS_KEY_SGEN_THREAD_INFO = 4,
	TLS_KEY_SGEN_TLAB_NEXT_ADDR = 5,
	TLS_KEY_SGEN_TLAB_TEMP_END = 6,
	TLS_KEY_BOEHM_GC_THREAD = 7,
	TLS_KEY_LMF_ADDR = 8,
	TLS_KEY_NUM = 9
} MonoTlsKey;

#ifdef HOST_WIN32

#include <windows.h>

#define MonoNativeTlsKey DWORD
#define mono_native_tls_alloc(key,destructor) ((*(key) = TlsAlloc ()) != TLS_OUT_OF_INDEXES && destructor == NULL)
#define mono_native_tls_free TlsFree
#define mono_native_tls_set_value TlsSetValue
#define mono_native_tls_get_value TlsGetValue

#else

#include <pthread.h>

#define MonoNativeTlsKey pthread_key_t
#define mono_native_tls_get_value pthread_getspecific

static inline int
mono_native_tls_alloc (MonoNativeTlsKey *key, void *destructor)
{
	return pthread_key_create (key, (void (*)(void*)) destructor) == 0;
}

static inline void
mono_native_tls_free (MonoNativeTlsKey key)
{
	pthread_key_delete (key);
}

static inline int
mono_native_tls_set_value (MonoNativeTlsKey key, gpointer value)
{
	return !pthread_setspecific (key, value);
}

#endif /* HOST_WIN32 */

/*
Decides which TLS backend is best suited for this configuration.

Given all Fast TLS requires changes to the JIT, we'll leave only
the fallback model enabled.
*/
#if defined (TARGET_MACH) && 0
#define FAST_TLS_MODEL_DARWIN
#elif defined(HAVE_KW_THREAD) && 0
#define FAST_TLS_MODEL_NATIVE
#else
#define FAST_TLS_MODEL_NONE
#endif


/*
The model dictates how much flexibility the JIT and AOT compiler have in terms of available operations.

There are 4 operations that can be done. Under None, no operations are available.

Load a value:
	Available in all modes.
	Under EmulatedIndirect, load the tls block pointer and do an indirect load from there.

Store a value:
	Available in all modes.
	Under EmulatedIndirect, load the tls block pointer and do an indirect store from there.

Load the address of a slot:
	Available in Native.
	Address calculation can be slow under EmulatedDirect.
	Under EmulatedIndirect, load the tls block pointer and offset from there.

Load the address of the TLS block:
	Available in in all modes.
	It's not possible to do it under EmulatedDirect since the slots might not be contiguous

*/
enum {
	/* Fast TLS is either not possible or probing failed. */
	MONO_FAST_TLS_MODEL_NONE,

	/* Fast TLS is done by directly loading the target slot.

	This model means that the compiler supplies a __thread keyword and we know
	how to load/store/address into those blocks.
	*/
	MONO_FAST_TLS_MODEL_NATIVE,

	/* Fast TLS is done by directly loading the target slot.

	Native Fast TLS is not available so we exploit the structure of the threading library
	dynamic TLS implementation (eg: pthread_{get/set}_specific).

	This means we probed successfuly for how to load/store and get the address of those slots.
	*/
	MONO_FAST_TLS_MODEL_EMULATED_DIRECT,

	/* Fast TLS is done by loading the TLS block pointer and then offseting into it. 

	Similar to the emulated direct model, but we could not figure out how to get the
	address of a TLS slot or allocate enough sequenctial slots for the TLS block.
	*/
	MONO_FAST_TLS_MODEL_EMULATED_INDIRECT,
};

/* Access API */

/* Returns the value of @key for the current thread */
void* mono_tls_get (MonoTlsKey key);

/* Set @key to @value for the current thread */
void mono_tls_set (MonoTlsKey key, void *value);

/* Returns the address of @key for the current thread  */
void* mono_tls_get_address (MonoTlsKey key);

/* Returns the address of the TLS block for the current thread  */
void** mono_tls_block_get_address (void);

/* Compiler Helpers */

/*
Implementation guide

JIT x AOT:

The JIT compiler must either implement the specific model, icall to the tls helpers passing or use the same thunks consumed by AOT (the later is the way to go).
The AOT compiler should implement TLS access by calling a thunk that implement the runtime semantics.
*/

/* Returns the TLS model that was probed to be the best one available. */
int mono_tls_get_model (void);

/* Returns the TLS offset to be used when generating code to access the thread block. */
int mono_tls_block_get_offset (void);

/*
Returns the tls offset to be used when generating native code to access @value

The JIT must take this value together with the TLS model to figure out what to generate.
*/
int mono_tls_get_offset (MonoTlsKey key);

/*
Returns the offsets in bytes from the block pointer to @key.

Might be negative.

Under EmulatedDirect it can change between runs.
*/
int mono_tls_block_get_key_offset (MonoTlsKey);

/*Initialize the tls system. Call this before doing any TLS operation.

This function can be called multiple times.
*/
void mono_tls_init (void);

/* Internal init function, don't call it. */
void mono_tls_real_init (void);


/* Legacy */
void mono_tls_key_set_offset (MonoTlsKey key, int offset);
int mono_tls_key_get_offset (MonoTlsKey key);

#endif /* __MONO_TLS_H__ */
