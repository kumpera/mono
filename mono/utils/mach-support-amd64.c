/*
 * mach-support-x86.c: mach support for x86
 *
 * Authors:
 *   Geoff Norton (gnorton@novell.com)
 *   Rodrigo Kumpera (kumpera@gmail.com)
 *
 * (C) 2010 Novell, Inc.
 * (C) 2013 Xamarin, Inc.
 */

#include <config.h>

#if defined(__MACH__)
#include <stdint.h>
#include <glib.h>
#include <pthread.h>
#include "utils/mono-sigcontext.h"
#include "mach-support.h"

/* Known offsets used for TLS storage*/
static const int known_tls_offsets[] = {
	0x60, /* All OSX versions up to 10.8 */
	0xe0, /* 10.9 and up */
};

void *
mono_mach_arch_get_ip (thread_state_t state)
{
	x86_thread_state64_t *arch_state = (x86_thread_state64_t *) state;

	return (void *) arch_state->__rip;
}

void *
mono_mach_arch_get_sp (thread_state_t state)
{
	x86_thread_state64_t *arch_state = (x86_thread_state64_t *) state;

	return (void *) arch_state->__rsp;
}

int
mono_mach_arch_get_mcontext_size ()
{
	return sizeof (struct __darwin_mcontext64);
}

void
mono_mach_arch_thread_state_to_mcontext (thread_state_t state, void *context)
{
	x86_thread_state64_t *arch_state = (x86_thread_state64_t *) state;
	struct __darwin_mcontext64 *ctx = (struct __darwin_mcontext64 *) context;

	ctx->__ss = *arch_state;
}

void
mono_mach_arch_mcontext_to_thread_state (void *context, thread_state_t state)
{
	x86_thread_state64_t *arch_state = (x86_thread_state64_t *) state;
	struct __darwin_mcontext64 *ctx = (struct __darwin_mcontext64 *) context;

	*arch_state = ctx->__ss;
}

int
mono_mach_arch_get_thread_state_size ()
{
	return sizeof (x86_thread_state64_t);
}

kern_return_t
mono_mach_arch_get_thread_state (thread_port_t thread, thread_state_t state, mach_msg_type_number_t *count)
{
	x86_thread_state64_t *arch_state = (x86_thread_state64_t *) state;
	kern_return_t ret;

	*count = x86_THREAD_STATE64_COUNT;

	ret = thread_get_state (thread, x86_THREAD_STATE64, (thread_state_t) arch_state, count);

	return ret;
}

kern_return_t
mono_mach_arch_set_thread_state (thread_port_t thread, thread_state_t state, mach_msg_type_number_t count)
{
	return thread_set_state (thread, x86_THREAD_STATE64, state, count);
}

void
mono_mach_arch_get_tls_probe_offsets (const gint32**offsets, gint32 *count)
{
	*offsets = known_tls_offsets;
	*count = G_N_ELEMENTS (known_tls_offsets);
}

gint32
mono_mach_arch_probe_local_tls_offset (void)
{
	gboolean have_tls_get;
	guint8 *ins = (guint8*)pthread_getspecific;

	/*
	 * We're looking for these two instructions:
	 *
	 * mov    %gs:[offset](,%rdi,8),%rax
	 * retq
	 */
	have_tls_get = ins [0] == 0x65 &&
		       ins [1] == 0x48 &&
		       ins [2] == 0x8b &&
		       ins [3] == 0x04 &&
		       ins [4] == 0xfd &&
		       ins [6] == 0x00 &&
		       ins [7] == 0x00 &&
		       ins [8] == 0x00 &&
		       ins [9] == 0xc3;

	if (!have_tls_get)
		return -1;

	return ins[5];
}

#endif
