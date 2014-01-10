/*
 * test-conc-hashtable.c: Unit test for the concurrent hashtable.
 *
 * Copyright (C) 2013 Xamarin Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License 2.0 as published by the Free Software Foundation;
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License 2.0 along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"

#include "utils/mono-threads.h"
#include "utils/mono-conc-hashtable.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include <pthread.h>

static int
serial_test (void)
{
	mono_mutex_t mutex;
	MonoConcurrentHashTable *h;
	int res = 0;

	mono_mutex_init (&mutex);
	h = mono_conc_hashtable_new (&mutex, NULL, NULL);
	mono_conc_hashtable_insert (h, GUINT_TO_POINTER (10), GUINT_TO_POINTER (20));
	mono_conc_hashtable_insert (h, GUINT_TO_POINTER (30), GUINT_TO_POINTER (40));
	mono_conc_hashtable_insert (h, GUINT_TO_POINTER (50), GUINT_TO_POINTER (60));
	mono_conc_hashtable_insert (h, GUINT_TO_POINTER (2), GUINT_TO_POINTER (3));

	if (mono_conc_hashtable_lookup (h, GUINT_TO_POINTER (30)) != GUINT_TO_POINTER (40))
		res = 1;
	if (mono_conc_hashtable_lookup (h, GUINT_TO_POINTER (10)) != GUINT_TO_POINTER (20))
		res = 1;
	if (mono_conc_hashtable_lookup (h, GUINT_TO_POINTER (2)) != GUINT_TO_POINTER (3))
		res = 1;
	if (mono_conc_hashtable_lookup (h, GUINT_TO_POINTER (50)) != GUINT_TO_POINTER (60))
		res = 1;

done:
	mono_conc_hashtable_destroy (h);
	mono_mutex_destroy (&mutex);
	if (!res)
		printf ("SERIAL TEST FAILED %d\n", res);
	return res;
}

static int
parallel_writers_single_reader (void)
{
	
}

int
main (void)
{
	MonoThreadInfoCallbacks cb = { NULL };
	int res = 0;

	mono_threads_init (&cb, sizeof (MonoThreadInfo));
	mono_thread_info_attach ((gpointer)&cb);
	res |= serial_test ();
	res |= parallel_writers_single_reader ();
	return res;
}
