#include <glib.h>
#include <pthread.h>

#include "config.h"
#include <mono/metadata/mono-gc.h>
#include <mono/metadata/gc-internal.h>
#include <mono/metadata/runtime.h>
#include <mono/metadata/method-builder.h>
#include <mono/metadata/object-internals.h>

#include <mono/utils/atomic.h>
#include <mono/utils/mono-threads.h>
#include <mono/utils/mono-counters.h>


#ifdef HAVE_COR_GC

static gboolean gc_initialized = FALSE;
static MonoMethod *write_barrier_method;


static void*
corgc_thread_register (MonoThreadInfo* info, void *baseptr)
{
	return info;
}

static void
corgc_thread_unregister (MonoThreadInfo *p)
{
	MonoNativeThreadId tid;

	tid = mono_thread_info_get_tid (p);

	if (p->runtime_thread)
		mono_threads_add_joinable_thread ((gpointer)tid);
}

static gboolean
mono_gc_is_critical_method (MonoMethod *method)
{
	return method == write_barrier_method;
}

void
mono_gc_base_init (void)
{
	MonoThreadInfoCallbacks cb;
	int dummy;

	if (gc_initialized)
		return;

	mono_counters_init ();

	memset (&cb, 0, sizeof (cb));
	cb.thread_register = corgc_thread_register;
	cb.thread_unregister = corgc_thread_unregister;
	cb.mono_method_is_critical = (gpointer)mono_runtime_is_critical_method;
#ifndef HOST_WIN32
	cb.thread_exit = mono_gc_pthread_exit;
	cb.mono_gc_pthread_create = (gpointer)mono_gc_pthread_create;
#endif
	
	mono_threads_init (&cb, sizeof (MonoThreadInfo));
	// mono_mutex_init (&mono_gc_lock);

	mono_thread_info_attach (&dummy);

	mono_gc_enable_events ();
	gc_initialized = TRUE;
}

void
mono_gc_collect (int generation)
{
}

int
mono_gc_max_generation (void)
{
	return 0;
}

int
mono_gc_get_generation  (MonoObject *object)
{
	return 0;
}

int
mono_gc_collection_count (int generation)
{
	return 0;
}

void
mono_gc_add_memory_pressure (gint64 value)
{
}

/* maybe track the size, not important, though */
int64_t
mono_gc_get_used_size (void)
{
	return 1024*1024;
}

int64_t
mono_gc_get_heap_size (void)
{
	return 2*1024*1024;
}

gboolean
mono_gc_is_gc_thread (void)
{
	return TRUE;
}

gboolean
mono_gc_register_thread (void *baseptr)
{
	return mono_thread_info_attach (baseptr) != NULL;
}

int
mono_gc_walk_heap (int flags, MonoGCReferences callback, void *data)
{
	return 1;
}

gboolean
mono_object_is_alive (MonoObject* o)
{
	return TRUE;
}

void
mono_gc_enable_events (void)
{
}

int
mono_gc_register_root (char *start, size_t size, void *descr)
{
	return TRUE;
}

void
mono_gc_deregister_root (char* addr)
{
}

void
mono_gc_weak_link_add (void **link_addr, MonoObject *obj, gboolean track)
{
	*link_addr = obj;
}

void
mono_gc_weak_link_remove (void **link_addr, gboolean track)
{
	*link_addr = NULL;
}

MonoObject*
mono_gc_weak_link_get (void **link_addr)
{
	return *link_addr;
}

void*
mono_gc_make_descr_for_string (gsize *bitmap, int numbits)
{
	return NULL;
}

void*
mono_gc_make_descr_for_object (gsize *bitmap, int numbits, size_t obj_size)
{
	return NULL;
}

void*
mono_gc_make_descr_for_array (int vector, gsize *elem_bitmap, int numbits, size_t elem_size)
{
	return NULL;
}

void*
mono_gc_make_descr_from_bitmap (gsize *bitmap, int numbits)
{
	return NULL;
}

void*
mono_gc_make_root_descr_all_refs (int numbits)
{
	return NULL;
}

void*
mono_gc_alloc_fixed (size_t size, void *descr)
{
	return g_malloc0 (size);
}

void
mono_gc_free_fixed (void* addr)
{
	g_free (addr);
}

void
mono_gc_wbarrier_set_field (MonoObject *obj, gpointer field_ptr, MonoObject* value)
{
	*(void**)field_ptr = value;
}

