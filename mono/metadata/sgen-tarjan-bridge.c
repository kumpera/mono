/*
 * sgen-bridge.c: Simple generational GC.
 *
 * Copyright 2011 Novell, Inc (http://www.novell.com)
 * Copyright 2011 Xamarin Inc (http://www.xamarin.com)
 *
 * THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 * OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 * Permission is hereby granted to use or copy this program
 * for any purpose,  provided the above notices are retained on all copies.
 * Permission to modify the code and to distribute modified code is granted,
 * provided the above notices are retained, and a notice that the code was
 * modified is included with the above copyright notice.
 *
 *
 * Copyright 2001-2003 Ximian, Inc
 * Copyright 2003-2010 Novell, Inc.
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

#include "config.h"

#ifdef HAVE_SGEN_GC

#include <stdlib.h>

#include "sgen-gc.h"
#include "sgen-bridge.h"
#include "sgen-hash-table.h"
#include "sgen-qsort.h"
#include "tabledefs.h"
#include "utils/mono-logger-internal.h"
#include "utils/mono-time.h"
#include "utils/mono-compiler.h"


typedef struct {
	int size;
	int capacity;
	char *data;
} DynArray;

/*Specializations*/

typedef struct {
	DynArray array;
} DynIntArray;

typedef struct {
	DynArray array;
} DynPtrArray;


/* private */

static void
dyn_array_init (DynArray *da)
{
	da->size = 0;
	da->capacity = 0;
	da->data = NULL;
}

static void
dyn_array_uninit (DynArray *da, int elem_size)
{
	if (da->capacity <= 0)
		return;

	sgen_free_internal_dynamic (da->data, elem_size * da->capacity, INTERNAL_MEM_BRIDGE_DATA);
	da->data = NULL;
}

static void
dyn_array_ensure_capacity (DynArray *da, int capacity, int elem_size)
{
	int old_capacity = da->capacity;
	char *new_data;

	if (capacity <= old_capacity)
		return;

	if (da->capacity == 0)
		da->capacity = 2;
	while (capacity > da->capacity)
		da->capacity *= 2;

	new_data = sgen_alloc_internal_dynamic (elem_size * da->capacity, INTERNAL_MEM_BRIDGE_DATA, TRUE);
	memcpy (new_data, da->data, elem_size * da->size);
	sgen_free_internal_dynamic (da->data, elem_size * old_capacity, INTERNAL_MEM_BRIDGE_DATA);
	da->data = new_data;
}

static void*
dyn_array_add (DynArray *da, int elem_size)
{
	void *p;

	dyn_array_ensure_capacity (da, da->size + 1, elem_size);

	p = da->data + da->size * elem_size;
	++da->size;
	return p;
}

/* int */
static void
dyn_array_int_init (DynIntArray *da)
{
	dyn_array_init (&da->array);
}

static void
dyn_array_int_uninit (DynIntArray *da)
{
	dyn_array_uninit (&da->array, sizeof (int));
}

static int
dyn_array_int_size (DynIntArray *da)
{
	return da->array.size;
}

static void
dyn_array_int_set_size (DynIntArray *da, int size)
{
	da->array.size = size;
}

static void
dyn_array_int_add (DynIntArray *da, int x)
{
	int *p = dyn_array_add (&da->array, sizeof (int));
	*p = x;
}

static int
dyn_array_int_get (DynIntArray *da, int x)
{
	return ((int*)da->array.data)[x];
}

static void
dyn_array_int_set (DynIntArray *da, int idx, int val)
{
	((int*)da->array.data)[idx] = val;
}

static void
dyn_array_int_ensure_capacity (DynIntArray *da, int capacity)
{
	dyn_array_ensure_capacity (&da->array, capacity, sizeof (int));
}

static void
dyn_array_int_set_all (DynIntArray *dst, DynIntArray *src)
{
	dyn_array_int_ensure_capacity (dst, src->array.size);
	memcpy (dst->array.data, src->array.data, src->array.size * sizeof (int));
	dst->array.size = src->array.size;
}

/* ptr */

static void
dyn_array_ptr_init (DynPtrArray *da)
{
	dyn_array_init (&da->array);
}

