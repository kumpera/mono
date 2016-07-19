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

#include <mono/metadata/domain-internals.h>
#include <mono/metadata/metadata-internals.h>

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

static void
hashtable_cleanup (HashTable *table)
{
	int i;

	for (i = 0; i < TABLE_SIZE; ++i) {
		HashNode *node = NULL;
		for (node = table->table [i]; node;) {
			HashNode *next = node->next;
			g_free (node);
			node = next;
		}
		table->table [i] = NULL;
	}
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

typedef struct {
	HashTable table;
	size_t alloc_bytes;
	size_t alloc_waste;
	size_t alloc_count;
} TagBag;

typedef struct {
	HashNode node;
	int kind;
	TagBag tags;
} MemDomInfo;

static TagBag malloc_tags;
static HashTable alloc_table;
static size_t alloc_bytes, alloc_count, alloc_waste;

static void
tagbag_init (TagBag *bag)
{
	hashtable_init (&bag->table, hash_str, equals_str);
	bag->alloc_bytes = bag->alloc_waste = bag->alloc_count = 0;
}

static void
tagbag_cleanup (TagBag *bag)
{
	hashtable_cleanup (&bag->table);
}

static void __attribute__((noinline))
break_on_zero_size (void)
{
}

static void
update_tag (TagBag *tag_bag, const char *tag, ssize_t size, ssize_t waste)
{
	TagInfo *info = hashtable_find (&tag_bag->table, tag);

	if (size == 0)
		break_on_zero_size ();
	// if (!strcmp ("class:fields", tag)) {
	// 	printf ("update %s root %d size %zd\n", tag, tag_bag == &malloc_tags, size);
	// }

	if (!info) {
		info = malloc (sizeof (TagInfo));
		info->size = info->waste = info->count = 0;
		hashtable_add (&tag_bag->table, &info->node, tag);
	}

	info->size += size;
	info->waste += waste;

	tag_bag->alloc_bytes += size;
	tag_bag->alloc_waste += waste;

	if (size >= 0) {
		++info->count;
		++tag_bag->alloc_count;
	} else {
		--info->count;
		--tag_bag->alloc_count;
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

#define MEMDOM_MAX 4
static size_t memdom_count [MEMDOM_MAX];
static HashTable memdom_table;

static void
memdom_new (MonoProfiler *prof, void* memdom, MonoProfilerMemoryDomain kind)
{
	alloc_lock ();
	MemDomInfo *info = malloc (sizeof (MemDomInfo));
	info->kind = kind;
	tagbag_init (&info->tags);

	hashtable_add (&memdom_table, &info->node, memdom);
	++memdom_count [kind];


	if (PRINT_MEMDOM)
		printf ("memdom new %p type %s\n", memdom, memdom_name [kind]);
	alloc_unlock ();
}

static void
memdom_destroy (MonoProfiler *prof, void* memdom)
{
	alloc_lock ();
	MemDomInfo *info = (MemDomInfo*)hashtable_remove (&memdom_table, memdom);

	if (info) {
		--memdom_count [info->kind];
		tagbag_cleanup (&info->tags);
		g_free (info);
	}

	if (PRINT_MEMDOM)
		printf ("memdom destroy %p\n", memdom);
	alloc_unlock ();
}

static void
memdom_alloc (MonoProfiler *prof, void* memdom, size_t size, const char *tag)
{
	alloc_lock ();
	MemDomInfo *info = hashtable_find (&memdom_table, memdom);
	if (info)
		update_tag (&info->tags, tag, size, 0);
	alloc_unlock ();
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
	update_tag (&malloc_tags, tag, info->size, info->waste);

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
			update_tag (&malloc_tags, info->tag, -info->size, -info->waste);

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
dump_memdom (MemDomInfo *memdom)
{
	char name [1024];
	name [0] = 0;
	MonoMemPool *mempool = NULL;
	switch (memdom->kind) {
	case MONO_PROFILE_MEMDOM_APPDOMAIN: {
		MonoDomain *domain = (MonoDomain *)memdom->node.key;
		snprintf (name, 1023, "domain_%d", domain->domain_id);
		mempool = domain->mp;
		break;
	}

	case MONO_PROFILE_MEMDOM_IMAGE: {
		MonoImage *image = (MonoImage *)memdom->node.key;
		snprintf (name, 1023, "image_%s", image->module_name);
		mempool = image->mempool;
		break;
	}

	case MONO_PROFILE_MEMDOM_IMAGE_SET: {
		MonoImageSet *set = (MonoImageSet*)memdom->node.key;
		strlcat (name, "imageset", sizeof (name));
		int i;
		for (i = 0; i < set->nimages; ++i) {
			if (strlcat (name, "_", sizeof (name)) >= sizeof(name))
				break;
			if (strlcat (name, set->images [i]->module_name, sizeof (name)) >= sizeof(name))
				break;
		}
		mempool = set->mempool;
		break;
	}
	}
	printf ("%s:\n", name);

	size_t mt_reported = 0;
	HT_FOREACH (&memdom->tags.table, TagInfo, info, {
		if (info->size) {
			printf ("   %s: alloc %zu waste %zu count %zu\n", info->node.key, info->size, info->waste, info->count);
			mt_reported += info->size;
		}
	});
	if (mt_reported)
		printf (">> REPORTED %zu bytes allocated %d\n", mt_reported, mono_mempool_get_allocated (mempool));
	printf ("...\n");
}

static void
dump_stats (void)
{
	int i;
	alloc_lock ();
	printf ("-------\n");
	printf ("alloc %zu alloc count %zu waste %zu\n", alloc_bytes, alloc_count, alloc_waste);
	printf ("rt alloc %zu alloc count %zu waste %zu\n", malloc_tags.alloc_bytes, malloc_tags.alloc_count, malloc_tags.alloc_waste);
	printf ("   reported %.2f memory and %.2f allocs\n",
		malloc_tags.alloc_bytes / (float)alloc_bytes,
		malloc_tags.alloc_count / (float)alloc_count);

	printf ("tag summary:\n");

	HT_FOREACH (&malloc_tags.table, TagInfo, info, {
		if (info->size)
			printf ("   %s: alloc %zu waste %zu count %zu\n", info->node.key, info->size, info->waste, info->count);
	});
	printf ("----\n");

	if (STRAY_ALLOCS) {
		HT_FOREACH (&alloc_table, AllocInfo, info, {
				if (!info->tag)
					printf ("stay alloc from %s\n", info->alloc_func);
		});
	}

	if (POKE_HASH_TABLES) {
		HT_FOREACH (&alloc_table, AllocInfo, info, {
			if (!info->tag || strcmp ("ghashtable", info->tag))
				continue;
			printf ("hashtable size %d from %s\n", g_hash_table_size ((GHashTable*)info->node.key), info->alloc_func);
		});
	}

	printf ("---memdom:\n");
	for (i = 1; i < MEMDOM_MAX; ++i)
		printf ("\t%s count %zu\n", memdom_name [i], memdom_count [i]);

	HT_FOREACH (&memdom_table, MemDomInfo, info, {
		dump_memdom (info);
	});
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

	tagbag_init (&malloc_tags);
	hashtable_init (&alloc_table, hash_ptr, equals_ptr);
	hashtable_init (&memdom_table, hash_ptr, equals_ptr);

	mono_profiler_install (prof, prof_shutdown);
	mono_profiler_install_memdom (memdom_new, memdom_destroy, memdom_alloc);
	mono_profiler_install_malloc (runtime_malloc_event);
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
