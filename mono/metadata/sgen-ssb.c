/*
 * Copyright 2001-2003 Ximian, Inc
 * Copyright 2003-2011 Novell, Inc.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * We're never actually using the first element.  It's always set to
 * NULL to simplify the elimination of consecutive duplicate
 * entries.
 */

#include "config.h"
#ifdef HAVE_SGEN_GC

#include "metadata/gc-internal.h"
#include "metadata/method-builder.h"
#include "metadata/sgen-protocol.h"
#include "metadata/sgen-ssb.h"
#include "utils/mono-compiler.h"
#include "utils/mono-counters.h"

#define OPDEF(a,b,c,d,e,f,g,h,i,j) \
	a = i,

enum {
#include "mono/cil/opcode.def"
	CEE_LAST
};

#undef OPDEF

static const int prime_table[] = {
	4093,
	8191,
	16381,
	32749,
	65521,
	131071,
	262139,
	524287,
	1048573,
	2097143,
	4194301,
	8388593,
	16777213,
	33554393,
	67108859,
	134217689,
	268435399,
	536870909,
	1073741789,
	0
};

#define STORE_REMSET_BUFFER_SIZE	1024
#define LOAD_FACTOR 0.7f

#ifdef HAVE_KW_THREAD
static __thread gpointer *store_remset_buffer;
static __thread long store_remset_buffer_index;
static __thread long *store_remset_buffer_index_addr;
#define STORE_REMSET_BUFFER	store_remset_buffer
#define STORE_REMSET_BUFFER_INDEX	store_remset_buffer_index
#else
#define STORE_REMSET_BUFFER	(__thread_info__->store_remset_buffer)
#define STORE_REMSET_BUFFER_INDEX	(__thread_info__->store_remset_buffer_index)
#endif

#define TOMBSTONE ((gpointer)(ssize_t)-1)
static gpointer *ssb_hashtable = NULL;
static int ssb_hashtable_size_index = 2;
static int ssb_hashtable_size, ssb_hashtable_elements;

static void
insert_no_check (gpointer *table, int table_size, gpointer pointer)
{
	int idx, initial_idx, free_slot = -1;

	/*FIXME, look for a good pointer hashing function*/
	idx = initial_idx = (int)pointer % table_size;
	do {
		gpointer k = table [idx];

		if (!k) {
			if (free_slot == -1)
				free_slot = idx;
			break;
		} else if (k == TOMBSTONE && free_slot == -1) {
			free_slot = idx;
		} else if (k == pointer) { 
			return;
		}

		if (++idx == table_size) //Wrap around
			idx = 0;
	} while (idx != initial_idx);

	table [free_slot] = pointer;
	++ssb_hashtable_elements;
}

static void
rehash (gboolean can_expand)
{
	int i;
	gpointer *new_table;
	/*FIXME trigger a nursery and only then expand when can_expand == FALSE*/
	int new_size = prime_table [++ssb_hashtable_size_index];

	g_assert (new_size);

	ssb_hashtable_elements = 0;
	new_table = mono_sgen_alloc_os_memory (new_size * sizeof (gpointer), TRUE);
	for (i = 0; i < ssb_hashtable_size; ++i) {
		if (ssb_hashtable [i] && ssb_hashtable [i] != TOMBSTONE)
			insert_no_check (new_table, new_size, ssb_hashtable [i]);
	}

	ssb_hashtable = new_table;
	ssb_hashtable_size = new_size;
}

static gboolean
find_pointer (gpointer pointer)
{
	int idx, initial_idx;
	idx = initial_idx = (int)pointer % ssb_hashtable_size;
	
	do {
		gpointer k = ssb_hashtable [idx];
		if (k == pointer)
			return TRUE;
		if (!k)
			break;
		if (++idx == ssb_hashtable_size) //Wrap around
			idx = 0;
	} while (idx != initial_idx);
	return FALSE;
}

