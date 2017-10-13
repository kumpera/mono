/**
 * \file
 * Microsoft threadpool runtime support
 *
 * Author:
 *	Ludovic Henry (ludovic.henry@xamarin.com)
 *
 * Copyright 2015 Xamarin, Inc (http://www.xamarin.com)
 * Licensed under the MIT license. See LICENSE file in the project root for full license information.
 */

//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
//
// Files:
//  - src/vm/comthreadpool.cpp
//  - src/vm/win32threadpoolcpp
//  - src/vm/threadpoolrequest.cpp
//  - src/vm/hillclimbing.cpp
//
// Ported from C++ to C and adjusted to Mono runtime

#include <stdlib.h>
#define _USE_MATH_DEFINES // needed by MSVC to define math constants
#include <math.h>
#include <config.h>
#include <glib.h>

#include <mono/metadata/class-internals.h>
#include <mono/metadata/exception.h>
#include <mono/metadata/gc-internals.h>
#include <mono/metadata/object.h>
#include <mono/metadata/object-internals.h>
#include <mono/metadata/threadpool.h>
#include <mono/metadata/threadpool-worker.h>
#include <mono/metadata/threadpool-io.h>
#include <mono/metadata/w32event.h>
#include <mono/utils/atomic.h>
#include <mono/utils/mono-compiler.h>
#include <mono/utils/mono-complex.h>
#include <mono/utils/mono-lazy-init.h>
#include <mono/utils/mono-logger.h>
#include <mono/utils/mono-logger-internals.h>
#include <mono/utils/mono-proclib.h>
#include <mono/utils/mono-threads.h>
#include <mono/utils/mono-time.h>
#include <mono/utils/refcount.h>
#include <mono/utils/mono-os-wait.h>
#include <mono/utils/hazard-pointer.h>

//// MonoConcArray temporary place

typedef struct _conc_set_data conc_set_data;

typedef struct {
	volatile conc_set_data *data;
} MonoConcArraySet;

///////
typedef struct {
	MonoRefCount ref;
	MonoDomain *domain;
	/* Number of outstanding jobs */
	volatile gint32 outstanding_request;
	/* Number of currently executing jobs */
	volatile gint32 threadpool_jobs;
	/* index in the tp set */
	gint32 index;
	/* Signalled when threadpool_jobs + outstanding_request is 0 */
	/* Protected by threadpool.domains_lock */
	MonoCoopCond cleanup_cond;
} ThreadPoolDomain;

typedef union {
	struct {
		gint16 starting; /* starting, but not yet in worker_callback */
		gint16 working; /* executing worker_callback */
	} _;
	gint32 as_gint32;
} ThreadPoolCounter;

typedef struct {
	MonoRefCount ref;
	MonoConcArraySet conc_domains;
	MonoCoopMutex domains_lock;

	ThreadPoolCounter counters;

	gint32 limit_io_min;
	gint32 limit_io_max;
} ThreadPool;

static mono_lazy_init_t status = MONO_LAZY_INIT_STATUS_NOT_INITIALIZED;

static ThreadPool threadpool;

#define COUNTER_ATOMIC(var,block) \
	do { \
		ThreadPoolCounter __old; \
		do { \
			(var) = __old = COUNTER_READ (); \
			{ block; } \
			if (!(counter._.starting >= 0)) \
				g_error ("%s: counter._.starting = %d, but should be >= 0", __func__, counter._.starting); \
			if (!(counter._.working >= 0)) \
				g_error ("%s: counter._.working = %d, but should be >= 0", __func__, counter._.working); \
		} while (InterlockedCompareExchange (&threadpool.counters.as_gint32, (var).as_gint32, __old.as_gint32) != __old.as_gint32); \
	} while (0)

static inline ThreadPoolCounter
COUNTER_READ (void)
{
	ThreadPoolCounter counter;
	counter.as_gint32 = InterlockedRead (&threadpool.counters.as_gint32);
	return counter;
}

