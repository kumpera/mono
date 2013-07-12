/*
 * mach-support-x86.c: mach support for x86
 *
 * Authors:
 *   Geoff Norton (gnorton@novell.com)
 *
 * (C) 2010 Ximian, Inc.
 */

#include <config.h>
#if defined(__MACH__)
#include <glib.h>
#include <mach/mach.h>
#include <mach/task.h>
#include <mach/mach_port.h>
#include <mach/thread_act.h>
#include <mach/thread_status.h>

#include <mono/metadata/appdomain.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-debug.h>
#include <mono/utils/mono-mmap.h>

#include "mach-support.h"

kern_return_t
mono_mach_get_threads (thread_act_array_t *threads, guint32 *count)
{
	kern_return_t ret;

	do {
		ret = task_threads (current_task (), threads, count);
	} while (ret != KERN_SUCCESS);

	return ret;
}

kern_return_t
mono_mach_free_threads (thread_act_array_t threads, guint32 count)
{
	return vm_deallocate(current_task (), (vm_address_t) threads, sizeof (thread_t) * count);
}

static gint32 tls_vector_offset, tls_get_offset;

static void
probe_tls_vector_offset (void)
{
	gint32 i;
	pthread_key_t key;
	void *old_value;
	void *canary;
	const gint32 *probe_offsets;
	gint32 offset_count;

	pthread_key_create (&key, NULL);
	old_value = pthread_getspecific (key);
	canary = (void*)0xDEADBEEFu;

	mono_mach_arch_get_tls_probe_offsets (&probe_offsets, &offset_count);
	pthread_key_create (&key, NULL);
	g_assert (old_value != canary);

	pthread_setspecific (key, canary);


	for (i = 0; i < offset_count; ++i) {
		tls_vector_offset = probe_offsets [i];
		if (mono_mach_arch_get_tls_value_from_thread (pthread_self (), key) == canary)
			goto ok;
	}
	tls_vector_offset = -1;

ok:
	pthread_setspecific (key, old_value);
	pthread_key_delete (key);	
}


void *
mono_mach_get_tls_address_from_thread (pthread_t thread, pthread_key_t key)
{
	/* OSX stores TLS values in a hidden array inside the pthread_t structure
	 * They are keyed off a giant array from a known offset into the pointer.  This value
	 * is baked into their pthread_getspecific implementation
	 */
	intptr_t *p = (intptr_t *) thread;
	intptr_t **tsd = (intptr_t **) ((char*)p + tls_vector_offset);

	return (void *) &tsd [key];	
}

void *
mono_mach_arch_get_tls_value_from_thread (pthread_t thread, guint32 key)
{
	return *(void**)mono_mach_get_tls_address_from_thread (thread, key);
}


void
mono_mach_init (void)
{
	probe_tls_vector_offset ();
	tls_get_offset = mono_mach_arch_probe_local_tls_offset ();
}
gint32
mono_mach_get_pthread_offset (void)
{
	return tls_vector_offset;
}

gint32
mono_mach_get_local_tls_offset (void)
{
	return tls_get_offset;
}

#endif
