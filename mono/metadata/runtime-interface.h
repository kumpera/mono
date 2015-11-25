#ifndef __RUNTIME_INTERFACE_H__
#define __RUNTIME_INTERFACE_H__

#include <mono/utils/mono-threads.h>



/*
The follow macros help with functions that either are called by managed code or
make calls to non-runtime functions.

Skeleton for an icall that returns no value:

void
do_something (MonoObject *this_raw, int flags)
{
	//Order matters
	//XXX it would be nice to colapse those two calls
	ICALL_ENTRY ();
	LOCAL_HANDLE_PUSH_FRAME ();

	//The convetion is that HANDLE_DCL wraps a variable with equal name with a _raw suffix. 
	HANDLE_DCL (this_obj);

	...
	//All icalls MUST have a single exit
	//XXX colapse those two calls into a single one
	//XXX force all epilogues to have a done label if we can avoid the warning
done:
	LOCAL_HANDLE_POP_FRAME ();
	ICALL_EXIT ();
}

Skeleton for an icall that returns a reference value

MonoObject*
do_something (void)
{
	ICALL_ENTRY ();
	LOCAL_HANDLE_PUSH_FRAME ();
	MonoLocalHandle ret;

	ret = mono_array_new_handle (...);

	//XXX this is pretty ugly, it would be nice to wrap this into something nicer
done:
	MonoObject *ret_raw = LOCAL_HANDLE_POP_FRAME_RET_MP ();
	ICALL_EXIT ();
	return ret_raw;
}


TODO

- Skeleton for non-icall functions
- Guidelines on how to handle'ize the existing API
- Skeleton + Guideless for fast (frame-less) icalls.

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
local handle structs
*/
typedef struct {
	void *base;
	int index, size;
} MonoLocalHandlesFrame;

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

#define LOCAL_HANDLE_POP_FRAME_RET(LH) (LH).__CANT_TOUCH_THIS__ = mono_local_handles_frame_pop_ret (&__frame, (MonoHandleInternalValue)(LH).__CANT_TOUCH_THIS__), (LH)

void* mono_thread_info_push_stack_mark (MonoThreadInfo *, void *);
void mono_thread_info_pop_stack_mark (MonoThreadInfo *, void *);

void mono_local_handles_frame_alloc (MonoLocalHandlesFrame *, int);
void mono_local_handles_frame_pop (MonoLocalHandlesFrame *);

typedef void* MonoHandleInternalValue;

void* mono_local_handles_frame_pop_ret (MonoLocalHandlesFrame *, MonoHandleInternalValue);
MonoHandleInternalValue mono_local_handles_alloc_handle (MonoLocalHandlesFrame *, void*);

void mono_local_handles_set_value (MonoHandleInternalValue *handle_addr, void *);

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


#define HANDLE_GET(LH) ((LH).__CANT_TOUCH_THIS__)
#define HANDLE_SETVAL_MP(LH, VALUE) do { mono_local_handles_set_value ((MonoHandleInternalValue*)&LH.__CANT_TOUCH_THIS__, VALUE); } while (0)

#define HANDLE_SET_MP(LH, FIELD, VALUE) do { \
	MONO_OBJECT_SETREF ((HANDLE_GET((LH))), FIELD, (VALUE));	\
} while (0)
#define HANDLE_SET_LH(LH, FIELD, VALUE) do { \
	MONO_OBJECT_SETREF ((HANDLE_GET((LH))), FIELD, HANDLE_GET(VALUE));	\
} while (0)

#define HANDLE_SET_VAL(LH, FIELD, VALUE) do { \
	(HANDLE_GET((LH)))->FIELD = (VALUE);	\
} while (0)


/*
An experiment in typed handles.
*/

#define DEF_HANDLE_TYPE(TYPE) \
typedef struct {	\
	TYPE *__CANT_TOUCH_THIS__;	\
} TYPE ## Handle;


#define HANDLE_DCL_TYPED(TYPE, LH) TYPE ## Handle LH = { mono_local_handles_alloc_handle(&__frame, LH ##_raw) }
#define HANDLE_NEW_TYPED(TYPE, NAME) TYPE ## Handle NAME = { mono_local_handles_alloc_handle(&__frame, NULL) }

DEF_HANDLE_TYPE (MonoObject);
DEF_HANDLE_TYPE (MonoString);
DEF_HANDLE_TYPE (MonoArray);

#endif