static void
worker_callback (void);
static void
mono_conc_array_set_init (MonoConcArraySet *set);
static void
mono_conc_array_destroy (MonoConcArraySet *set);

static void
destroy (gpointer unused)
{
	mono_coop_mutex_destroy (&threadpool.domains_lock);
	mono_conc_array_destroy (&threadpool.conc_domains);
}

static void
initialize (void)
{
	g_assert (sizeof (ThreadPoolCounter) == sizeof (gint32));

	mono_refcount_init (&threadpool, destroy);

	mono_coop_mutex_init (&threadpool.domains_lock);
	mono_conc_array_set_init (&threadpool.conc_domains);

	threadpool.limit_io_min = mono_cpu_count ();
	threadpool.limit_io_max = CLAMP (threadpool.limit_io_min * 100, MIN (threadpool.limit_io_min, 200), MAX (threadpool.limit_io_min, 200));

	mono_threadpool_worker_init (worker_callback);
}

static void
cleanup (void)
{
	mono_threadpool_worker_cleanup ();

	mono_refcount_dec (&threadpool);
}

gboolean
mono_threadpool_enqueue_work_item (MonoDomain *domain, MonoObject *work_item, MonoError *error)
{
	static MonoClass *threadpool_class = NULL;
	static MonoMethod *unsafe_queue_custom_work_item_method = NULL;
	MonoDomain *current_domain;
	MonoBoolean f;
	gpointer args [2];

	error_init (error);
	g_assert (work_item);

	if (!threadpool_class)
		threadpool_class = mono_class_load_from_name (mono_defaults.corlib, "System.Threading", "ThreadPool");

	if (!unsafe_queue_custom_work_item_method)
		unsafe_queue_custom_work_item_method = mono_class_get_method_from_name (threadpool_class, "UnsafeQueueCustomWorkItem", 2);
	g_assert (unsafe_queue_custom_work_item_method);

	f = FALSE;

	args [0] = (gpointer) work_item;
	args [1] = (gpointer) &f;

	current_domain = mono_domain_get ();
	if (current_domain == domain) {
		mono_runtime_invoke_checked (unsafe_queue_custom_work_item_method, NULL, args, error);
		return_val_if_nok (error, FALSE);
	} else {
		mono_thread_push_appdomain_ref (domain);
		if (mono_domain_set (domain, FALSE)) {
			mono_runtime_invoke_checked (unsafe_queue_custom_work_item_method, NULL, args, error);
			if (!is_ok (error)) {
				mono_thread_pop_appdomain_ref ();
				return FALSE;
			}
			mono_domain_set (current_domain, TRUE);
		}
		mono_thread_pop_appdomain_ref ();
	}
	return TRUE;
}

//----------------------------------------

#define INITIAL_SIZE 16

struct _conc_set_data {
	volatile size_t len;
	size_t capacity;
	volatile ThreadPoolDomain *data [1];
};

static void
mono_conc_array_set_init (MonoConcArraySet *set)
{
	conc_set_data *data = g_malloc0 ((INITIAL_SIZE + 2) * sizeof (gpointer));
	data->len = 0;
	data->capacity = INITIAL_SIZE;

	mono_atomic_store_release (&set->data, data);
}

static void
mono_conc_array_destroy (MonoConcArraySet *set)
{
	gpointer old = InterlockedExchangePointer ((gpointer)&set->data, NULL);
	if (old)
		mono_thread_hazardous_try_free (old, g_free);
}

static void
tpdomain_real_free (void *arg)
{
	ThreadPoolDomain *tpdomain = arg;
	mono_coop_cond_destroy (&tpdomain->cleanup_cond);
	g_free (tpdomain);
}

static void
tpdomain_queue_destroy (void *domain)
{
	mono_thread_hazardous_try_free (domain, tpdomain_real_free);
}