static void
dyn_array_ptr_uninit (DynPtrArray *da)
{
	dyn_array_uninit (&da->array, sizeof (void*));
}

static int
dyn_array_ptr_size (DynPtrArray *da)
{
	return da->array.size;
}

static void
dyn_array_ptr_set_size (DynPtrArray *da, int size)
{
	da->array.size = size;
}

static void*
dyn_array_ptr_get (DynPtrArray *da, int x)
{
	return ((void**)da->array.data)[x];
}

static void
dyn_array_ptr_add (DynPtrArray *da, void *ptr)
{
	void **p = dyn_array_add (&da->array, sizeof (void*));
	*p = ptr;
}

#define dyn_array_ptr_push dyn_array_ptr_add

static void*
dyn_array_ptr_pop (DynPtrArray *da)
{
	void *p;
	int size = da->array.size;
	g_assert (size > 0);
	p = dyn_array_ptr_get (da, size - 1);
	--da->array.size;
	return p;
}

static void
enable_accounting (void)
{
	// bridge_accounting_enabled = TRUE;
	// hash_table = (SgenHashTable)SGEN_HASH_TABLE_INIT (INTERNAL_MEM_BRIDGE_HASH_TABLE, INTERNAL_MEM_BRIDGE_HASH_TABLE_ENTRY, sizeof (HashEntryWithAccounting), mono_aligned_addr_hash, NULL);
}

static MonoGCBridgeObjectKind
class_kind (MonoClass *class)
{
	MonoGCBridgeObjectKind res = bridge_callbacks.bridge_class_kind (class);

	/* If it's a bridge, nothing we can do about it. */
	if (res == GC_BRIDGE_TRANSPARENT_BRIDGE_CLASS || res == GC_BRIDGE_OPAQUE_BRIDGE_CLASS)
		return res;

	/* Non bridge classes with no pointers will never point to a bridge, so we can savely ignore them. */
	if (!class->has_references) {
		SGEN_LOG (6, "class %s is opaque\n", class->name);
		return GC_BRIDGE_OPAQUE_CLASS;
	}

	/* Some arrays can be ignored */
	if (class->rank == 1) {
		MonoClass *elem_class = class->element_class;

		/* FIXME the bridge check can be quite expensive, cache it at the class level. */
		/* An array of a sealed type that is not a bridge will never get to a bridge */
		if ((elem_class->flags & TYPE_ATTRIBUTE_SEALED) && !elem_class->has_references && !bridge_callbacks.bridge_class_kind (elem_class)) {
			SGEN_LOG (6, "class %s is opaque\n", class->name);
			return GC_BRIDGE_OPAQUE_CLASS;
		}
	}

	return GC_BRIDGE_TRANSPARENT_CLASS;
}

//enable unsage logging
// #define DUMP_GRAPH 1
// #define DUMP_OBJS 1

enum {
	INITIAL,
	SCANNED,
	FINISHED_ON_STACK,
	FINISHED_OFF_STACK
};

typedef struct {
	MonoObject *obj; //XXX this can be eliminated.
	mword lock_word;

	int index;
	int low_index;
	int color : 27;

	unsigned state : 2;
	unsigned is_bridge : 1;
	unsigned obj_state : 2;
} ScanData;

/*
Optimizations:
	Both arrays can be inlined since the number of colors and bridges is known in advance
	We can split this data structure in two, those with bridges and those without
*/
typedef struct {
	DynIntArray other_colors;
	DynPtrArray bridges;
	int api_index    : 31;
	unsigned visited : 1;
} ColorData;


static DynPtrArray scan_stack, loop_stack, registered_bridges, color_table;
static DynIntArray low_color, xref_merge_array;

static int ignored_objects;
static int object_index;
static int num_colors_with_bridges;
static int xref_count;

static size_t setup_time, tarjan_time, scc_setup_time, gather_xref_time, xref_setup_time, cleanup_time;
static SgenBridgeProcessor *bridge_processor;

#define BUCKET_SIZE 8184
#define NUM_SCAN_ENTRIES ((BUCKET_SIZE - SIZEOF_VOID_P * 2) / sizeof (ScanData))

typedef struct _ObjectBucket ObjectBucket;
struct _ObjectBucket {
	ObjectBucket *next;
	ScanData *next_data;
	ScanData data [NUM_SCAN_ENTRIES];
};