static void
insert_pointer (gpointer pointer, gboolean can_expand)
{
	if (ssb_hashtable_elements >= ssb_hashtable_size * LOAD_FACTOR)
		rehash (can_expand);
	insert_no_check (ssb_hashtable, ssb_hashtable_size, pointer);
}

static void
insert_from_ssb (gpointer *buffer, int size, gboolean can_expand)
{
	int i;
	for (i = 1; i < size; ++i)
		insert_pointer (buffer [i], can_expand);
}

static void
clear_thread_store_remset_buffer (SgenThreadInfo *info)
{
	*info->store_remset_buffer_index_addr = 0;
	memset (*info->store_remset_buffer_addr, 0, sizeof (gpointer) * STORE_REMSET_BUFFER_SIZE);
}


static void
sgen_ssb_evacuate_buffer (void)
{
	TLAB_ACCESS_INIT;
	insert_from_ssb (STORE_REMSET_BUFFER, STORE_REMSET_BUFFER_SIZE, FALSE);
	STORE_REMSET_BUFFER_INDEX = 0;
}

void
sgen_ssb_record_pointer (gpointer ptr)
{
	gpointer *buffer;
	int index;
	TLAB_ACCESS_INIT;

	LOCK_GC;

	buffer = STORE_REMSET_BUFFER;
	index = STORE_REMSET_BUFFER_INDEX;
	/* This simple optimization eliminates a sizable portion of
	   entries.  Comparing it to the last but one entry as well
	   doesn't eliminate significantly more entries. */
	if (buffer [index] == ptr) {
		UNLOCK_GC;
		return;
	}

	DEBUG (8, fprintf (gc_debug_file, "Adding remset at %p\n", ptr));
	HEAVY_STAT (++stat_wbarrier_generic_store_remset);

	++index;
	if (index >= STORE_REMSET_BUFFER_SIZE) {
		sgen_ssb_evacuate_buffer ();
		index = STORE_REMSET_BUFFER_INDEX;
		g_assert (index == 0);
		++index;
	}
	buffer [index] = ptr;
	STORE_REMSET_BUFFER_INDEX = index;

	UNLOCK_GC;
}

void
sgen_ssb_scan (void *start_nursery, void *end_nursery, SgenGrayQueue *queue)
{
	int i;
	SgenThreadInfo **thread_table;
	SgenThreadInfo *info;

	printf ("elements %d\n", ssb_hashtable_elements);
	/*FLUSH remaining elements*/
	thread_table = mono_sgen_get_thread_table ();
	for (i = 0; i < THREAD_HASH_SIZE; ++i) {
		for (info = thread_table [i]; info; info = info->next) {
			insert_from_ssb ((gpointer*)*info->store_remset_buffer_addr, *info->store_remset_buffer_index_addr, TRUE);
			clear_thread_store_remset_buffer (info);
		}
	}

	/* the generic store ones */
	for (i = 0; i < ssb_hashtable_size; ++i) {
		gpointer *addr = (gpointer*)ssb_hashtable[i];
		if (addr && addr != TOMBSTONE) {
			gpointer *ptr = *addr;
			if (((void*)ptr < start_nursery || (void*)ptr >= end_nursery)) {
				gpointer new, old = *ptr;
				major_collector.copy_object (ptr, queue);
				DEBUG (9, fprintf (gc_debug_file, "Kept object on ssb slot due to promotion at %p with %p\n", ptr, *ptr));
				if (old)
					binary_protocol_ptr_update (ptr, old, *ptr, (gpointer)LOAD_VTABLE (*ptr), mono_sgen_safe_object_get_size (*ptr));
				new = *ptr;
				if (new < start_nursery || new >= end_nursery) {
					/*
					 * If the object was promoted, we can remove it.
					 */
					ssb_hashtable[i] = TOMBSTONE;
				} else {
					DEBUG (9, fprintf (gc_debug_file, "Keep on the ssb hash because of pinning %p (%p %s)\n", ptr, *ptr, mono_sgen_safe_name (*ptr)));
				}
			}
		}
	}
}