static void
domains_set_lock (void)
{
	mono_coop_mutex_lock (&threadpool.domains_lock);
}

static void
domains_set_unlock (void)
{
	mono_coop_mutex_unlock (&threadpool.domains_lock);
}

/* LOCKING: domains_set_lock will be acquired. Returns object with RC + 1. Returns NULL if the domain is unloading */
static ThreadPoolDomain *
tpdomain_try_create (MonoDomain *domain)
{

	ThreadPoolDomain *tpdomain;
	ThreadPoolDomain *res = NULL;

	tpdomain = g_new0 (ThreadPoolDomain, 1);
	tpdomain->domain = domain;
	mono_refcount_init (tpdomain, tpdomain_queue_destroy);
	mono_coop_cond_init (&tpdomain->cleanup_cond);
	tpdomain->ref.ref = 2; //1 for the conc set + 1 for the caller


	domains_set_lock ();

	//Must be done under domain_lock to coordinate with remove_domain_jobs
	if (mono_domain_is_unloading (domain))
		goto done;

	//no other mutation happening, we can avoid the conc machinery
	int i;
	conc_set_data *data = (conc_set_data *)threadpool.conc_domains.data;
	for (i = 0; i < data->len; ++i) {
		ThreadPoolDomain *cur = (ThreadPoolDomain *)data->data [i];
		if (cur && cur->domain == domain) {
			/* RC can't drop to zero for items in the domain set while we hold the domains_set_lock as we DEC after removing from the set while holding the same lock */
			g_assert (mono_refcount_tryinc (cur));
			res = cur;
			goto done;
		}
	}

	//ensure we have space
	if (data->len == data->capacity) {
		conc_set_data *new_data = g_malloc0 ((data->capacity * 2 + 2) * sizeof (gpointer));
		new_data->len = data->len;
		new_data->capacity = data->capacity * 2;
		memcpy ((gpointer)new_data->data, (gpointer)data->data, data->len * sizeof (gpointer));

		// all copied data to new_data *must* be visible before we publish it
		mono_atomic_store_release (&threadpool.conc_domains.data, new_data);

		mono_thread_hazardous_try_free (data, g_free);
		data = new_data;
	}

	//insert in the first NULL slow
	for (i = 0; i <= data->len; ++i) {
		if (!data->data [i])
			break;
	}

	//Ensure tpdomain init is fully visible
	tpdomain->index = i;
	mono_atomic_store_release (&data->data [i], tpdomain);
	//stored at the end
	if (i == data->len)
		mono_atomic_store_release (&data->len, data->len + 1);	//The previous store *must* be visible before we bump len

	res = tpdomain;
done:
	domains_set_unlock ();
	if (res != tpdomain && tpdomain)
		tpdomain_real_free (tpdomain);
	return res;
}

/* LOCKING: domains_set_lock will be acquired. If removed, object is RC - 1 */
static gboolean
tpdomain_remove (ThreadPoolDomain *tpdomain)
{
	g_assert (tpdomain);;

	domains_set_lock ();
	int i;
	conc_set_data *data = (conc_set_data *)threadpool.conc_domains.data;

	for (i = 0; i < data->len; ++i) {
		if (data->data [i] == tpdomain) {
			if (i == data->len - 1)
				data->len = data->len - 1;

			//The previous store *must* be visible before we null the value
			mono_atomic_store_release (&data->data [i], NULL);
			g_assert (tpdomain->ref.ref >= 2); //caller owns this object plus the set
			mono_refcount_dec (tpdomain);

			domains_set_unlock ();
			return TRUE;
		}
	}

	domains_set_unlock ();
	return FALSE;
}

static void
tpdomain_unref (ThreadPoolDomain *tpdomain)
{
	mono_refcount_dec (tpdomain);
}