static ObjectBucket *root_alloc;
static ObjectBucket *cur_alloc;
static int scan_object_count;

static ObjectBucket*
new_bucket (void)
{
	ObjectBucket *res = sgen_alloc_internal (INTERNAL_MEM_TARJAN_OBJ_BUCKET);
	res->next_data = &res->data [0];
	return res;
}

static void
obj_alloc_init (void)
{
	root_alloc = cur_alloc = new_bucket ();
}

static ScanData*
alloc_data (void)
{
	ScanData *res;
retry:

	/* next_data points to the first free entry */
	res = cur_alloc->next_data;
	if (res >= &cur_alloc->data [NUM_SCAN_ENTRIES]) {
		ObjectBucket *b = new_bucket ();
		cur_alloc->next = b;
		cur_alloc = b;
		goto retry;
	}
	cur_alloc->next_data = res + 1;
	scan_object_count++;
	return res;
}

static ScanData*
create_data (MonoObject *obj)
{
	mword *o = (mword*)obj;
	ScanData *res = alloc_data ();
	res->obj = obj;
	res->index = res->low_index = res->color = -1;
	res->obj_state = o [0] & SGEN_VTABLE_BITS_MASK;
	res->lock_word = o [1];

	o [0] |= SGEN_VTABLE_BITS_MASK;
	o [1] = (mword)res;
	return res;
}

static ScanData*
find_data (MonoObject *obj)
{
	ScanData *a = NULL;
	mword *o = (mword*)obj;
	if ((o [0] & SGEN_VTABLE_BITS_MASK) == SGEN_VTABLE_BITS_MASK)
		a = (ScanData*)o [1];
	return a;
}

static void
clear_after_processing (void)	
{
	ObjectBucket *cur;

	for (cur = root_alloc; cur; cur = cur->next) {
		ScanData *sd;
		for (sd = &cur->data [0]; sd < cur->next_data; ++sd) {
			mword *o = (mword*)sd->obj;
			o [0] &= ~SGEN_VTABLE_BITS_MASK;
			o [0] |= sd->obj_state;
			o [1] = sd->lock_word;		
		}
	}
}

static MonoObject*
bridge_object_forward (MonoObject *obj)
{
	MonoObject *fwd;
	mword *o = (mword*)obj;
	if ((o [0] & SGEN_VTABLE_BITS_MASK) == SGEN_VTABLE_BITS_MASK)
		return obj;

	fwd = SGEN_OBJECT_IS_FORWARDED (obj);
	return fwd ? fwd : obj;
}


static void
clear_data (void)	
{
	ObjectBucket *cur = root_alloc;

	scan_object_count = 0;

	while (cur) {
		ObjectBucket *tmp = cur->next;
		sgen_free_internal (cur, INTERNAL_MEM_TARJAN_OBJ_BUCKET);
		cur = tmp;
	}

	root_alloc = cur_alloc = NULL;
}

static int
object_data_count (void)
{
	return scan_object_count;
}


static const char*
safe_name_bridge (MonoObject *obj)
{
	MonoVTable *vt = (MonoVTable*)SGEN_LOAD_VTABLE (obj);
	return vt->klass->name;
}

static ScanData*
find_or_create_data (MonoObject *obj)
{
	ScanData *entry = find_data (obj);
	if (!entry)
		entry = create_data (obj);
	return entry;
}

//----------
	
static int
new_color (gboolean force_new, ColorData **outcolor)
{
	ColorData *cd;
	/* XXX Try to find an equal one and return it */
	if (!force_new) {
		
	}

	cd = sgen_alloc_internal_dynamic (sizeof (ColorData), INTERNAL_MEM_BRIDGE_DATA, TRUE);
	cd->api_index = -1;
	dyn_array_int_set_all (&cd->other_colors, &low_color);

	dyn_array_ptr_add (&color_table, cd);
	*outcolor = cd;

	return dyn_array_ptr_size (&color_table) - 1;
}



static void
register_bridge_object (MonoObject *obj)
{
	find_or_create_data (obj)->is_bridge = TRUE;
}

