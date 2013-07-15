/*
 * mono-fast-tls.h: Low-level Fast TLS support
 *
 * Author:
 *	Rodrigo Kumpera (kumpera@gmail.com)
 *
 * Copyright 2011 Novell, Inc (http://www.novell.com)
 * Copyright 2011 Xamarin, Inc (http://www.xamarin.com)
 */

#ifndef __MONO_FAST_TLS_H__
#define __MONO_FAST_TLS_H__

#include <config.h>
#include <mono/utils/mono-tls.h>
#include <mono/utils/mono-compiler.h>

/*
Issues:

Make fast inlines of get/set if KW_THREAD is present.
Add opcodes for getting the address and storing to TLS so we can
get rid of the TLS variables that store addresses for other slots.
*/
/*
Decide which TLS backend is best suited for this configuration.

Given all Fast TLS requires changes to the JIT, we'll leave only
the fallback model enabled.
*/
#if defined (TARGET_MACH)
#define FAST_TLS_MODEL_DARWIN
#elif defined(HAVE_KW_THREAD) && 0
#define FAST_TLS_MODEL_NATIVE
#else
#define FAST_TLS_MODEL_NONE
#endif


/*
Always add before MONO_TLS_KEY_COUNT and not change the order.
*/
enum {
	MONO_TLS_APPDOMAIN_KEY = 0,
	MONO_TLS_LMF_KEY,
	MONO_TLS_LMF_ADDR_KEY,
	MONO_TLS_JIT_TLS_KEY,
	MONO_TLS_THREAD_KEY,
	MONO_TLS_SGEN_TLAB_NEXT_ADDR_KEY,
	MONO_TLS_SGEN_TLAB_TEMP_END_KEY,
	MONO_TLS_KEY_COUNT
};

/*
The model dictates how much flexibility the JIT has in terms of available operations.

There are 4 operations that can be done. Under None, no operations are available.

Load a value:
	Available in all modes.
	Under EmulatedIndirect, load the tls block pointer and do an indirect load from there.

Store a value:
	Available in all modes.
	Under EmulatedIndirect, load the tls block pointer and do an indirect store from there.

Load the address of a slot:
	Available in Native.
	Address calculation can slow under emulated.
	Under EmulatedIndirect, load the tls block pointer and offset from there.

Load the address of the TLS block:
	Available in all modes.
	Address calculation can slow under emulated.
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
	MONO_FAST_TLS_MODEL_EMULATED,

	/* Fast TLS is done by loading the TLS block pointer and then offseting into it. 

	Similar to the emulated model, but we could not figure out how to get the
	address of a TLS slot.
	*/
	MONO_FAST_TLS_MODEL_EMULATED_INDIRECT,
};

typedef int MonoFastTlsKey;

/* Access API */

/* Returns the value of @key for the current thread */
void* mono_tls_get (MonoFastTlsKey key) MONO_INTERNAL;

/* Set @key to @value for the current thread */
void mono_tls_set (MonoFastTlsKey key, void *value) MONO_INTERNAL;

/* Returns the address of @key for the current thread  */
void* mono_tls_get_address (MonoFastTlsKey key) MONO_INTERNAL;

/* Returns the address of the TLS block for the current thread  */
void** mono_tls_get_block_address (void) MONO_INTERNAL;

/* Compiler Helpers */

/* Returns the TLS model that was probed to be the best one available. */
gint32 mono_tls_get_fast_tls_model (void) MONO_INTERNAL;

/* Return the TLS offset to be used when generating code to access the thread block. */
gint32 mono_tls_get_fast_tls_block_offset (void) MONO_INTERNAL;

/*
Returns the tls offset to be used when generating native code to access @value

The JIT must take this value together with the TLS model to figure out what to generate.
*/
gint32 mono_tls_get_fast_tls_offset (MonoFastTlsKey key) MONO_INTERNAL;

/* 
Returns if fast TLS is available for @key.

Some targets, like windows, cannot address all dynamic TLS slots so
probing must be done per-slot.
*/
gboolean mono_tls_is_fast_tls_available (MonoFastTlsKey key) MONO_INTERNAL;

/*
Return a surrogate MonoFastTlsKey for a given native_offset.

Given a native tls offset, return a value that can be used by the compiler with
mono_tls_get_fast_tls_offset/mono_tls_is_fast_tls_available to generate fast code.

The returned value doesn't work with any other functions.

The reasoning for this function is that if you have a __thread variable that cannot
be converted to the new schema but you still want the JIT to emit fast tls for it.
This exists only for boehm's managed allocator, avoid at all costs.

Sample usage pattern:

__thread tls_var;
int offset;
MONO_THREAD_VAR_OFFSET (tls_var, offset);

mono_mb_emit_byte (mb, MONO_CUSTOM_PREFIX);
mono_mb_emit_byte (mb, CEE_MONO_TLS);
mono_mb_emit_i4 (mb, mono_tls_translate_native_offset (offset));

*/
MonoFastTlsKey mono_tls_translate_native_offset (gint32 native_offset) MONO_INTERNAL;

/*Initialize the tls system. Call this before doing any TLS operation.

This function can be called multiple times.
*/
void mono_tls_init (void) MONO_INTERNAL;

/* Internal init function, don't call it. */
void mono_tls_real_init (void) MONO_INTERNAL;

#endif