/* Returns object with RC + 1 */
static ThreadPoolDomain *
tpdomain_get (MonoDomain *domain)
{
	g_assert (domain);

	MonoThreadHazardPointers *hp = mono_hazard_pointer_get ();
	ThreadPoolDomain *res = NULL;

	//conc_set_data goes to HP0, ThreadPoolDomain goes to HP1
	int i;
	conc_set_data *data = (conc_set_data *)mono_get_hazardous_pointer ((gpointer volatile*)&threadpool.conc_domains.data, hp, 0);
	for (i = 0; i < data->len; ++i) {
		res = (ThreadPoolDomain *)mono_get_hazardous_pointer ((gpointer volatile*)&data->data[i], hp, 1);
		if (res && res->domain == domain) {
			/* We failed to INC, meaning we raced to another thread deleting it */
			if (!mono_refcount_tryinc (res))
				res = NULL;
			goto done;
		}
	}

done:
	mono_hazard_pointer_clear (hp, 1);
	mono_hazard_pointer_clear (hp, 0);
	return res;
}


/* Returns object with RC + 1 */
static ThreadPoolDomain *
tpdomain_get_next (int prev_index)
{
	ThreadPoolDomain *tpdomain = NULL;

	MonoThreadHazardPointers *hp = mono_hazard_pointer_get ();

	int i;
	conc_set_data *data = (conc_set_data *)mono_get_hazardous_pointer ((gpointer volatile*)&threadpool.conc_domains.data, hp, 0);
	int len = data->len;
	if (len == 0)
		goto done;
	for (i = prev_index + 1; i < len + prev_index + 1; ++i) {
		ThreadPoolDomain *tmp = (ThreadPoolDomain *)mono_get_hazardous_pointer ((gpointer volatile*)&data->data[i % len], hp, 1);
		if (tmp && tmp->outstanding_request > 0 && mono_refcount_tryinc (tmp)) {
			tpdomain = tmp;
			break;
		}
	}

done:
	mono_hazard_pointer_clear (hp, 1);
	mono_hazard_pointer_clear (hp, 0);
	return tpdomain;
}


//----------------------------------------


static MonoObject*
try_invoke_perform_wait_callback (MonoObject** exc, MonoError *error)
{
	HANDLE_FUNCTION_ENTER ();
	error_init (error);
	MonoObject *res = mono_runtime_try_invoke (mono_defaults.threadpool_perform_wait_callback_method, NULL, NULL, exc, error);
	HANDLE_FUNCTION_RETURN_VAL (res);
}