static gboolean
is_opaque_object (MonoObject *obj)
{
	if ((obj->vtable->gc_bits & SGEN_GC_BIT_BRIDGE_OPAQUE_OBJECT) == SGEN_GC_BIT_BRIDGE_OPAQUE_OBJECT) {
		SGEN_LOG (6, "ignoring %s\n", obj->vtable->klass->name);
		++ignored_objects;
		return TRUE;
	}
	return FALSE;
}

static void
push_object (MonoObject *obj)
{
	ScanData *data;
	obj = bridge_object_forward (obj);

#if DUMP_OBJS
	printf ("\t%p %s\n", obj, safe_name_bridge (obj));
#endif

#if DUMP_GRAPH
	printf ("\t= pushing %p %s -> ", obj, safe_name_bridge (obj));
#endif
	/* Object types we can ignore */
	if (is_opaque_object (obj)) {
#if DUMP_GRAPH
		printf ("opaque\n");
#endif
		return;
	}

	data = find_data (obj);

	/* Already marked - XXX must be done this way as the bridge themselves are alive. */
	if (data && data->state != INITIAL) {
#if DUMP_GRAPH
		printf ("already marked\n");
#endif
		return;
	}

	/* We only care about dead objects */
	if (!data && sgen_object_is_live (obj)) {
#if DUMP_GRAPH
		printf ("alive\n");
#endif
		return;
	}

#if DUMP_GRAPH
	printf ("pushed!\n");
#endif

	if (!data)
		data = find_or_create_data (obj);
	g_assert (data->state == INITIAL);
	g_assert (data->index == -1);
	dyn_array_ptr_push (&scan_stack, data);
}

#undef HANDLE_PTR
#define HANDLE_PTR(ptr,obj)	do {					\
		MonoObject *dst = (MonoObject*)*(ptr);			\
		if (dst) push_object (dst); 			\
	} while (0)

static void
push_all (ScanData *data)
{
	MonoObject *obj = data->obj;
	char *start = (char*)obj;

#if DUMP_GRAPH
	printf ("**scanning %p %s\n", obj, safe_name_bridge (obj));
#endif

#if DUMP_OBJS
	printf ("> %p %s\n", obj, safe_name_bridge (obj));
#endif
	#include "sgen-scan-object.h"
}


static void
compute_low_index (ScanData *data, MonoObject *obj)
{
	ScanData *other;
	ColorData *cd;
	obj = bridge_object_forward (obj);

	other = find_data (obj);

#if DUMP_GRAPH
	printf ("\tcompute low %p ->%p (%s) %p (%d / %d)\n", data->obj, obj, safe_name_bridge (obj), other, other->index, other->low_index);
#endif
	if (!other)
		return;

	g_assert (other->state != INITIAL);

	if ((other->state == SCANNED || other->state == FINISHED_ON_STACK) && data->low_index > other->low_index)
		data->low_index = other->low_index;

	/* Compute the low color */
	if (other->color == -1)
		return;

	cd = dyn_array_ptr_get (&color_table, other->color);
	if (!cd->visited) {
		dyn_array_int_add (&low_color, other->color);
		cd->visited = TRUE;
	}
}

#undef HANDLE_PTR
#define HANDLE_PTR(ptr,obj)	do {					\
		MonoObject *dst = (MonoObject*)*(ptr);			\
		if (dst) compute_low_index (data, dst); 			\
	} while (0)

static void
compute_low (ScanData *data)
{
	MonoObject *obj = data->obj;
	char *start = (char*)obj;
	
	#include "sgen-scan-object.h"
}

static int
reduce_color (ColorData **outcolor)
{
	int color;
	int size = dyn_array_int_size (&low_color);

	if (size == 0)
		color = -1;
	else if (size == 1) {
		color = dyn_array_int_get (&low_color, 0);
		*outcolor = dyn_array_ptr_get (&color_table, color);
	} else
		color = new_color (FALSE, outcolor);

	return color;
}

