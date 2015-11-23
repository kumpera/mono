#include <config.h>

#include <mono/metadata/runtime-interface.h>

void*
mono_thread_info_push_stack_mark (MonoThreadInfo *info, void *new_stack_bottom)
{
	return NULL;
}

void
mono_thread_info_pop_stack_mark (MonoThreadInfo *info, void *old_stack_bottom)
{
}

void
mono_local_handles_frame_alloc (MonoLocalHandlesFrame *frame, int requested_frames)
{
}

void
mono_local_handles_frame_pop (MonoLocalHandlesFrame *frame)
{
}

MonoLocalHandle
mono_local_handles_frame_pop_ret (MonoLocalHandlesFrame *frame, MonoLocalHandle handle)
{
	return handle;
}


MonoLocalHandle
mono_local_handles_alloc_handle (MonoLocalHandlesFrame *frame, void *mop)
{
	MonoLocalHandle res = { mop };
	return res;
}
