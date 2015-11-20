#ifndef __RUNTIME_INTERFACE_H__
#define __RUNTIME_INTERFACE_H__

#include <mono/utils/mono-threads.h>



/*
The follow macros help with functions that either are called by managed code or
make calls to non-runtime functions.
*/

/*
managed -> native helpers.

The following macros help working with icalls.

TODO:
	All those macros lack checked build asserts for entry/exit states and missuse.
*/


/*
The following macros must happen at the beginning of every single icall

their function is to transition the thread from managed into native code and ensure
the thread can be suspended if needed.

ICALL_ENTRY does push all registers and update the stack pointer. We need this because the managed
caller might have managed pointers in callee safe registers and we need to ensure the GC will see them.

ICALL_ENTRY_FAST doesn't update the stack mark, it must only be used with functions that ever safepoint.

ICALL_EXIT(_FAST) cleanup the work done by the matching ICALL_ENTRY function. It must be the last thing
to happen in the function and it must match the first one whether is __FAST or not.

TODO:
	1) optimize the case we control code gen and can ensure no callee saved registers will hold managed pointers,
	This needs to be coordinated with the effort to remove icall wrappers.

NOTES:
	Maybe we have functions that have a no-safepoints fast-path and we'll have to offer a split variant of ICALL_ENTRY
*/

#define ICALL_ENTRY() \
	__builtin_unwind_init ();	\
	MonoThreadInfo *__current_thread = mono_thread_info_current ();	\
	void *__previous_stack_mark = mono_thread_info_push_stack_mark (__current_thread, &__current_thread);	\

#define ICALL_ENTRY_FAST() \
	//Nothing to do here

#define ICALL_EXIT() \
	mono_thread_info_pop_stack_mark (__current_thread, __previous_stack_mark);

#define ICALL_EXIT_FAST() \
	//Nothing to do here


/*
Tunnables for local handles

TODO:
	Move this to a tunables file so it's not lost here
*/
#define MIN_LOCAL_HANDLES_FRAME_SIZE 8

/*
Local handles frame setup teardown.

Those should be used in two circustances:
	- In icalls that take managed pointer arguments
	- In runtime functions that create local handles


TODO:
	1) Experiment with alloc zig-zag'ing for common sizes.
	2) Experiment with always having a frame available (push on attach and call to managed) and having icalls just reset it.
*/


#define LOCAL_HANDLE_PUSH_FRAME()	\
	MonoLocalHandlesFrame __frame;	\
	mono_local_handles_frame_alloc (&__frame, MIN_LOCAL_HANDLES_FRAME_SIZE);

#define LOCAL_HANDLE_POP_FRAME()	\
	mono_local_handles_frame_pop (&__frame);

void* mono_thread_info_push_stack_mark (MonoThreadInfo *, void *);
void mono_thread_info_pop_stack_mark (MonoThreadInfo *, void *);

typedef struct {
	void *base;
	int index, size;
} MonoLocalHandlesFrame;

void mono_local_handles_frame_alloc (MonoLocalHandlesFrame *, int);
void mono_local_handles_frame_pop (MonoLocalHandlesFrame *);

/*
Local handle creation and manipulation code.

Handles are meant to be opaque and never directly accessed by user code.

This is to encourage never exposing managed pointers on the stack for
more than temporary expersions.

Handles require a frame to be available so you must always use it in
conjuction with the frame macros from above.

TODO:
	Add checked build asserts

NOTES:

*/
typedef struct {
	void *__CANT_TOUCH_THIS__;
} MonoLocalHandle;

#define LOCAL_HANDLE_NEW(MP) (mono_local_handles_alloc_handle(&__frame, (MP)))

#define HANDLE_GET(LH) ((LH).__CANT_TOUCH_THIS__)
#define HANDLE_SET_MP(TYPE, LH, FIELD, VALUE) do { \
	MONO_OBJECT_SETREF (((TYPE*)HANDLE_GET((LH))), FIELD, (VALUE));	\
} while (0)

#define HANDLE_SET_VAL(TYPE, LH, FIELD, VALUE) do { \
	((TYPE*)HANDLE_GET((LH)))->FIELD = (VALUE);	\
} while (0)

MonoLocalHandle mono_local_handles_alloc_handle (MonoLocalHandlesFrame *, void*);

#endif