static void
worker_callback (void)
{
	MonoError error;
	ThreadPoolDomain *tpdomain;
	ThreadPoolCounter counter;
	MonoInternalThread *thread;
	int previous_idx = -1;

	if (!mono_refcount_tryinc (&threadpool))
		return;

	thread = mono_thread_internal_current ();

	COUNTER_ATOMIC (counter, {
		if (!(counter._.working < 32767 /* G_MAXINT16 */))
			g_error ("%s: counter._.working = %d, but should be < 32767", __func__, counter._.working);

		counter._.starting --;
		counter._.working ++;
	});

	if (mono_runtime_is_shutting_down ()) {
		COUNTER_ATOMIC (counter, {
			counter._.working --;
		});

		mono_refcount_dec (&threadpool);
		return;
	}

	/*
	 * This is needed so there is always an lmf frame in the runtime invoke call below,
	 * so ThreadAbortExceptions are caught even if the thread is in native code.
	 */
	mono_defaults.threadpool_perform_wait_callback_method->save_lmf = TRUE;

	while (!mono_runtime_is_shutting_down ()) {
		gboolean retire = FALSE;

		if (thread->state & (ThreadState_AbortRequested | ThreadState_SuspendRequested)) {
			if (mono_thread_interruption_checkpoint ())
				continue;
		}

		tpdomain = tpdomain_get_next (previous_idx);
		if (!tpdomain)
			break;

		previous_idx = tpdomain->index;
		gint32 outstanding_request = tpdomain->outstanding_request;
		if (outstanding_request < 1 || InterlockedCompareExchange (&tpdomain->outstanding_request, outstanding_request - 1, outstanding_request) != outstanding_request) {
			tpdomain_unref (tpdomain);
			continue;
		}
		g_assert (outstanding_request >= 0);

		mono_trace (G_LOG_LEVEL_DEBUG, MONO_TRACE_THREADPOOL, "[%p] worker running in domain %p (outstanding requests %d)",
			GUINT_TO_POINTER (MONO_NATIVE_THREAD_ID_TO_UINT (mono_native_thread_id_get ())), tpdomain->domain, tpdomain->outstanding_request);

		g_assert (tpdomain->threadpool_jobs >= 0);
		InterlockedIncrement (&tpdomain->threadpool_jobs);

		if (mono_thread_name_changed (thread)) {
			MonoString *thread_name = mono_string_new_checked (mono_get_root_domain (), "Threadpool worker", &error);
			mono_error_assert_ok (&error);
			mono_thread_set_name_internal (thread, thread_name, FALSE, TRUE, &error);
			mono_error_assert_ok (&error);
		}

		mono_thread_reset_state (thread, ThreadState_Background);

		mono_thread_push_appdomain_ref (tpdomain->domain);
		if (mono_domain_set (tpdomain->domain, FALSE)) {
			MonoObject *exc = NULL, *res;

			res = try_invoke_perform_wait_callback (&exc, &error);
			if (exc || !mono_error_ok(&error)) {
				if (exc == NULL)
					exc = (MonoObject *) mono_error_convert_to_exception (&error);
				else
					mono_error_cleanup (&error);
				mono_thread_internal_unhandled_exception (exc);
			} else if (res && *(MonoBoolean*) mono_object_unbox (res) == FALSE) {
				retire = TRUE;
			}

			mono_domain_set (mono_get_root_domain (), TRUE);
		}
		mono_thread_pop_appdomain_ref ();


		InterlockedDecrement (&tpdomain->threadpool_jobs);
		g_assert (tpdomain->threadpool_jobs >= 0);

		if (tpdomain->outstanding_request + tpdomain->threadpool_jobs == 0 && mono_domain_is_unloading (tpdomain->domain)) {
			gboolean removed;

			domains_set_lock ();
			removed = tpdomain_remove (tpdomain);
			g_assert (removed);

			mono_coop_cond_signal (&tpdomain->cleanup_cond);
			domains_set_unlock ();

			tpdomain_unref (tpdomain);

			tpdomain = NULL;
		} else {
			tpdomain_unref (tpdomain);
		}

		if (retire)
			break;
	}

	COUNTER_ATOMIC (counter, {
		counter._.working --;
	});

	mono_refcount_dec (&threadpool);
}

void
mono_threadpool_cleanup (void)
{
#ifndef DISABLE_SOCKETS
	mono_threadpool_io_cleanup ();
#endif
	mono_lazy_cleanup (&status, cleanup);
}

MonoAsyncResult *
mono_threadpool_begin_invoke (MonoDomain *domain, MonoObject *target, MonoMethod *method, gpointer *params, MonoError *error)
{
	static MonoClass *async_call_klass = NULL;
	MonoMethodMessage *message;
	MonoAsyncResult *async_result;
	MonoAsyncCall *async_call;
	MonoDelegate *async_callback = NULL;
	MonoObject *state = NULL;

	if (!async_call_klass)
		async_call_klass = mono_class_load_from_name (mono_defaults.corlib, "System", "MonoAsyncCall");

	error_init (error);

	message = mono_method_call_message_new (method, params, mono_get_delegate_invoke (method->klass), (params != NULL) ? (&async_callback) : NULL, (params != NULL) ? (&state) : NULL, error);
	return_val_if_nok (error, NULL);

	async_call = (MonoAsyncCall*) mono_object_new_checked (domain, async_call_klass, error);
	return_val_if_nok (error, NULL);

	MONO_OBJECT_SETREF (async_call, msg, message);
	MONO_OBJECT_SETREF (async_call, state, state);

	if (async_callback) {
		MONO_OBJECT_SETREF (async_call, cb_method, mono_get_delegate_invoke (((MonoObject*) async_callback)->vtable->klass));
		MONO_OBJECT_SETREF (async_call, cb_target, async_callback);
	}

	async_result = mono_async_result_new (domain, NULL, async_call->state, NULL, (MonoObject*) async_call, error);
	return_val_if_nok (error, NULL);
	MONO_OBJECT_SETREF (async_result, async_delegate, target);

	mono_threadpool_enqueue_work_item (domain, (MonoObject*) async_result, error);
	return_val_if_nok (error, NULL);

	return async_result;
}