static void
create_scc (ScanData *data)
{
	int i, color;
	gboolean found = FALSE;
	gboolean found_bridge = FALSE;
	ColorData *color_data = NULL;

	for (i = dyn_array_ptr_size (&loop_stack) - 1; i >= 0; --i) {
		ScanData *other = dyn_array_ptr_get (&loop_stack, i);
		found_bridge |= other->is_bridge;
		if (found_bridge || other == data)
			break;
	}

#if DUMP_GRAPH
	printf ("|SCC rooted in %s (%p) has bridge %d\n", safe_name_bridge (data->obj), data->obj, found_bridge);
	printf ("\tpoints-to-colors: ");
	for (i = 0; i < dyn_array_int_size (&low_color); ++i)
		printf ("%d ", dyn_array_int_get (&low_color, i));
	printf ("\n");

	printf ("loop stack: ");
	for (i = 0; i < dyn_array_ptr_size (&loop_stack); ++i) {
		ScanData *other = dyn_array_ptr_get (&loop_stack, i);
		printf ("(%d/%d)", other->index, other->low_index);
	}
	printf ("\n");
#endif
	if (found_bridge) {
		color = new_color (TRUE, &color_data);
		++num_colors_with_bridges;
	} else {
		color = reduce_color (&color_data);
	}

	while (dyn_array_ptr_size (&loop_stack) > 0) {
		ScanData *other = dyn_array_ptr_pop (&loop_stack);

#if DUMP_GRAPH
		printf ("\tmember %s (%p) index %d low-index %d color %d state %d\n", safe_name_bridge (other->obj), other->obj, other->index, other->low_index, other->color, other->state);
#endif

		other->color = color;
		switch (other->state) {
		case FINISHED_ON_STACK:
			other->state = FINISHED_OFF_STACK;
			break;
		case FINISHED_OFF_STACK:
			break;
		default:
			g_error ("Invalid state when building SCC %d", other->state);
		}

		if (other->is_bridge)
			dyn_array_ptr_add (&color_data->bridges, other->obj);

		if (other == data) {
			found = TRUE;
			break;
		}
	}
	g_assert (found);

	for (i = 0; i < dyn_array_int_size (&low_color); ++i) {
		ColorData *cd = dyn_array_ptr_get (&color_table, dyn_array_int_get (&low_color, i));
		g_assert (cd->visited);
		cd->visited = FALSE;
	}
	dyn_array_int_set_size (&low_color, 0);
	found_bridge = FALSE;
}

static void
dfs (void)
{
	g_assert (dyn_array_ptr_size (&scan_stack) == 1);
	g_assert (dyn_array_ptr_size (&loop_stack) == 0);

	dyn_array_int_set_size (&low_color, 0);

	while (dyn_array_ptr_size (&scan_stack) > 0) {
		ScanData *data = dyn_array_ptr_pop (&scan_stack);

		/**
		 * Ignore finished objects on stack, they happen due to loops. For example:
		 * A -> C
		 * A -> B
		 * B -> C
		 * C -> A
		 *
		 * We start scanning from A and push C before B. So, after the first iteration, the scan stack will have: A C B.
		 * We then visit B, which will find C in its initial state and push again.
		 * Finally after finish with C and B, the stack will be left with "A C" and at this point C should be ignored.
         *
         * The above explains FINISHED_ON_STACK, to explain FINISHED_OFF_STACK, consider if the root was D, which pointed
		 * to A and C. A is processed first, leaving C on stack after that in the mentioned state.
		 */
		if (data->state == FINISHED_ON_STACK || data->state == FINISHED_OFF_STACK)
			continue;

		if (data->state == INITIAL) {
			g_assert (data->index == -1);
			g_assert (data->low_index == -1);

			data->state = SCANNED;
			data->low_index = data->index = object_index++;
			dyn_array_ptr_push (&scan_stack, data);
			dyn_array_ptr_push (&loop_stack, data);

#if DUMP_GRAPH
			printf ("+scanning %s (%p) index %d color %d\n", safe_name_bridge (data->obj), data->obj, data->index, data->color);
#endif
			/*push all refs */
			push_all (data);
		} else {
			g_assert (data->state == SCANNED);
			data->state = FINISHED_ON_STACK;

#if DUMP_GRAPH
			printf ("-finishing %s (%p) index %d low-index %d color %d\n", safe_name_bridge (data->obj), data->obj, data->index, data->low_index, data->color);
#endif

			/* Compute low index */
			compute_low (data);
#if DUMP_GRAPH
			printf ("-finished %s (%p) index %d low-index %d color %d\n", safe_name_bridge (data->obj), data->obj, data->index, data->low_index, data->color);
#endif
			//SCC root
			if (data->index == data->low_index)
				create_scc (data);
		}
	}
}

