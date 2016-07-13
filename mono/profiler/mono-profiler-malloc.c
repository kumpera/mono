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

//OSX only
#include <malloc/malloc.h>


void mono_profiler_startup (const char *desc);


struct _MonoProfiler {
	int filling;
};

static size_t alloc_bytes, alloc_count, malloc_waste, realloc_waste;

static void
prof_shutdown (MonoProfiler *prof)
{
	printf ("profiling done\n");
}

static const char *memdom_name[] = {
	"invalid",
	"appdomain",
	"image",
	"image-set",
};

static int last_time;
static void
dump_alloc (void)
{
	++last_time;
	if (last_time % 100)
		return;
	printf ("alloc %zu alloc count %zu malloc waste %zu realloc waste %zu\n",
		alloc_bytes, alloc_count, malloc_waste, realloc_waste);
}

static void
memdom_new (MonoProfiler *prof, void* memdom, MonoProfilerMemoryDomain kind)
{
	printf ("memdom new %p type %s\n", memdom, memdom_name [kind]);
	dump_alloc ();
}

static void
memdom_destroy (MonoProfiler *prof, void* memdom)
{
	printf ("memdom destroy %p\n", memdom);
	dump_alloc ();
}

static void
memdom_alloc (MonoProfiler *prof, void* memdom, size_t size, const char *tag)
{
	printf ("memdom %p alloc %zu %s\n", memdom, size, tag);
}

static void
runtime_malloc_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	printf ("malloc %p %zu %s\n", address, size, tag);
}

static void
runtime_free_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	printf ("free %p %zu %s\n", address, size, tag);
}

static void
runtime_valloc_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	printf ("valloc %p %zu %s\n", address, size, tag);
}

static void
runtime_vfree_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	printf ("vfree %p %zu %s\n", address, size, tag);
}

static void __attribute__((noinline))
break_on_malloc_waste (void)
{
	
}

static void *
platform_malloc (size_t size)
{
	__sync_fetch_and_add (&alloc_bytes, malloc_good_size (size));
	__sync_fetch_and_add (&alloc_count, 1);
	__sync_fetch_and_add (&malloc_waste, (malloc_good_size (size) - size));
	dump_alloc ();

	size_t waste = (malloc_good_size (size) - size);
	if (waste > 10)
		break_on_malloc_waste ();

	return malloc (size);
}

static void *
platform_realloc (void *mem, size_t count)
{
	size_t extra = malloc_good_size (count) - malloc_size (mem);

	__sync_fetch_and_add (&alloc_bytes, extra);
	__sync_fetch_and_add (&alloc_count, 1);
	__sync_fetch_and_add (&realloc_waste, (malloc_good_size (count) - count));
	dump_alloc ();
	 return realloc (mem, count);
 }

static void
platform_free (void *mem)
{
	__sync_fetch_and_add (&alloc_count, -1);
	__sync_fetch_and_add (&alloc_bytes, -malloc_size (mem));
	dump_alloc ();	
}

static void*
platform_calloc (size_t count, size_t size)
{
	size_t actual = count * size;
	__sync_fetch_and_add (&alloc_bytes, malloc_good_size (actual));
	__sync_fetch_and_add (&alloc_count, 1);
	__sync_fetch_and_add (&malloc_waste, (malloc_good_size (actual) - actual));

	dump_alloc ();
	 return calloc (count, size);
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