MonoObject *
mono_threadpool_end_invoke (MonoAsyncResult *ares, MonoArray **out_args, MonoObject **exc, MonoError *error)
{
	MonoAsyncCall *ac;

	error_init (error);
	g_assert (exc);
	g_assert (out_args);

	*exc = NULL;
	*out_args = NULL;

	/* check if already finished */
	mono_monitor_enter ((MonoObject*) ares);

	if (ares->endinvoke_called) {
		mono_error_set_invalid_operation(error, "Delegate EndInvoke method called more than once");
		mono_monitor_exit ((MonoObject*) ares);
		return NULL;
	}

	ares->endinvoke_called = 1;

	/* wait until we are really finished */
	if (ares->completed) {
		mono_monitor_exit ((MonoObject *) ares);
	} else {
		gpointer wait_event;
		if (ares->handle) {
			wait_event = mono_wait_handle_get_handle ((MonoWaitHandle*) ares->handle);
		} else {
			wait_event = mono_w32event_create (TRUE, FALSE);
			g_assert(wait_event);
			MonoWaitHandle *wait_handle = mono_wait_handle_new (mono_object_domain (ares), wait_event, error);
			if (!is_ok (error)) {
				mono_w32event_close (wait_event);
				return NULL;
			}
			MONO_OBJECT_SETREF (ares, handle, (MonoObject*) wait_handle);
		}
		mono_monitor_exit ((MonoObject*) ares);
		MONO_ENTER_GC_SAFE;
#ifdef HOST_WIN32
		mono_win32_wait_for_single_object_ex (wait_event, INFINITE, TRUE);
#else
		mono_w32handle_wait_one (wait_event, MONO_INFINITE_WAIT, TRUE);
#endif
		MONO_EXIT_GC_SAFE;
	}

	ac = (MonoAsyncCall*) ares->object_data;
	g_assert (ac);

	*exc = ac->msg->exc; /* FIXME: GC add write barrier */
	*out_args = ac->out_args;
	return ac->res;
}

gboolean
mono_threadpool_remove_domain_jobs (MonoDomain *domain, int timeout)
{
	gint64 end;
	ThreadPoolDomain *tpdomain;
	gboolean ret;

	g_assert (domain);
	g_assert (timeout >= -1);

	g_assert (mono_domain_is_unloading (domain));

	if (timeout != -1)
		end = mono_msec_ticks () + timeout;

#ifndef DISABLE_SOCKETS
	mono_threadpool_io_remove_domain_jobs (domain);
	if (timeout != -1) {
		if (mono_msec_ticks () > end)
			return FALSE;
	}
#endif

	/*
	 * Wait for all threads which execute jobs in the domain to exit.
	 * The is_unloading () check in worker_request () ensures that
	 * no new jobs are added after we enter the lock below.
	 */

	if (!mono_lazy_is_initialized (&status))
		return TRUE;

	mono_refcount_inc (&threadpool);

	tpdomain = tpdomain_get (domain);
	if (!tpdomain) {
		mono_refcount_dec (&threadpool);
		return TRUE;
	}

	ret = TRUE;

	domains_set_lock ();
	while (tpdomain->outstanding_request + tpdomain->threadpool_jobs > 0) {
		if (timeout == -1) {
			mono_coop_cond_wait (&tpdomain->cleanup_cond, &threadpool.domains_lock);
		} else {
			gint64 now;
			gint res;

			now = mono_msec_ticks();
			if (now > end) {
				ret = FALSE;
				break;
			}

			res = mono_coop_cond_timedwait (&tpdomain->cleanup_cond, &threadpool.domains_lock, end - now);
			if (res != 0) {
				ret = FALSE;
				break;
			}
		}
	}

	/* Remove from the list the worker threads look at */
	tpdomain_remove (tpdomain);
	domains_set_unlock ();

	tpdomain_unref (tpdomain);

	mono_refcount_dec (&threadpool);

	return ret;
}