void
mono_gc_wbarrier_set_arrayref (MonoArray *arr, gpointer slot_ptr, MonoObject* value)
{
	*(void**)slot_ptr = value;
}

void
mono_gc_wbarrier_arrayref_copy (gpointer dest_ptr, gpointer src_ptr, int count)
{
	mono_gc_memmove_aligned (dest_ptr, src_ptr, count * sizeof (gpointer));
}

void
mono_gc_wbarrier_generic_store (gpointer ptr, MonoObject* value)
{
	*(void**)ptr = value;
}

void
mono_gc_wbarrier_generic_store_atomic (gpointer ptr, MonoObject *value)
{
	InterlockedWritePointer (ptr, value);
}

void
mono_gc_wbarrier_generic_nostore (gpointer ptr)
{
}

void
mono_gc_wbarrier_value_copy (gpointer dest, gpointer src, int count, MonoClass *klass)
{
	mono_gc_memmove_atomic (dest, src, count * mono_class_value_size (klass, NULL));
}

void
mono_gc_wbarrier_object_copy (MonoObject* obj, MonoObject *src)
{
	/* do not copy the sync state */
	mono_gc_memmove_aligned ((char*)obj + sizeof (MonoObject), (char*)src + sizeof (MonoObject),
			mono_object_class (obj)->instance_size - sizeof (MonoObject));
}

int
mono_gc_get_aligned_size_for_allocator (int size)
{
	return size;
}

MonoMethod*
mono_gc_get_managed_allocator (MonoClass *klass, gboolean for_box, gboolean known_instance_size)
{
	return NULL;
}

MonoMethod*
mono_gc_get_managed_array_allocator (MonoClass *klass)
{
	return NULL;
}

MonoMethod*
mono_gc_get_managed_allocator_by_type (int atype)
{
	return NULL;
}

guint32
mono_gc_get_managed_allocator_types (void)
{
	return 0;
}

const char *
mono_gc_get_gc_name (void)
{
	return "CoreCLR GC";
}

void
mono_gc_clear_domain (MonoDomain *domain)
{
}

int
mono_gc_get_suspend_signal (void)
{
	return -1;
}

int
mono_gc_get_restart_signal (void)
{
	return -1;
}


#define OPDEF(a,b,c,d,e,f,g,h,i,j) \
	a = i,

enum {
#include "mono/cil/opcode.def"
	CEE_LAST
};

MonoMethod*
mono_gc_get_write_barrier (void)
{
	MonoMethod *res;
	MonoMethodBuilder *mb;
	MonoMethodSignature *sig;

	if (write_barrier_method)
		return write_barrier_method;

	/* Create the IL version of mono_gc_barrier_generic_store () */
	sig = mono_metadata_signature_alloc (mono_defaults.corlib, 1);
	sig->ret = &mono_defaults.void_class->byval_arg;
	sig->params [0] = &mono_defaults.int_class->byval_arg;

	mb = mono_mb_new (mono_defaults.object_class, "wbarrier", MONO_WRAPPER_WRITE_BARRIER);

	mono_mb_emit_ldarg (mb, 0);
	mono_mb_emit_icall (mb, mono_gc_wbarrier_generic_nostore);
	mono_mb_emit_byte (mb, CEE_RET);

	res = mono_mb_create_method (mb, sig, 16);
	mono_mb_free (mb);

	//FIXME locking
	write_barrier_method = res;

	return write_barrier_method;
}

void*
mono_gc_invoke_with_gc_lock (MonoGCLockedCallbackFunc func, void *data)
{
	return func (data);
}

char*
mono_gc_get_description (void)
{
	return g_strdup (DEFAULT_GC_NAME);
}

void
mono_gc_set_desktop_mode (void)
{
}

gboolean
mono_gc_is_moving (void)
{
	return TRUE;
}

gboolean
mono_gc_is_disabled (void)
{
	return FALSE;
}

void
mono_gc_wbarrier_value_copy_bitmap (gpointer _dest, gpointer _src, int size, unsigned bitmap)
{
	g_assert_not_reached ();
}

guint8*
mono_gc_get_card_table (int *shift_bits, gpointer *card_mask)
{
	return NULL;
}

gboolean
mono_gc_card_table_nursery_check (void)
{
	g_assert_not_reached ();
	return TRUE;
}

void*
mono_gc_get_nursery (int *shift_bits, size_t *size)
{
	return NULL;
}

