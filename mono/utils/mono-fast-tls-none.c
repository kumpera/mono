/*
* mono-tls-unknown.c: Low-level fast thread local storage when none is available
 *
 * Author:
 *	Rodrigo Kumpera (kumpera@gmail.com)
 *
 * Copyright 2013 Xamarin, Inc (http://www.xamarin.com)
 */

#include <mono/utils/mono-tls.h>

#if defined(FAST_TLS_MODEL_NONE)

#include <glib.h>
#include <mono/utils/mono-compiler.h>
#include <mono/utils/mono-tls.h>
#include <mono/utils/mono-threads.h>
#include <mono/utils/atomic.h>

void*
mono_tls_get (MonoTlsKey key)
{
	return mono_thread_info_current ()->tls [key];

}
void
mono_tls_set (MonoTlsKey key, void *value)
{
	mono_thread_info_current ()->tls [key] = value;
}

void*
mono_tls_get_address (MonoTlsKey key)
{
	return &mono_thread_info_current ()->tls [key];
}

int
mono_tls_get_offset (MonoTlsKey key)
{
	g_error ("current tls model doesn't support fast access");
	return -1;
}

int
mono_tls_get_model (void)
{
	return MONO_FAST_TLS_MODEL_NONE;
}

int
mono_tls_block_get_offset (void)
{
	g_error ("current tls model cannot calculate the address of the TLS block");
}

void**
mono_tls_block_get_address (void)
{
	return &mono_thread_info_current ()->tls [0];
}

void
mono_tls_real_init (void)
{
}

#endif