void
mono_threadpool_suspend (void)
{
	if (mono_lazy_is_initialized (&status))
		mono_threadpool_worker_set_suspended (TRUE);
}

void
mono_threadpool_resume (void)
{
	if (mono_lazy_is_initialized (&status))
		mono_threadpool_worker_set_suspended (FALSE);
}

void
ves_icall_System_Threading_ThreadPool_GetAvailableThreadsNative (gint32 *worker_threads, gint32 *completion_port_threads)
{
	ThreadPoolCounter counter;

	if (!worker_threads || !completion_port_threads)
		return;

	if (!mono_lazy_initialize (&status, initialize) || !mono_refcount_tryinc (&threadpool)) {
		*worker_threads = 0;
		*completion_port_threads = 0;
		return;
	}

	counter = COUNTER_READ ();

	*worker_threads = MAX (0, mono_threadpool_worker_get_max () - counter._.working);
	*completion_port_threads = threadpool.limit_io_max;

	mono_refcount_dec (&threadpool);
}

void
ves_icall_System_Threading_ThreadPool_GetMinThreadsNative (gint32 *worker_threads, gint32 *completion_port_threads)
{
	if (!worker_threads || !completion_port_threads)
		return;

	if (!mono_lazy_initialize (&status, initialize) || !mono_refcount_tryinc (&threadpool)) {
		*worker_threads = 0;
		*completion_port_threads = 0;
		return;
	}

	*worker_threads = mono_threadpool_worker_get_min ();
	*completion_port_threads = threadpool.limit_io_min;

	mono_refcount_dec (&threadpool);
}

void
ves_icall_System_Threading_ThreadPool_GetMaxThreadsNative (gint32 *worker_threads, gint32 *completion_port_threads)
{
	if (!worker_threads || !completion_port_threads)
		return;

	if (!mono_lazy_initialize (&status, initialize) || !mono_refcount_tryinc (&threadpool)) {
		*worker_threads = 0;
		*completion_port_threads = 0;
		return;
	}

	*worker_threads = mono_threadpool_worker_get_max ();
	*completion_port_threads = threadpool.limit_io_max;

	mono_refcount_dec (&threadpool);
}

MonoBoolean
ves_icall_System_Threading_ThreadPool_SetMinThreadsNative (gint32 worker_threads, gint32 completion_port_threads)
{
	if (completion_port_threads <= 0 || completion_port_threads > threadpool.limit_io_max)
		return FALSE;

	if (!mono_lazy_initialize (&status, initialize) || !mono_refcount_tryinc (&threadpool))
		return FALSE;

	if (!mono_threadpool_worker_set_min (worker_threads)) {
		mono_refcount_dec (&threadpool);
		return FALSE;
	}

	threadpool.limit_io_min = completion_port_threads;

	mono_refcount_dec (&threadpool);
	return TRUE;
}