void
mono_gc_set_current_thread_appdomain (MonoDomain *domain)
{
}

gboolean
mono_gc_precise_stack_mark_enabled (void)
{
	return FALSE;
}

FILE *
mono_gc_get_logfile (void)
{
	return NULL;
}

void
mono_gc_conservatively_scan_area (void *start, void *end)
{
	g_assert_not_reached ();
}

void *
mono_gc_scan_object (void *obj, void *gc_data)
{
	g_assert_not_reached ();
	return NULL;
}

gsize*
mono_gc_get_bitmap_for_descr (void *descr, int *numbits)
{
	g_assert_not_reached ();
	return NULL;
}

void
mono_gc_set_gc_callbacks (MonoGCCallbacks *callbacks)
{
}

void
mono_gc_set_stack_end (void *stack_end)
{
}

int
mono_gc_get_los_limit (void)
{
	return G_MAXINT;
}

gboolean
mono_gc_user_markers_supported (void)
{
	return FALSE;
}

void *
mono_gc_make_root_descr_user (MonoGCRootMarkFunc marker)
{
	return NULL;
}

#ifndef HOST_WIN32

int
mono_gc_pthread_create (pthread_t *new_thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg)
{
	return pthread_create (new_thread, attr, start_routine, arg);
}

int
mono_gc_pthread_join (pthread_t thread, void **retval)
{
	return pthread_join (thread, retval);
}

int
mono_gc_pthread_detach (pthread_t thread)
{
	return pthread_detach (thread);
}

void
mono_gc_pthread_exit (void *retval)
{
	pthread_exit (retval);
}

void mono_gc_set_skip_thread (gboolean value)
{
}
#endif

#ifdef HOST_WIN32
BOOL APIENTRY mono_gc_dllmain (HMODULE module_handle, DWORD reason, LPVOID reserved)
{
	return TRUE;
}
#endif

guint
mono_gc_get_vtable_bits (MonoClass *class)
{
	return 0;
}

void
mono_gc_register_altstack (gpointer stack, gint32 stack_size, gpointer altstack, gint32 altstack_size)
{
}

gboolean
mono_gc_set_allow_synchronous_major (gboolean flag)
{
	return TRUE;
}

int
mono_gc_invoke_finalizers (void)
{
	return 0;
}

gboolean
mono_gc_pending_finalizers (void)
{
	return FALSE;
}
void
mono_gc_set_string_length (MonoString *str, gint32 new_length)
{
	str->length = new_length;
}

//moving collector extra callbacks
gboolean
mono_gc_ephemeron_array_add (MonoObject *obj)
{
	return FALSE;
}

int
mono_gc_finalizers_for_domain (MonoDomain *domain, MonoObject **out_array, int out_size)
{
	return 0;
}

void
mono_gc_register_for_finalization (MonoObject *obj, void *user_data)
{
	
}

int
mono_gc_register_root_wbarrier (char *start, size_t size, void *descr)
{
	return TRUE;
}

void*
mono_gc_alloc_obj (MonoVTable *vtable, size_t size)
{
	void **res = g_malloc0 (size);
	*res = vtable;
	return res;
}

void*
mono_gc_alloc_vector (MonoVTable *vtable, size_t size, uintptr_t max_length)
{
	MonoArray *arr = (MonoArray *)mono_gc_alloc_obj (vtable, size);;
	arr->max_length = (mono_array_size_t)max_length;
	return arr;
}


void*
mono_gc_alloc_array (MonoVTable *vtable, size_t size, uintptr_t max_length, uintptr_t bounds_size)
{
	MonoArray *arr = (MonoArray *)mono_gc_alloc_obj (vtable, size);;
	MonoArrayBounds *bounds = (MonoArrayBounds*)((char*)arr + size - bounds_size);

	arr->max_length = (mono_array_size_t)max_length;
	arr->bounds = bounds;
	return arr;
}

void*
mono_gc_alloc_string (MonoVTable *vtable, size_t size, gint32 len)
{
	MonoString *str = (MonoString *)mono_gc_alloc_obj (vtable, size);

	str->length = len;
	return str;
}

void*
mono_gc_alloc_pinned_obj (MonoVTable *vtable, size_t size)
{
	return mono_gc_alloc_obj (vtable, size);
}

void*
mono_gc_alloc_mature (MonoVTable *vtable)
{
	return mono_gc_alloc_obj (vtable, vtable->klass->instance_size);
}


#endif