static void
register_finalized_object (MonoObject *obj)
{
	g_assert (sgen_need_bridge_processing ());
	dyn_array_ptr_push (&registered_bridges, obj);
}

static void
reset_data (void)
{
	dyn_array_ptr_set_size (&registered_bridges, 0);
}

static void
cleanup (void)
{
	int i;

	for (i = 0; i <  dyn_array_ptr_size (&color_table); ++i) {
		ColorData *color_data = dyn_array_ptr_get (&color_table, i);
		dyn_array_int_uninit (&color_data->other_colors);
		dyn_array_ptr_uninit (&color_data->bridges);
		sgen_free_internal_dynamic (color_data, sizeof (ColorData), INTERNAL_MEM_BRIDGE_DATA);
	}
	dyn_array_ptr_set_size (&scan_stack, 0);
	dyn_array_ptr_set_size (&loop_stack, 0);
	dyn_array_ptr_set_size (&registered_bridges, 0);
	dyn_array_ptr_set_size (&color_table, 0);
	clear_data ();
	object_index = 0;
	num_colors_with_bridges = 0;
}

static void
dump_color_table (const char *why, gboolean do_index)
{
	int i;
	printf ("colors%s:\n", why);
	for (i = 0; i <  dyn_array_ptr_size (&color_table); ++i) {
		int j;
		ColorData *cd = dyn_array_ptr_get (&color_table, i);
		if (do_index)
			printf ("\t%d(%d):", i, cd->api_index);
		else
			printf ("\t%d: ", i);
		for (j = 0; j < dyn_array_int_size (&cd->other_colors); ++j) {
			printf ("%d ", dyn_array_int_get (&cd->other_colors, j));
		}
		if (dyn_array_ptr_size (&cd->bridges)) {
			printf (" bridges: ");
			for (j = 0; j < dyn_array_ptr_size (&cd->bridges); ++j) {
				MonoObject *obj = dyn_array_ptr_get (&cd->bridges, j);
				ScanData *data = find_or_create_data (obj);
				printf ("%d ", data->index);
			}
		}
		printf ("\n");
	}
}

static gint64
step_timer (gint64 *timer)
{
	gint64 curtime, diff;

	SGEN_TV_GETTIME (curtime); 
	diff = SGEN_TV_ELAPSED (*timer, curtime);
	*timer = curtime;
	return diff;
}
static void
processing_stw_step (void)
{
	int i;
	int bridge_count;
	gint64 curtime;

	if (!dyn_array_ptr_size (&registered_bridges))
		return;

#if defined (DUMP_GRAPH) || defined (DUMP_OBJS)
	printf ("-----------------\n");
#endif
	/*
	 * bridge_processing_in_progress must be set with the world
	 * stopped.  If not there would be race conditions.
	 */
	bridge_processing_in_progress = TRUE;

	SGEN_TV_GETTIME (curtime);

	obj_alloc_init ();

	bridge_count = dyn_array_ptr_size (&registered_bridges);
	for (i = 0; i < bridge_count ; ++i)
		register_bridge_object (dyn_array_ptr_get (&registered_bridges, i));

	setup_time = step_timer (&curtime);

	for (i = 0; i < bridge_count; ++i) {
		ScanData *sd = find_or_create_data (dyn_array_ptr_get (&registered_bridges, i));
		// ScanData *sd = find_data (dyn_array_ptr_get (&registered_bridges, i));
		if (sd->state == INITIAL) {
			dyn_array_ptr_push (&scan_stack, sd);
			dfs ();
		} else {
			g_assert (sd->state == FINISHED_OFF_STACK);
		}
	}

	tarjan_time = step_timer (&curtime);

#if defined (DUMP_GRAPH) || defined (DUMP_OBJS)
	printf ("----summary----\n");
	printf ("bridges:\n");
	for (i = 0; i < bridge_count; ++i) {
		ScanData *sd = find_or_create_data (dyn_array_ptr_get (&registered_bridges, i));
		printf ("\t%s (%p) index %d color %d\n", safe_name_bridge (sd->obj), sd->obj, sd->index, sd->color);
	}

	dump_color_table (" after tarjan", FALSE);
#endif

	clear_after_processing ();
}