MonoBoolean
ves_icall_System_Threading_ThreadPool_SetMaxThreadsNative (gint32 worker_threads, gint32 completion_port_threads)
{
	gint cpu_count = mono_cpu_count ();

	if (completion_port_threads < threadpool.limit_io_min || completion_port_threads < cpu_count)
		return FALSE;

	if (!mono_lazy_initialize (&status, initialize) || !mono_refcount_tryinc (&threadpool))
		return FALSE;

	if (!mono_threadpool_worker_set_max (worker_threads)) {
		mono_refcount_dec (&threadpool);
		return FALSE;
	}

	threadpool.limit_io_max = completion_port_threads;

	mono_refcount_dec (&threadpool);
	return TRUE;
}

void
ves_icall_System_Threading_ThreadPool_InitializeVMTp (MonoBoolean *enable_worker_tracking)
{
	if (enable_worker_tracking) {
		// TODO implement some kind of switch to have the possibily to use it
		*enable_worker_tracking = FALSE;
	}

	mono_lazy_initialize (&status, initialize);
}

MonoBoolean
ves_icall_System_Threading_ThreadPool_NotifyWorkItemComplete (void)
{
	if (mono_domain_is_unloading (mono_domain_get ()) || mono_runtime_is_shutting_down ())
		return FALSE;

	return mono_threadpool_worker_notify_completed ();
}

void
ves_icall_System_Threading_ThreadPool_NotifyWorkItemProgressNative (void)
{
	mono_threadpool_worker_notify_completed ();
}

void
ves_icall_System_Threading_ThreadPool_ReportThreadStatus (MonoBoolean is_working)
{
	// TODO
	MonoError error;
	mono_error_set_not_implemented (&error, "");
	mono_error_set_pending_exception (&error);
}

MonoBoolean
ves_icall_System_Threading_ThreadPool_RequestWorkerThread (void)
{
	MonoDomain *domain;
	ThreadPoolDomain *tpdomain;
	ThreadPoolCounter counter;

	domain = mono_domain_get ();
	if (mono_domain_is_unloading (domain))
		return FALSE;

	if (!mono_lazy_initialize (&status, initialize) || !mono_refcount_tryinc (&threadpool)) {
		/* threadpool has been destroyed, we are shutting down */
		return FALSE;
	}

	tpdomain = tpdomain_get (domain);
	if (!tpdomain) {
		/* synchronize with mono_threadpool_remove_domain_jobs */
		if (mono_domain_is_unloading (domain)) {
			mono_refcount_dec (&threadpool);
			return FALSE;
		}

		//try_create is the sync point with remove_domain_jobs so it might fail */
		tpdomain = tpdomain_try_create (domain);

		// It's unloading
		if (!tpdomain) {
			mono_refcount_dec (&threadpool);
			return FALSE;
		}
	}

	g_assert (tpdomain);

	gint32 outstanding_request = InterlockedIncrement (&tpdomain->outstanding_request);
	g_assert (outstanding_request >= 1);

	tpdomain_unref (tpdomain);

	COUNTER_ATOMIC (counter, {
		if (counter._.starting == 16) {
			mono_refcount_dec (&threadpool);
			return TRUE;
		}

		counter._.starting ++;
	});

	mono_threadpool_worker_request ();

	mono_refcount_dec (&threadpool);
	return TRUE;
}

MonoBoolean G_GNUC_UNUSED
ves_icall_System_Threading_ThreadPool_PostQueuedCompletionStatus (MonoNativeOverlapped *native_overlapped)
{
	/* This copy the behavior of the current Mono implementation */
	MonoError error;
	mono_error_set_not_implemented (&error, "");
	mono_error_set_pending_exception (&error);
	return FALSE;
}

MonoBoolean G_GNUC_UNUSED
ves_icall_System_Threading_ThreadPool_BindIOCompletionCallbackNative (gpointer file_handle)
{
	/* This copy the behavior of the current Mono implementation */
	return TRUE;
}

MonoBoolean G_GNUC_UNUSED
ves_icall_System_Threading_ThreadPool_IsThreadPoolHosted (void)
{
	return FALSE;
}
