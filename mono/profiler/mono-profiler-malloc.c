/*
 * mono-profiler-aot.c: Ahead of Time Compiler Profiler for Mono.
 *
 *
 * Copyright 2008-2009 Novell, Inc (http://www.novell.com)
 *
 * This profiler collects profiling information usable by the Mono AOT compiler
 * to generate better code. It saves the information into files under ~/.mono. 
 * The AOT compiler can load these files during compilation.
 * Currently, only the order in which methods were compiled is saved, 
 * allowing more efficient function ordering in the AOT files.
 * Licensed under the MIT license. See LICENSE file in the project root for full license information.
 */

#include <config.h>
#include <mono/metadata/profiler.h>
#include <mono/metadata/tokentype.h>
#include <mono/metadata/tabledefs.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/assembly.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <glib.h>
#include <sys/stat.h>
#include <pthread.h>

//OSX only
#include <malloc/malloc.h>
#include <execinfo.h>
#include <dlfcn.h>

//XXX for now all configuration is here
#define PRINT_ALLOCATION FALSE
#define PRINT_MEMDOM FALSE
#define PRINT_RT_ALLOC FALSE
#define STRAY_ALLOCS FALSE
#define POKE_HASH_TABLES FALSE

void mono_profiler_startup (const char *desc);
static void dump_alloc_stats (void);

struct _MonoProfiler {
	int filling;
};

/* Misc stuff */
static pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;

static void
alloc_lock (void)
{
	pthread_mutex_lock (&alloc_mutex);
}

static void
alloc_unlock (void)
{
	pthread_mutex_unlock (&alloc_mutex);
}

/* Hashtable implementation. Can't use glib's as it reports memory usage itself */

/* maybe we could make it dynamic later */
#define TABLE_SIZE 1019

typedef struct _HashNode HashNode;

struct _HashNode {
	const void *key;
	HashNode *next;
};

typedef bool (*ht_equals) (const void *keyA, const void *keyB);
typedef unsigned int (*ht_hash) (const void *key);

typedef struct {
	ht_hash hash;
	ht_equals equals;
	void* table [TABLE_SIZE];
} HashTable;

static void
hashtable_init (HashTable *table, ht_hash hash, ht_equals equals)
{
	table->hash = hash;
	table->equals = equals;
	memset (table->table, 0, TABLE_SIZE * sizeof (void*));
}

static void*
hashtable_find (HashTable *table, const void *key)
{
	int bucket = table->hash (key) % TABLE_SIZE;

	HashNode *node = NULL;
	for (node = table->table [bucket]; node; node = node->next) {
		if (table->equals (key, node->key))
			break;
	}
	return node;
}

static void
hashtable_add (HashTable *table, HashNode *node, const void *key)
{
	int bucket = table->hash (key) % TABLE_SIZE;
	node->key = key;
	node->next = table->table [bucket];
	table->table [bucket] = node;
}

static HashNode*
hashtable_remove (HashTable *table, const void *key)
{
	int bucket = table->hash (key) % TABLE_SIZE;

	HashNode **prev;

	for (prev = (HashNode**)&table->table [bucket]; *prev; prev = &(*prev)->next) {
		HashNode *node = *prev;
		if (table->equals (node->key, key)) {
			*prev = node->next;
			node->next = NULL;
			return node;
		}
	}
	return NULL;
}

#define HT_FOREACH(HT,NODE_TYPE,NODE_VAR,CODE_BLOCK) {	\
	int __i;	\
	for (__i = 0; __i < TABLE_SIZE; ++__i) {	\
		HashNode *__node;	\
		for (__node = (HT)->table [__i]; __node; __node = __node->next) { 	\
			NODE_TYPE *NODE_VAR = (NODE_TYPE *)__node; 	\
			CODE_BLOCK \
		}	\
	}	\
}

static unsigned int
hash_ptr (const void *ptr)
{
	size_t addr = (size_t)ptr;
	return abs ((int)((addr * 1737350767) ^ ((addr * 196613) >> 16)));
}


static unsigned int
hash_str (const void *key)
{
	const char *str = key;
	int hash = 0;

	while (*str++)
		hash = (hash << 5) - (hash + *str);

	return abs (hash);
}

static bool
equals_ptr (const void *a, const void *b)
{
	return a == b;
}

static bool
equals_str (const void *a, const void *b)
{
	return !strcmp (a, b);
}

typedef struct {
	HashNode node;
	size_t size;
	size_t waste;
	const char *tag;
	const char *alloc_func;
} AllocInfo;

/* malloc tracking */
typedef struct {
	HashNode node;
	size_t size;
	size_t waste;
	size_t count;
} TagInfo;


static HashTable tag_table;
static size_t rt_alloc_bytes, rt_alloc_count, rt_alloc_waste;

static HashTable alloc_table;
static size_t alloc_bytes, alloc_count, alloc_waste;