static void
gather_xrefs (ColorData *color)
{
	int i;
	for (i = 0; i < dyn_array_int_size (&color->other_colors); ++i) {
		int index = dyn_array_int_get (&color->other_colors, i);
		ColorData *src = dyn_array_ptr_get (&color_table, index);
		if (src->visited)
			continue;
		src->visited = TRUE;
		if (dyn_array_ptr_size (&src->bridges))
			dyn_array_int_add (&xref_merge_array, index);
		else
			gather_xrefs (src);
	}
}

static void
reset_xrefs (ColorData *color)
{
	int i;
	for (i = 0; i < dyn_array_int_size (&color->other_colors); ++i) {
		int index = dyn_array_int_get (&color->other_colors, i);
		ColorData *src = dyn_array_ptr_get (&color_table, index);
		if (!src->visited)
			continue;
		src->visited = FALSE;
		if (!dyn_array_ptr_size (&src->bridges))
			reset_xrefs (src);
	}
}

static mono_bool
is_bridge_object_alive (MonoObject *obj, void *data)
{
	SgenHashTable *table = data;
	unsigned char *value = sgen_hash_table_lookup (table, obj);
	if (!value)
		return TRUE;
	return *value;
}

static void
processing_build_callback_data (int generation)
{
	int i, j, api_index;
	MonoGCBridgeSCC **api_sccs;
	MonoGCBridgeXRef *api_xrefs;
	gint64 curtime;

	g_assert (bridge_processor->num_sccs == 0 && bridge_processor->num_xrefs == 0);
	g_assert (!bridge_processor->api_sccs && !bridge_processor->api_xrefs);

	if (!dyn_array_ptr_size (&registered_bridges))
		return;

	SGEN_TV_GETTIME (curtime);

	/*create API objects */

#if defined (DUMP_GRAPH) || defined (DUMP_OBJS)
	printf ("***** API *****\n");
	printf ("number of SCCs %d\n", num_colors_with_bridges);
#endif

	/* This is a straightforward translation from colors to the bridge callback format. */
	api_sccs = sgen_alloc_internal_dynamic (sizeof (MonoGCBridgeSCC*) * num_colors_with_bridges, INTERNAL_MEM_BRIDGE_DATA, TRUE);
	api_index = xref_count = 0;

	for (i = 0; i < dyn_array_ptr_size (&color_table); ++i) {
		ColorData *cd = dyn_array_ptr_get (&color_table, i);
		int bridges = dyn_array_ptr_size (&cd->bridges);
		if (!bridges)
			continue;

		api_sccs [api_index] = sgen_alloc_internal_dynamic (sizeof (MonoGCBridgeSCC) + sizeof (MonoObject*) * bridges, INTERNAL_MEM_BRIDGE_DATA, TRUE);
		api_sccs [api_index]->is_alive = FALSE;
		api_sccs [api_index]->num_objs = bridges;

		cd->api_index = api_index;

		for (j = 0; j < bridges; ++j)
			api_sccs [api_index]->objs [j] = dyn_array_ptr_get (&cd->bridges, j);
		api_index++;
	}

	scc_setup_time = step_timer (&curtime);

	for (i = 0; i < dyn_array_ptr_size (&color_table); ++i) {
		ColorData *cd = dyn_array_ptr_get (&color_table, i);
		int bridges = dyn_array_ptr_size (&cd->bridges);
		if (!bridges)
			continue;

		dyn_array_int_set_size (&xref_merge_array, 0);
		gather_xrefs (cd);
		reset_xrefs (cd);
		dyn_array_int_set_all (&cd->other_colors, &xref_merge_array);
		xref_count += dyn_array_int_size (&cd->other_colors);
	}

	gather_xref_time = step_timer (&curtime);

#if defined (DUMP_GRAPH) || defined (DUMP_OBJS)
	printf ("TOTAL XREFS %d\n", xref_count);
	dump_color_table (" after xref pass", TRUE);
#endif

	api_xrefs = sgen_alloc_internal_dynamic (sizeof (MonoGCBridgeXRef) * xref_count, INTERNAL_MEM_BRIDGE_DATA, TRUE);
	api_index = 0;
	for (i = 0; i < dyn_array_ptr_size (&color_table); ++i) {
		ColorData *src = dyn_array_ptr_get (&color_table, i);
		int bridges = dyn_array_ptr_size (&src->bridges);
		if (!bridges)
			continue;

		for (j = 0; j < dyn_array_int_size (&src->other_colors); ++j) {
			ColorData *dest = dyn_array_ptr_get (&color_table, dyn_array_int_get (&src->other_colors, j));
			g_assert (dyn_array_ptr_size (&dest->bridges)); /* We flattened the color graph, so this must never happen. */

			api_xrefs [api_index].src_scc_index = src->api_index;
			api_xrefs [api_index].dst_scc_index = dest->api_index;
			++api_index;
		}
	}
	g_assert (xref_count == api_index);
	xref_setup_time = step_timer (&curtime);

#if defined (DUMP_GRAPH) || defined (DUMP_OBJS)
	printf ("---xrefs:\n");
	for (i = 0; i < xref_count; ++i)
		printf ("\t%d -> %d\n", api_xrefs [i].src_scc_index, api_xrefs [i].dst_scc_index);
#endif

	//FIXME move half of the cleanup to before the bridge callback?
	bridge_processor->num_sccs = num_colors_with_bridges;
	bridge_processor->api_sccs = api_sccs;
	bridge_processor->num_xrefs = xref_count;
	bridge_processor->api_xrefs = api_xrefs;
}

