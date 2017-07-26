/**
 * \file
 * mono Ahead of Time compiler
 *
 * Author:
 *   Dietmar Maurer (dietmar@ximian.com)
 *   Zoltan Varga (vargaz@gmail.com)
 *
 * (C) 2002 Ximian, Inc.
 * Copyright 2003-2011 Novell, Inc.
 * Copyright 2011 Xamarin, Inc.
 * Licensed under the MIT license. See LICENSE file in the project root for full license information.
 */

#include "config.h"

#include <sys/types.h>

#include "mini.h"
#include "interp/interp.h"

#ifdef TARGET_WASM

static void
wasm_restore_context (void)
{
	g_error ("wasm_restore_context");
}

static void
wasm_call_filter (void)
{
	g_error ("wasm_call_filter");
}

static void
wasm_throw_exception (void)
{
	g_error ("wasm_throw_exception");
}

static void
wasm_rethrow_exception (void)
{
	g_error ("wasm_rethrow_exception");
}

static void
wasm_throw_corlib_exception (void)
{
	g_error ("wasm_throw_corlib_exception");
}

static char
type_to_c (MonoType *t)
{
handle_enum:
	switch (t->type) {
	case MONO_TYPE_BOOLEAN:
	case MONO_TYPE_CHAR:
	case MONO_TYPE_I1:
	case MONO_TYPE_U1:
	case MONO_TYPE_I2:
	case MONO_TYPE_U2:
	case MONO_TYPE_I4:
	case MONO_TYPE_U4:
	case MONO_TYPE_I:
	case MONO_TYPE_U:
	case MONO_TYPE_PTR:
	case MONO_TYPE_SZARRAY:
	case MONO_TYPE_CLASS:
	case MONO_TYPE_OBJECT:
	case MONO_TYPE_STRING:
		return 'I';
	case MONO_TYPE_R4:
		return 'F';
	case MONO_TYPE_R8:
		return 'D';
		break;
	case MONO_TYPE_I8:
	case MONO_TYPE_U8:
		return 'L';
	case MONO_TYPE_VOID:
		return 'V';
	case MONO_TYPE_VALUETYPE:
		if (t->data.klass->enumtype) {
			t = mono_class_enum_basetype (t->data.klass);
			goto handle_enum;
		}

		printf ("struct is %s\n", mono_type_full_name (t));
		return 'S';
	case MONO_TYPE_GENERICINST:
		return 'G';
	default:
		g_warning ("CANT TRANSLATE %s", mono_type_full_name (t));
		return 'X';
	}
}

static void
invoke_viii (void *target_func, InterpMethodArguments *margs)
{
	void (*func)(gpointer a, gpointer b, gpointer c) = target_func;
	func (margs->iargs [0], margs->iargs [1], margs->iargs [2]);
}

static void
invoke_i (void *target_func, InterpMethodArguments *margs)
{
	int (*func)(void) = target_func;
	int res = func ();
	*(int*)margs->retval = res;
}

static void
invoke_iii (void *target_func, InterpMethodArguments *margs)
{
	int (*func)(gpointer a, gpointer b) = target_func;
	int res = func (margs->iargs [0], margs->iargs [1]);
	*(int*)margs->retval = res;
}
static void
wasm_enter_icall_trampoline (void *target_func, InterpMethodArguments *margs)
{
	static char cookie [8];
	static int c_count;

	MonoMethodSignature *sig = margs->sig;
	// printf ("wasm_enter_icall_trampoline %p\n", target_func);
	// printf ("\tilen %d flen %d\n", margs->ilen, margs->flen);
	// printf ("\tsig %s\n", mono_signature_get_desc (sig, FALSE));

	c_count = sig->param_count + sig->hasthis + 1;
	cookie [0] = type_to_c (sig->ret);
	if (sig->hasthis)
		cookie [1] = 'I';
	for (int i = 0; i < sig->param_count; ++i)
		cookie [1 + sig->hasthis + i ] = type_to_c (sig->params [i]);
	cookie [c_count] = 0;

	printf ("cookie %s (%d)\n", cookie, c_count);

	if (!strcmp ("VIII", cookie))
		invoke_viii (target_func, margs);
	else if (!strcmp ("I", cookie))
		invoke_i (target_func, margs);
	else if (!strcmp ("III", cookie))
		invoke_iii (target_func, margs);
	else {
		printf ("CANNOT HANDLE COOKIE %s\n", cookie);
		g_assert (0);
	}
		
}

gpointer
mono_aot_get_trampoline_full (const char *name, MonoTrampInfo **out_tinfo)
{
	gpointer code = NULL;

	if (!strcmp (name, "restore_context"))
		code = wasm_restore_context;
	else if (!strcmp (name, "call_filter"))
		code = wasm_call_filter;
	else if (!strcmp (name, "throw_exception"))
		code = wasm_throw_exception;
	else if (!strcmp (name, "rethrow_exception"))
		code = wasm_rethrow_exception;
	else if (!strcmp (name, "throw_corlib_exception"))
		code = wasm_throw_corlib_exception;
	else if (!strcmp (name, "enter_icall_trampoline"))
		code = wasm_enter_icall_trampoline;

	if (!code) {
		printf ("CAN'T GET TRAMP FOR %s\n", name);
		g_assert (0);
	}

	if (out_tinfo) {
		MonoTrampInfo *tinfo = g_new0 (MonoTrampInfo, 1);
		tinfo->code = code;
		tinfo->code_size = 1;
		tinfo->name = g_strdup (name);
		tinfo->ji = NULL;
		tinfo->unwind_ops = NULL;
		tinfo->uw_info = NULL;
		tinfo->uw_info_len = 0;
		tinfo->owns_uw_info = FALSE;

		*out_tinfo = tinfo;
	}

	return code;
}
#endif