static void
update_tag (const char *tag, ssize_t size, ssize_t waste)
{
	TagInfo *info = hashtable_find (&tag_table, tag);

	if (!info) {
		info = malloc (sizeof (TagInfo));
		info->size = info->waste = info->count = 0;
		hashtable_add (&tag_table, &info->node, tag);
	}

	info->size += size;
	info->waste += waste;

	rt_alloc_bytes += size;
	rt_alloc_waste += waste;

	if (size > 0) {
		++info->count;
		++rt_alloc_count;
	} else {
		--info->count;
		--rt_alloc_count;
	}
}

static void __attribute__((noinline))
break_on_bad_runtime_malloc (void)
{
}

static const char *memdom_name[] = {
	"invalid",
	"appdomain",
	"image",
	"image-set",
};


static void
memdom_new (MonoProfiler *prof, void* memdom, MonoProfilerMemoryDomain kind)
{
	if (PRINT_MEMDOM)
		printf ("memdom new %p type %s\n", memdom, memdom_name [kind]);
	dump_alloc_stats ();
}

static void
memdom_destroy (MonoProfiler *prof, void* memdom)
{
	if (PRINT_MEMDOM)
		printf ("memdom destroy %p\n", memdom);
	dump_alloc_stats ();
}

static void
memdom_alloc (MonoProfiler *prof, void* memdom, size_t size, const char *tag)
{
	if (PRINT_RT_ALLOC)
		printf ("memdom %p alloc %zu %s\n", memdom, size, tag);
}

static const char*
filter_bt (int skip, const char *filter_funcs)
{
	void *bt_ptrs [10];
 	int c = backtrace (bt_ptrs, 10);
	if (c) {
		int i;
		const char *it = NULL;
		for (i = 0; i < c - (skip + 1); ++i) {
			Dl_info info;
			if (!dladdr (bt_ptrs [skip + i], &info))
				continue;
			if (strstr (info.dli_sname, "_malloc") || strstr (info.dli_sname, "report_alloc"))
				continue;
			if (strstr (info.dli_sname, filter_funcs))
				continue;
			it = info.dli_sname;
			break;
		}
		return it;
	}
	return NULL;
}

static void
runtime_malloc_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	alloc_lock ();
	AllocInfo *info = hashtable_find (&alloc_table, address);
	if (!info) {
		printf ("stray alloc that didn't come from g_malloc %p\n", address);
		break_on_bad_runtime_malloc ();
		goto done;
	}

	if (info->tag) {
		printf ("runtime reported same pointer twice %p %s x %s\n", address, info->tag, tag);
		break_on_bad_runtime_malloc ();
		goto done;
	}

	if (info->size != size) {
		printf ("runtime reported pointer with different sizes %p %zu x %zu\n", address, info->size, size);
		break_on_bad_runtime_malloc ();
		goto done;
	}

	info->tag = tag;
	update_tag (tag, info->size, info->waste);

	if (!strcmp (tag, "ghashtable")) {
		const char *f = filter_bt (3, "g_hash_table");
		// printf ("hashtable %p allocated from %s\n", address, f);
		info->alloc_func = f;
	}

done:
	alloc_unlock ();

	dump_alloc_stats ();
}

static void
runtime_free_event (MonoProfiler *prof, void *address)
{
	// if (PRINT_RT_ALLOC)
	// 	printf ("free %p\n", address);
}

static void
runtime_valloc_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	if (PRINT_RT_ALLOC)
		printf ("valloc %p %zu %s\n", address, size, tag);
}

static void
runtime_vfree_event (MonoProfiler *prof, void *address, size_t size)
{
	if (PRINT_RT_ALLOC)
		printf ("vfree %p %zu\n", address, size);
}

static void __attribute__((noinline))
break_on_malloc_waste (void)
{
}

static void __attribute__((noinline))
break_on_large_alloc (void)
{
}

static void
del_alloc (void *address)
{
	alloc_lock ();
	AllocInfo *info = (AllocInfo*)hashtable_remove (&alloc_table, address);

	if (info) {
		alloc_bytes -= info->size;
		--alloc_count;
		alloc_waste -= info->waste;

		if (info->tag)
			update_tag (info->tag, -info->size, -info->waste);

		g_free (info);
	}

	alloc_unlock ();
}

