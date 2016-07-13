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

//XXX for now all configuration is here
#define PRINT_ALLOCATION FALSE
#define PRINT_MEMDOM FALSE
#define PRINT_RT_ALLOC FALSE

void mono_profiler_startup (const char *desc);
static void dump_alloc_stats (void);

struct _MonoProfiler {
	int filling;
};


static void
prof_shutdown (MonoProfiler *prof)
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

static void
runtime_malloc_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	if (PRINT_RT_ALLOC)
		printf ("malloc %p %zu %s\n", address, size, tag);
}

static void
runtime_free_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	if (PRINT_RT_ALLOC)
		printf ("free %p %zu %s\n", address, size, tag);
}

static void
runtime_valloc_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	if (PRINT_RT_ALLOC)
		printf ("valloc %p %zu %s\n", address, size, tag);
}

static void
runtime_vfree_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	if (PRINT_RT_ALLOC)
		printf ("vfree %p %zu %s\n", address, size, tag);
}


typedef struct _AllocInfo AllocInfo;

/* malloc tracking */
struct _AllocInfo {
	void *address;
	size_t size;
	size_t waste;
	AllocInfo *next;
};

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
static AllocInfo *alloc_table [TABLE_SIZE];

static size_t alloc_bytes, alloc_count, alloc_waste;


static void __attribute__((noinline))
break_on_malloc_waste (void)
{
	
}

static size_t
hash_ptr (size_t ptr)
{
	return (int)((ptr * 1737350767) ^ ((ptr * 196613) >> 16));
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
			g_free (info);
			break;
		}
	}
	alloc_unlock ();
}


static void
add_alloc (void *address, size_t size)
{
	size_t addr = (size_t)address;
	int bucket = hash_ptr (addr) % TABLE_SIZE;

	AllocInfo *info = malloc (sizeof (AllocInfo));
	info->address = address;
	info->size = size;
	info->waste = malloc_size (address) - size;

	alloc_lock ();
	info->next = alloc_table [bucket];
	alloc_table [bucket] = info;

	alloc_bytes += size;
	++alloc_count;
	alloc_waste += info->waste;

	alloc_unlock ();

	if (info->waste > 10)
		break_on_malloc_waste ();

}

static void
dump_alloc_stats (void)
{
	static int last_time;

	if (!PRINT_ALLOCATION)
		return;
	++last_time;
	if (last_time % 100)
		return;
	printf ("alloc %zu alloc count %zu waste %zu\n",
		alloc_bytes, alloc_count, alloc_waste);
}


static void *
platform_malloc (size_t size)
{
	void * res = malloc (size);
	add_alloc (res, size);

	dump_alloc_stats ();
	return res;
}

static void *
platform_realloc (void *mem, size_t count)
{
	del_alloc (mem);
	void * res = realloc (mem, count);
	add_alloc (res, count);

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
	add_alloc (res, count * size);

	dump_alloc_stats ();
	return res;
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