void
sgen_ssb_clear (void)
{
	int i;
	SgenThreadInfo **thread_table;
	SgenThreadInfo *info;

	memset (ssb_hashtable, 0, ssb_hashtable_size * sizeof (gpointer));

	thread_table = mono_sgen_get_thread_table ();
	for (i = 0; i < THREAD_HASH_SIZE; ++i) {
		for (info = thread_table [i]; info; info = info->next)
			clear_thread_store_remset_buffer (info);
	}
}

gboolean
sgen_ssb_find (char *addr)
{
	int i;
	SgenThreadInfo **thread_table;
	SgenThreadInfo *info;

	if (find_pointer (addr))
		return TRUE;

	for (i = 0; i < THREAD_HASH_SIZE; ++i) {
		for (info = thread_table [i]; info; info = info->next) {
			int j;
			for (j = 0; j < *info->store_remset_buffer_index_addr; ++j) {
				if ((*info->store_remset_buffer_addr) [j + 1] == addr)
					return TRUE;
			}
		}
	}

	return FALSE;
}

void
sgen_ssb_register_thread (SgenThreadInfo *info)
{
#ifndef HAVE_KW_THREAD
	SgenThreadInfo *__thread_info__ = info;
#endif

	info->store_remset_buffer_addr = &STORE_REMSET_BUFFER;
	info->store_remset_buffer_index_addr = &STORE_REMSET_BUFFER_INDEX;

#ifdef HAVE_KW_THREAD
	store_remset_buffer_index_addr = &store_remset_buffer_index;
#endif

	STORE_REMSET_BUFFER = mono_sgen_alloc_internal (INTERNAL_MEM_STORE_REMSET);
	STORE_REMSET_BUFFER_INDEX = 0;	
}

void
sgen_ssb_unregister_thread (SgenThreadInfo *info)
{
	if (*info->store_remset_buffer_index_addr)
		insert_from_ssb (*info->store_remset_buffer_addr, STORE_REMSET_BUFFER_SIZE, FALSE);
	mono_sgen_free_internal (*info->store_remset_buffer_addr, INTERNAL_MEM_STORE_REMSET);
}

void
sgen_ssb_init (void)
{
	mono_sgen_register_fixed_internal_mem_type (INTERNAL_MEM_STORE_REMSET, sizeof (gpointer) * STORE_REMSET_BUFFER_SIZE);
	ssb_hashtable_size = prime_table [ssb_hashtable_size_index];
	ssb_hashtable = mono_sgen_alloc_os_memory (ssb_hashtable_size * sizeof (gpointer), TRUE);
}


