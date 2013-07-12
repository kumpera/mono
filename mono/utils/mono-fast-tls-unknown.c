/*
* mono-tls-unknown.c: Low-level fast thread local storage when none is available
 *
 * Author:
 *	Rodrigo Kumpera (kumpera@gmail.com)
 *
 * Copyright 2013 Xamarin, Inc (http://www.xamarin.com)
 */

#include <mono/utils/mono-fast-tls.h>

#if defined(FAST_TLS_MODEL_NONE)

#include <glib.h>
#include <mono/utils/mono-compiler.h>
#include <mono/utils/mono-tls.h>
#include <mono/utils/mono-threads.h>
#include <mono/utils/atomic.h>

void*
mono_tls_get (MonoFastTlsKey key)
{
	return mono_thread_info_current ()->tls_block [key];

}
void
mono_tls_set (MonoFastTlsKey key, void *value)
{
	mono_thread_info_current ()->tls_block [key] = value;
}

void*
mono_tls_get_address (MonoFastTlsKey key)
{
	return &mono_thread_info_current ()->tls_block [key];
}

gint32
mono_tls_get_fast_tls_offset (MonoFastTlsKey key)
{
	g_assert (key >= 0 && key < MONO_TLS_KEY_COUNT);
	return -1;
}

gint32
mono_tls_get_fast_tls_model (void)
{
	return MONO_FAST_TLS_MODEL_NONE;
}

gint32
mono_tls_get_fast_tls_block_offset (void)
{
	g_error ("current tls model cannot calculate the address of the TLS block");
}

void**
mono_tls_get_block_address (void)
{
	return &mono_thread_info_current ()->tls_block [0];
}

MonoFastTlsKey
mono_tls_translate_native_offset (gint32 native_offset)
{
	g_error ("current tls model cannot translate offsets");
}

void
mono_tls_real_init (void)
{
}

#endif