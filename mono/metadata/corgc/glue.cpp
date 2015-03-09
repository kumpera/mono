#include "common.h"

#include "gcenv.h"

#include "gc.h"
#include "objecthandle.h"

#include "gcdesc.h"

#include <glib.h>

GCSystemInfo g_SystemInfo;

extern "C" {
int mono_cpu_count ();
int mono_pagesize ();
MethodTable* corgc_get_array_fill_vtable ();
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