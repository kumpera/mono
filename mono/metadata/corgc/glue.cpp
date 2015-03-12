#include "config.h"

#include "common.h"

#include "gcenv.h"

#include "gc.h"
#include "objecthandle.h"

#include "gcdesc.h"

#include <glib.h>
#include <pthread.h>


#if HAVE_SYSCTL
#include <sys/sysctl.h>
#endif

#include <sys/param.h>
#if HAVE_SYS_VMPARAM_H
#include <sys/vmparam.h>
#endif  // HAVE_SYS_VMPARAM_H

#if TARGET_MACH
#include <mach/vm_types.h>
#include <mach/vm_param.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#endif

GCSystemInfo g_SystemInfo;

extern "C" {
int mono_cpu_count ();
int mono_pagesize ();
guint32 mono_msec_ticks ();
gint64  mono_100ns_ticks ();

MethodTable* corgc_get_array_fill_vtable ();
int cor_preempt_gc_get ();
void cor_preempt_gc_set(int);
void *cor_thread_next(void*);
void *cor_get_thread_alloc_ctx(void*);
void cor_set_cur_thread_as_special ();
void cor_destroy_thread (void*);
void *cor_thread_current ();

#include <mono/utils/atomic.h>
#include <mono/utils/mono-mmap.h>

}

MethodTable * g_pFreeObjectMethodTable;

void InitializeSystemInfo()
{
    g_SystemInfo.dwNumberOfProcessors = mono_cpu_count ();
    g_SystemInfo.dwPageSize = mono_pagesize ();
    g_SystemInfo.dwAllocationGranularity = mono_pagesize ();
}

extern "C" void
corgc_init ()
{
	
	InitializeSystemInfo ();

	g_pFreeObjectMethodTable = corgc_get_array_fill_vtable ();
    if (!Ref_Initialize())
        g_error ("FAILED to init handle table");

    GCHeap *pGCHeap = GCHeap::CreateGCHeap();
    if (!pGCHeap)
        g_error ("FAILED to create GC HEAP");


    if (FAILED(pGCHeap->Initialize()))
        g_error ("FAILED to initialize GC HEAP");
}

extern "C" void
corgc_attach ()
{
	ThreadStore::AttachCurrentThread(false);	
}


/* glue code for the PAL stuff */

extern "C" void 
DebugBreak()
{
	G_BREAKPOINT ();
}

extern "C" void
FlushProcessWriteBuffers()
{
	g_error ("FlushProcessWriteBuffers");
}

extern "C" DWORD
GetCurrentThreadId()
{
	return (DWORD)(size_t)pthread_self ();
}

extern "C" DWORD
GetTickCount()
{
	return mono_msec_ticks ();
}

extern "C" BOOL
QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
	lpPerformanceCount->QuadPart = mono_100ns_ticks ();
	return TRUE;
}

extern "C" BOOL
QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
	lpFrequency->QuadPart = 10 * 1000; //100ns
	return TRUE;
}


extern "C" UINT 
WINAPI
GetWriteWatch(
  DWORD dwFlags,
  PVOID lpBaseAddress,
  SIZE_T dwRegionSize,
  PVOID *lpAddresses,
  ULONG_PTR * lpdwCount,
  ULONG * lpdwGranularity
)
{
	g_error ("GetWriteWatch");
}

extern "C" UINT
ResetWriteWatch(
  LPVOID lpBaseAddress,
  SIZE_T dwRegionSize
)
{
	g_error ("ResetWriteWatch");
}

extern "C" BOOL
 VirtualUnlock(
  LPVOID lpAddress,
  SIZE_T dwSize
)
{
	//FIXME this can be implemented in terms of mprotect
	printf ("VirtualUnlock %p %zx\n", lpAddress, dwSize);
	return TRUE;
}

extern "C" VOID
YieldProcessor ()
{
	sched_yield ();
}

bool
__SwitchToThread (uint32_t dwSleepMSec, uint32_t dwSwitchCount)
{
	sched_yield ();
	return false;
}

