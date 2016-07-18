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

/* maybe we could make it dynamic later */
#define TABLE_SIZE 1019

static int
hash_ptr (size_t ptr)
{
	return abs ((int)((ptr * 1737350767) ^ ((ptr * 196613) >> 16)));
}


static int
hash_str (const char *str)
{
	int hash = 0;

	while (*str++)
		hash = (hash << 5) - (hash + *str);

	return abs (hash);
}

typedef struct _AllocInfo AllocInfo;

struct _AllocInfo {
	void *address;
	size_t size;
	size_t waste;
	const char *tag;
	const char *alloc_func;
	AllocInfo *next;
};

typedef struct _TagInfo TagInfo;

/* malloc tracking */
struct _TagInfo {
	const char *tag;
	size_t size;
	size_t waste;
	size_t count;
	TagInfo *next;
};


static AllocInfo* get_alloc (void *address);

static TagInfo *tag_table [TABLE_SIZE];
static size_t rt_alloc_bytes, rt_alloc_count, rt_alloc_waste;


static void
update_tag (const char *tag, ssize_t size, ssize_t waste)
{
	int bucket = hash_str (tag) % TABLE_SIZE;

	TagInfo *info;
	for (info = tag_table [bucket]; info; info = info->next) {
		if (!strcmp (tag, info->tag))
			break;
	}

	if (!info) {
		info = malloc (sizeof (TagInfo));
		info->tag = tag;
		info->size = info->waste = info->count = 0;
		info->next = tag_table [bucket];
		tag_table [bucket] = info;
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
	AllocInfo *info = get_alloc (address);
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

/* malloc tracking */
static AllocInfo *alloc_table [TABLE_SIZE];

static size_t alloc_bytes, alloc_count, alloc_waste;

static void __attribute__((noinline))
break_on_malloc_waste (void)
{
}

static void __attribute__((noinline))
break_on_large_alloc (void)
{
}

static AllocInfo*
get_alloc (void *address)
{
	size_t addr = (size_t)address;
	int bucket = hash_ptr (addr) % TABLE_SIZE;

	AllocInfo *info;
	for (info = alloc_table [bucket]; info; info = info->next) {
		if (info->address == address)
			return info;
	}
	return NULL;
}

static void
del_alloc (void *address)
{
	size_t addr = (size_t)address;
	int bucket = hash_ptr (addr) % TABLE_SIZE;

	AllocInfo **prev;

	alloc_lock ();
	for (prev = &alloc_table [bucket]; *prev; prev = &(*prev)->next) {
		AllocInfo *info = *prev;
		if (info->address == address) {
			alloc_bytes -= info->size;
			--alloc_count;
			alloc_waste -= info->waste;

			*prev = info->next;

			if (info->tag)
				update_tag (info->tag, -info->size, -info->waste);

			g_free (info);
			break;
		}
	}
	alloc_unlock ();
}

static void
add_alloc (void *address, size_t size, const char *tag)
{
	size_t addr = (size_t)address;
	int bucket = hash_ptr (addr) % TABLE_SIZE;

	AllocInfo *info = malloc (sizeof (AllocInfo));
	info->address = address;
	info->size = size;
	info->tag = NULL;
	info->waste = malloc_size (address) - size;

	alloc_lock ();
	info->next = alloc_table [bucket];
	alloc_table [bucket] = info;

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
	TagInfo *info;
	int i;
	int max_bucket = 0;
	for (i = 0; i < TABLE_SIZE; ++i) {
		int c = 0;
		for (info = tag_table[i]; info; info = info->next) {
			++c;
			if (info->size)
				printf ("   %s: alloc %zu waste %zu count %zu\n", info->tag, info->size, info->waste, info->count);
		}
		max_bucket = MAX (max_bucket, c);
	}
	printf ("---- (collision max %d)\n", max_bucket);

	if (STRAY_ALLOCS) {
		AllocInfo *ainfo;
		for (i = 0; i < TABLE_SIZE; ++i) {
			for (ainfo = alloc_table[i]; ainfo; ainfo = ainfo->next) {
				if (!ainfo->tag)
					printf ("stay alloc from %s\n", ainfo->alloc_func);
			}
		}
	}

	if (POKE_HASH_TABLES) {
		AllocInfo *ainfo;
		for (i = 0; i < TABLE_SIZE; ++i) {
			for (ainfo = alloc_table[i]; ainfo; ainfo = ainfo->next) {
				if (!ainfo->tag || strcmp ("ghashtable", ainfo->tag))
					continue;
				printf ("hashtable size %d from %s\n", g_hash_table_size (ainfo->address), ainfo->alloc_func);
			}
		}		
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
	AllocInfo *old = get_alloc (mem);
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