MonoMethod*
sgen_ssb_get_write_barrier (void)
{
	MonoMethod *res;
	MonoMethodBuilder *mb;
	MonoMethodSignature *sig;
#ifdef MANAGED_WBARRIER
	int label_no_wb_1, label_no_wb_2, label_no_wb_3, label_no_wb_4, label_need_wb, label_slow_path;
#ifndef SGEN_ALIGN_NURSERY
	int label_continue_1, label_continue_2, label_no_wb_5;
	int dereferenced_var;
#endif
	int buffer_var, buffer_index_var, dummy_var;
	int nursery_bits;
	size_t nursery_size;

	gpointer nursery_start = mono_gc_get_nursery (&nursery_bits, &nursery_size);

#ifdef HAVE_KW_THREAD
	int stack_end_offset = -1, store_remset_buffer_offset = -1;
	int store_remset_buffer_index_offset = -1, store_remset_buffer_index_addr_offset = -1;

	MONO_THREAD_VAR_OFFSET (stack_end, stack_end_offset);
	g_assert (stack_end_offset != -1);
	MONO_THREAD_VAR_OFFSET (store_remset_buffer, store_remset_buffer_offset);
	g_assert (store_remset_buffer_offset != -1);
	MONO_THREAD_VAR_OFFSET (store_remset_buffer_index, store_remset_buffer_index_offset);
	g_assert (store_remset_buffer_index_offset != -1);
	MONO_THREAD_VAR_OFFSET (store_remset_buffer_index_addr, store_remset_buffer_index_addr_offset);
	g_assert (store_remset_buffer_index_addr_offset != -1);
#endif
#endif

	/* Create the IL version of mono_gc_barrier_generic_store () */
	sig = mono_metadata_signature_alloc (mono_defaults.corlib, 1);
	sig->ret = &mono_defaults.void_class->byval_arg;
	sig->params [0] = &mono_defaults.int_class->byval_arg;

	mb = mono_mb_new (mono_defaults.object_class, "wbarrier", MONO_WRAPPER_WRITE_BARRIER);

#ifdef MANAGED_WBARRIER
	if (mono_runtime_has_tls_get ()) {
#ifdef SGEN_ALIGN_NURSERY
		// if (ptr_in_nursery (ptr)) return;
		/*
		 * Masking out the bits might be faster, but we would have to use 64 bit
		 * immediates, which might be slower.
		 */
		mono_mb_emit_ldarg (mb, 0);
		mono_mb_emit_icon (mb, nursery_bits);
		mono_mb_emit_byte (mb, CEE_SHR_UN);
		mono_mb_emit_icon (mb, (mword)nursery_start >> nursery_bits);
		label_no_wb_1 = mono_mb_emit_branch (mb, CEE_BEQ);

		// if (!ptr_in_nursery (*ptr)) return;
		mono_mb_emit_ldarg (mb, 0);
		mono_mb_emit_byte (mb, CEE_LDIND_I);
		mono_mb_emit_icon (mb, nursery_bits);
		mono_mb_emit_byte (mb, CEE_SHR_UN);
		mono_mb_emit_icon (mb, (mword)nursery_start >> nursery_bits);
		label_no_wb_2 = mono_mb_emit_branch (mb, CEE_BNE_UN);
#else

		// if (ptr < (nursery_start)) goto continue;
		mono_mb_emit_ldarg (mb, 0);
		mono_mb_emit_ptr (mb, (gpointer) nursery_start);
		label_continue_1 = mono_mb_emit_branch (mb, CEE_BLT);

		// if (ptr >= nursery_real_end)) goto continue;
		mono_mb_emit_ldarg (mb, 0);
		mono_mb_emit_ptr (mb, (gpointer) nursery_real_end);
		label_continue_2 = mono_mb_emit_branch (mb, CEE_BGE);

		// Otherwise return
		label_no_wb_1 = mono_mb_emit_branch (mb, CEE_BR);

		// continue:
		mono_mb_patch_branch (mb, label_continue_1);
		mono_mb_patch_branch (mb, label_continue_2);

		// Dereference and store in local var
		dereferenced_var = mono_mb_add_local (mb, &mono_defaults.int_class->byval_arg);
		mono_mb_emit_ldarg (mb, 0);
		mono_mb_emit_byte (mb, CEE_LDIND_I);
		mono_mb_emit_stloc (mb, dereferenced_var);

		// if (*ptr < nursery_start) return;
		mono_mb_emit_ldloc (mb, dereferenced_var);
		mono_mb_emit_ptr (mb, (gpointer) nursery_start);
		label_no_wb_2 = mono_mb_emit_branch (mb, CEE_BLT);

		// if (*ptr >= nursery_end) return;
		mono_mb_emit_ldloc (mb, dereferenced_var);
		mono_mb_emit_ptr (mb, (gpointer) nursery_real_end);
		label_no_wb_5 = mono_mb_emit_branch (mb, CEE_BGE);

#endif 
		// if (ptr >= stack_end) goto need_wb;
		mono_mb_emit_ldarg (mb, 0);
		EMIT_TLS_ACCESS (mb, stack_end, stack_end_offset);
		label_need_wb = mono_mb_emit_branch (mb, CEE_BGE_UN);

		// if (ptr >= stack_start) return;
		dummy_var = mono_mb_add_local (mb, &mono_defaults.int_class->byval_arg);
		mono_mb_emit_ldarg (mb, 0);
		mono_mb_emit_ldloc_addr (mb, dummy_var);
		label_no_wb_3 = mono_mb_emit_branch (mb, CEE_BGE_UN);

		// need_wb:
		mono_mb_patch_branch (mb, label_need_wb);

		// buffer = STORE_REMSET_BUFFER;
		buffer_var = mono_mb_add_local (mb, &mono_defaults.int_class->byval_arg);
		EMIT_TLS_ACCESS (mb, store_remset_buffer, store_remset_buffer_offset);
		mono_mb_emit_stloc (mb, buffer_var);

		// buffer_index = STORE_REMSET_BUFFER_INDEX;
		buffer_index_var = mono_mb_add_local (mb, &mono_defaults.int_class->byval_arg);
		EMIT_TLS_ACCESS (mb, store_remset_buffer_index, store_remset_buffer_index_offset);
		mono_mb_emit_stloc (mb, buffer_index_var);

		// if (buffer [buffer_index] == ptr) return;
		mono_mb_emit_ldloc (mb, buffer_var);
		mono_mb_emit_ldloc (mb, buffer_index_var);
		g_assert (sizeof (gpointer) == 4 || sizeof (gpointer) == 8);
		mono_mb_emit_icon (mb, sizeof (gpointer) == 4 ? 2 : 3);
		mono_mb_emit_byte (mb, CEE_SHL);
		mono_mb_emit_byte (mb, CEE_ADD);
		mono_mb_emit_byte (mb, CEE_LDIND_I);
		mono_mb_emit_ldarg (mb, 0);
		label_no_wb_4 = mono_mb_emit_branch (mb, CEE_BEQ);

		// ++buffer_index;
		mono_mb_emit_ldloc (mb, buffer_index_var);
		mono_mb_emit_icon (mb, 1);
		mono_mb_emit_byte (mb, CEE_ADD);
		mono_mb_emit_stloc (mb, buffer_index_var);

		// if (buffer_index >= STORE_REMSET_BUFFER_SIZE) goto slow_path;
		mono_mb_emit_ldloc (mb, buffer_index_var);
		mono_mb_emit_icon (mb, STORE_REMSET_BUFFER_SIZE);
		label_slow_path = mono_mb_emit_branch (mb, CEE_BGE);

		// buffer [buffer_index] = ptr;
		mono_mb_emit_ldloc (mb, buffer_var);
		mono_mb_emit_ldloc (mb, buffer_index_var);
		g_assert (sizeof (gpointer) == 4 || sizeof (gpointer) == 8);
		mono_mb_emit_icon (mb, sizeof (gpointer) == 4 ? 2 : 3);
		mono_mb_emit_byte (mb, CEE_SHL);
		mono_mb_emit_byte (mb, CEE_ADD);
		mono_mb_emit_ldarg (mb, 0);
		mono_mb_emit_byte (mb, CEE_STIND_I);

		// STORE_REMSET_BUFFER_INDEX = buffer_index;
		EMIT_TLS_ACCESS (mb, store_remset_buffer_index_addr, store_remset_buffer_index_addr_offset);
		mono_mb_emit_ldloc (mb, buffer_index_var);
		mono_mb_emit_byte (mb, CEE_STIND_I);

		// return;
		mono_mb_patch_branch (mb, label_no_wb_1);
		mono_mb_patch_branch (mb, label_no_wb_2);
		mono_mb_patch_branch (mb, label_no_wb_3);
		mono_mb_patch_branch (mb, label_no_wb_4);
#ifndef SGEN_ALIGN_NURSERY
		mono_mb_patch_branch (mb, label_no_wb_5);
#endif
		mono_mb_emit_byte (mb, CEE_RET);

		// slow path
		mono_mb_patch_branch (mb, label_slow_path);
	}
#endif

	mono_mb_emit_ldarg (mb, 0);
	mono_mb_emit_icall (mb, mono_gc_wbarrier_generic_nostore);
	mono_mb_emit_byte (mb, CEE_RET);

	res = mono_mb_create_method (mb, sig, 16);
	mono_mb_free (mb);

	return res;
}

#endif /* HAVE_SGEN_GC */