void
GetProcessMemoryLoad(
            LPMEMORYSTATUSEX lpBuffer)
{

    // PERF_ENTRY(GlobalMemoryStatusEx);
    // ENTRY("GlobalMemoryStatusEx (lpBuffer=%p)\n", lpBuffer);

    lpBuffer->dwMemoryLoad = 0;
    lpBuffer->ullTotalPhys = 0;
    lpBuffer->ullAvailPhys = 0;
    lpBuffer->ullTotalPageFile = 0;
    lpBuffer->ullAvailPageFile = 0;
    lpBuffer->ullTotalVirtual = 0;
    lpBuffer->ullAvailVirtual = 0;
    lpBuffer->ullAvailExtendedVirtual = 0;

    BOOL fRetVal = FALSE;

    // Get the physical memory size
#if HAVE_SYSCONF && HAVE__SC_PHYS_PAGES
    int64_t physical_memory;

    // Get the Physical memory size
    physical_memory = sysconf( _SC_PHYS_PAGES ) * sysconf( _SC_PAGE_SIZE );
    lpBuffer->ullTotalPhys = (DWORDLONG)physical_memory;
    fRetVal = TRUE;
#elif HAVE_SYSCTL
    int mib[2];
    int64_t physical_memory;
    size_t length;

    // Get the Physical memory size
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    length = sizeof(INT64);
    int rc = sysctl(mib, 2, &physical_memory, &length, NULL, 0);
    if (rc != 0)
    {
        ASSERT("sysctl failed for HW_MEMSIZE (%d)\n", errno);
    }
    else
    {
        lpBuffer->ullTotalPhys = (DWORDLONG)physical_memory;
        fRetVal = TRUE;
    }
#elif HAVE_SYSINFO
    // TODO: implement getting memory details via sysinfo. On Linux, it provides swap file details that
    // we can use to fill in the xxxPageFile members.

#endif // HAVE_SYSCONF

    // Get the physical memory in use - from it, we can get the physical memory available.
    // We do this only when we have the total physical memory available.
    if (lpBuffer->ullTotalPhys > 0)
    {
#if HAVE_SYSCONF && HAVE__SC_AVPHYS_PAGES
        lpBuffer->ullAvailPhys = sysconf(_SC_AVPHYS_PAGES) * sysconf(_SC_PAGE_SIZE);
        INT64 used_memory = lpBuffer->ullTotalPhys - lpBuffer->ullAvailPhys;
        lpBuffer->dwMemoryLoad = (DWORD)((used_memory * 100) / lpBuffer->ullTotalPhys);
#else
        vm_size_t page_size;
        mach_port_t mach_port;
        mach_msg_type_number_t count;
        vm_statistics_data_t vm_stats;
        mach_port = mach_host_self();
        count = sizeof(vm_stats) / sizeof(natural_t);
        if (KERN_SUCCESS == host_page_size(mach_port, &page_size))
        {
            if (KERN_SUCCESS == host_statistics(mach_port, HOST_VM_INFO, (host_info_t)&vm_stats, &count))
            {
                lpBuffer->ullAvailPhys = (int64_t)vm_stats.free_count * (int64_t)page_size;
                INT64 used_memory = ((INT64)vm_stats.active_count + (INT64)vm_stats.inactive_count + (INT64)vm_stats.wire_count) *  (INT64)page_size;
                lpBuffer->dwMemoryLoad = (DWORD)((used_memory * 100) / lpBuffer->ullTotalPhys);
            }
        }
#endif // HAVE_SYSCONF
    }

    // TODO: figure out a way to get the real values for the total / available virtual
    lpBuffer->ullTotalVirtual = lpBuffer->ullTotalPhys;
    lpBuffer->ullAvailVirtual = lpBuffer->ullAvailPhys;

    // LOGEXIT("GlobalMemoryStatusEx returns %d\n", fRetVal);
    // PERF_EXIT(GlobalMemoryStatusEx);

    // return fRetVal;
}
//Host env

void DestroyThread(Thread * pThread)
{
	cor_destroy_thread ((void*)pThread);
}

bool PalStartBackgroundGCThread(BackgroundCallback callback, void* pCallbackContext)
{
	g_error ("PalStartBackgroundGCThread");
    return false;
}


// #define MEM_COMMIT              0x1000
// #define MEM_RESERVE             0x2000
// #define MEM_DECOMMIT            0x4000
// #define MEM_RELEASE             0x8000
// #define MEM_RESET               0x80000
//
// #define PAGE_NOACCESS           0x01
// #define PAGE_READWRITE          0x04
//
void * ClrVirtualAlloc(
    void * lpAddress,
    size_t dwSize,
    uint32_t flAllocationType,
    uint32_t flProtect)
{
	int flags = 0;

	if (flProtect & PAGE_NOACCESS)
		flags |= MONO_MMAP_NONE;
	if (flProtect & PAGE_READWRITE)
		flags |= MONO_MMAP_READ | MONO_MMAP_WRITE;

	if (flAllocationType & MONO_MMAP_DISCARD)
		g_error ("MONO_MMAP_DISCARD");
	// MONO_MMAP_NONE = 0,
	// MONO_MMAP_READ    = 1 << 0,
	// MONO_MMAP_WRITE   = 1 << 1,
	// MONO_MMAP_EXEC    = 1 << 2,
	// /* make the OS discard the dirty data and fill with 0 */
	// MONO_MMAP_DISCARD = 1 << 3,
	// /* other flags (add commit, sync) */
	// MONO_MMAP_PRIVATE = 1 << 4,
	// MONO_MMAP_SHARED  = 1 << 5,
	// MONO_MMAP_ANON    = 1 << 6,
	// MONO_MMAP_FIXED   = 1 << 7,
	// MONO_MMAP_32BIT   = 1 << 8
	//
	return mono_valloc (lpAddress, dwSize, flags);
}
//
// void * ClrVirtualAllocAligned(
//     void * lpAddress,
//     size_t dwSize,
//     uint32_t flAllocationType,
//     uint32_t flProtect,
//     size_t dwAlignment);
//
// bool ClrVirtualFree(
//         void * lpAddress,
//         size_t dwSize,
//         uint32_t dwFreeType);
//
// bool
// ClrVirtualProtect(
//            void * lpAddress,
//            size_t dwSize,
//            uint32_t flNewProtect,
//            uint32_t * lpflOldProtect);