static void
add_alloc (void *address, size_t size, const char *tag)
{
	AllocInfo *info = malloc (sizeof (AllocInfo));
	info->size = size;
	info->tag = NULL;
	info->waste = malloc_size (address) - size;

	alloc_lock ();
	hashtable_add (&alloc_table, &info->node, address);

	alloc_bytes += size;
	++alloc_count;
	alloc_waste += info->waste;

	if (STRAY_ALLOCS) {
		int skip = 2;
		void *bt_ptrs [10];
	 	int c = backtrace (bt_ptrs, 10);
		if (c) {
			int i;
			const char *it = NULL;
			for (i = 0; i < c - (skip + 1); ++i) {
				Dl_info info;
				if (!dladdr (bt_ptrs [skip + i], &info))
					continue;
				if (strstr (info.dli_sname, "g_malloc") || strstr (info.dli_sname, "g_calloc") || strstr (info.dli_sname, "m_malloc")
					|| strstr (info.dli_sname, "g_realloc") || strstr (info.dli_sname, "g_memdup") || strstr (info.dli_sname, "g_slist_alloc") || strstr (info.dli_sname, "g_list_alloc")
					|| strstr (info.dli_sname, "g_strndup") || strstr (info.dli_sname, "g_vasprintf") || strstr (info.dli_sname, "g_slist_prepend"))
					continue;
				it = info.dli_sname;
				break;
			}
			info->alloc_func = it;
		}
	}

	alloc_unlock ();

	if (info->waste > 10)
		break_on_malloc_waste ();
	if (size > 1000)
		break_on_large_alloc ();
}

static void
dump_stats (void)
{
	alloc_lock ();
	printf ("-------\n");
	printf ("alloc %zu alloc count %zu waste %zu\n", alloc_bytes, alloc_count, alloc_waste);
	printf ("rt alloc %zu alloc count %zu waste %zu\n", rt_alloc_bytes, rt_alloc_count, rt_alloc_waste);
	printf ("   reported %.2f memory and %.2f allocs\n",
		rt_alloc_bytes / (float)alloc_bytes,
		rt_alloc_count / (float)alloc_count);

	printf ("tag summary:\n");

	HT_FOREACH (&tag_table, TagInfo, info, {
		if (info->size)
			printf ("   %s: alloc %zu waste %zu count %zu\n", info->node.key, info->size, info->waste, info->count);
	});
	printf ("----\n");

	if (STRAY_ALLOCS) {
		HT_FOREACH (&tag_table, AllocInfo, info, {
				if (!info->tag)
					printf ("stay alloc from %s\n", info->alloc_func);
		});
	}

	if (POKE_HASH_TABLES) {
		HT_FOREACH (&tag_table, AllocInfo, info, {
			if (!info->tag || strcmp ("ghashtable", info->tag))
				continue;
			printf ("hashtable size %d from %s\n", g_hash_table_size ((GHashTable*)info->node.key), info->alloc_func);
		});
	}


	alloc_unlock ();
}

static void
icall_dump_allocs (void)
{
	dump_stats ();
}

static void
dump_alloc_stats (void)
{
	static int last_time;

	if (!PRINT_ALLOCATION)
		return;
	++last_time;
	if (last_time % 20)
		return;
	dump_stats ();
}

static void *
platform_malloc (size_t size)
{	
	void * res = malloc (size);
	add_alloc (res, size, NULL);

	dump_alloc_stats ();
	return res;
}

static void *
platform_realloc (void *mem, size_t count)
{
	AllocInfo *old = hashtable_find (&alloc_table, mem);
	const char *tag = old ? old->tag : NULL;

	del_alloc (mem);
	void * res = realloc (mem, count);
	add_alloc (res, count, tag);

	dump_alloc_stats ();
	return res;
 }

static void
platform_free (void *mem)
{
	del_alloc (mem);
	free (mem);

	dump_alloc_stats ();
}

static void*
platform_calloc (size_t count, size_t size)
{
	void * res = calloc (count, size);
	add_alloc (res, count * size, NULL);

	dump_alloc_stats ();
	return res;
}

static void
runtime_inited (MonoProfiler *prof)
{
	mono_add_internal_call ("Mono.MemProf::DumpAllocationInfo", icall_dump_allocs);
}

static void
prof_shutdown (MonoProfiler *prof)
{
}

/* the entry point */
void
mono_profiler_startup (const char *desc)
{
	MonoProfiler *prof;

	prof = g_new0 (MonoProfiler, 1);

	hashtable_init (&tag_table, hash_str, equals_str);
	hashtable_init (&alloc_table, hash_ptr, equals_ptr);
	mono_profiler_install (prof, prof_shutdown);
	mono_profiler_install_memdom (memdom_new, memdom_destroy, memdom_alloc);
	mono_profiler_install_malloc (runtime_malloc_event, runtime_free_event);
	mono_profiler_install_valloc (runtime_valloc_event, runtime_vfree_event);
	mono_profiler_install_runtime_initialized (runtime_inited);

	MonoAllocatorVTable alloc_vt = {
		.version = MONO_ALLOCATOR_VTABLE_VERSION,
		.malloc = platform_malloc,
		.realloc = platform_realloc,
		.free = platform_free,
		.calloc = platform_calloc
	};

	if (!mono_set_allocator_vtable (&alloc_vt))
		printf ("set allocator failed :(\n");


}