static void
processing_after_callback (int generation)
{
	gint64 curtime;
	int bridge_count = dyn_array_ptr_size (&registered_bridges);
	int object_count = object_data_count ();
	int color_count = dyn_array_ptr_size (&color_table);
	int scc_count = num_colors_with_bridges;

	SGEN_TV_GETTIME (curtime);

	/* cleanup */
	cleanup ();

	cleanup_time = step_timer (&curtime);

	mono_trace (G_LOG_LEVEL_INFO, MONO_TRACE_GC, "GC_TAR_BRIDGE bridges %d objects %d colors %d ignored %d sccs %d xref %d setup %.2fms tarjan %.2fms scc-setup %.2fms gather-xref %.2fms xref-setup %.2fms cleanup %.2fms",
		bridge_count, object_count, color_count,
		ignored_objects, scc_count, xref_count,
		setup_time / 10000.0f,
		tarjan_time / 10000.0f,
		scc_setup_time / 10000.0f,
		gather_xref_time / 10000.0f,
		xref_setup_time / 10000.0f,
		cleanup_time / 10000.0f);

	bridge_processing_in_progress = FALSE;
}

static void
describe_pointer (MonoObject *obj)
{
	// HashEntry *entry;
	int i;

	for (i = 0; i < dyn_array_ptr_size (&registered_bridges); ++i) {
		if (obj == dyn_array_ptr_get (&registered_bridges, i)) {
			printf ("Pointer is a registered bridge object.\n");
			break;
		}
	}

	// entry = sgen_hash_table_lookup (&hash_table, obj);
	// if (!entry)
	// 	return;
	// 
	// printf ("Bridge hash table entry %p:\n", entry);
	// printf ("  is bridge: %d\n", (int)entry->is_bridge);
	// printf ("  is visited: %d\n", (int)entry->is_visited);
}

void
sgen_tarjan_bridge_init (SgenBridgeProcessor *collector)
{
	collector->reset_data = reset_data;
	collector->processing_stw_step = processing_stw_step;
	collector->processing_build_callback_data = processing_build_callback_data;
	collector->processing_after_callback = processing_after_callback;
	collector->class_kind = class_kind;
	collector->register_finalized_object = register_finalized_object;
	collector->describe_pointer = describe_pointer;
	collector->enable_accounting = enable_accounting;
	// collector->set_dump_prefix = set_dump_prefix;

	sgen_register_fixed_internal_mem_type (INTERNAL_MEM_TARJAN_OBJ_BUCKET, BUCKET_SIZE);
	g_assert (sizeof (ObjectBucket) <= BUCKET_SIZE);
	bridge_processor = collector;
}

#endif
