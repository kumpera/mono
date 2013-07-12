/*
 * mono-tls-native.c: Low-level thread fast local storage that uses __thread
 *
 * Author:
 *	Rodrigo Kumpera (kumpera@gmail.com)
 *
 * Copyright 2013 Xamarin, Inc (http://www.xamarin.com)
 */


#include <mono/utils/mono-fast-tls.h>

#if defined(FAST_TLS_MODEL_NATIVE)

#include <glib.h>
#include <mono/utils/mono-compiler.h>
#include <mono/utils/mono-tls.h>
#include <mono/utils/mono-threads.h>
#include <mono/utils/atomic.h>

static __thread void *mono_tls_block [MONO_TLS_KEY_COUNT];

void*
mono_tls_get (MonoFastTlsKey key)
{
	return tls_block [key];
}

void
mono_tls_set (MonoFastTlsKey key, void *value)
{
	mono_tls_block [key] = value;
}

void*
mono_tls_get_address (MonoFastTlsKey key)
{
	return &tls_block [key];
}

gint32
mono_tls_get_offset (MonoFastTlsKey key)
{
	int off = -1;
	if (key < 0)
		return key;

	g_assert (key >= 0 && key < MONO_TLS_KEY_COUNT);

	MONO_THREAD_VAR_OFFSET (mono_tls_block, off);
	if (off == -1)
		return -1;
	return off + sizeof (void*) * key;
}

gint32
mono_tls_get_model (void)
{
	return MONO_FAST_TLS_MODEL_NATIVE;
}

gint32
mono_tls_block_get_offset (void)
{
	return mono_tls_get_tls_offset (0);
}

void**
mono_tls_block_get_address (void)
{
	return &mono_tls_block[0];
}

MonoFastTlsKey
mono_tls_translate_native_offset (gint32 native_offset)
{
	return -native_offset;
}

static void
mono_tls_real_init (void)
{
}


#endif