//Thread:

Thread * GetThread()
{
	return (Thread*)cor_thread_current ();
}
	
bool Thread::PreemptiveGCDisabled()
{
	return cor_preempt_gc_get () == 0;
}

void Thread::EnablePreemptiveGC()
{
	cor_preempt_gc_set (1);
}

void Thread::DisablePreemptiveGC()
{
	cor_preempt_gc_set (0);
}

alloc_context* Thread::GetAllocContext()
{
	return (alloc_context*)cor_get_thread_alloc_ctx ((void*)this);
}

void Thread::SetGCSpecial(bool fGCSpecial)
{
	g_assert (fGCSpecial);
	cor_set_cur_thread_as_special ();
}
//Thread store

Thread * ThreadStore::GetThreadList(Thread * pThread)
{
	return (Thread *)cor_thread_next ((void*)pThread);
}


void ThreadStore::AttachCurrentThread(bool fAcquireThreadStoreLock)
{
	//FIXME can't honor fAcquireThreadStoreLock == false at this point
	g_error ("AttachCurrentThread");
}

//PAL stuff
int32_t FastInterlockIncrement(int32_t volatile *lpAddend)
{
    return InterlockedIncrement((LONG *)lpAddend);
}

int32_t FastInterlockDecrement(int32_t volatile *lpAddend)
{
    return InterlockedDecrement((LONG *)lpAddend);
}

int32_t FastInterlockExchange(int32_t volatile *Target, int32_t Value)
{
    return InterlockedExchange((LONG *)Target, Value);
}

int32_t FastInterlockCompareExchange(int32_t volatile *Destination, int32_t Exchange, int32_t Comperand)
{
    return InterlockedCompareExchange((LONG *)Destination, Exchange, Comperand);
}

int32_t FastInterlockExchangeAdd(int32_t volatile *Addend, int32_t Value)
{
    return InterlockedExchangeAdd((LONG *)Addend, Value);
}

void * _FastInterlockExchangePointer(void * volatile *Target, void * Value)
{
    return InterlockedExchangePointer(Target, Value);
}

void * _FastInterlockCompareExchangePointer(void * volatile *Destination, void * Exchange, void * Comperand)
{
    return InterlockedCompareExchangePointer(Destination, Exchange, Comperand);
}

void FastInterlockOr(uint32_t volatile *p, uint32_t msk)
{
	uint32_t old_val;
	do {
		old_val = *p;
	} while (InterlockedCompareExchange ((volatile gint32 *)p, old_val | msk, old_val) != old_val);
	// return old_val;
}

void FastInterlockAnd(uint32_t volatile *p, uint32_t msk)
{
	uint32_t old_val;
	do {
		old_val = *p;
	} while (InterlockedCompareExchange ((volatile gint32 *)p, old_val & msk, old_val) != old_val);
	// return old_val;
}


bool PalHasCapability(PalCapability capability)
{
	return FALSE;
}

void
UnsafeInitializeCriticalSection(CRITICAL_SECTION * lpCriticalSection)
{
	pthread_mutex_init (&lpCriticalSection->mutex, NULL);
}

void
UnsafeEEEnterCriticalSection(CRITICAL_SECTION *lpCriticalSection)
{
	pthread_mutex_lock (&lpCriticalSection->mutex);
}

void
UnsafeEELeaveCriticalSection(CRITICAL_SECTION * lpCriticalSection)
{
	pthread_mutex_unlock (&lpCriticalSection->mutex);
}

void
UnsafeDeleteCriticalSection(CRITICAL_SECTION *lpCriticalSection)
{
	pthread_mutex_destroy (&lpCriticalSection->mutex);
}



