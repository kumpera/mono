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

#ifdef TARGET_WASM

static void
wasm_restore_context (void)
{
}

gpointer
mono_aot_get_trampoline_full (const char *name, MonoTrampInfo **out_tinfo)
{
	if (strcmp (name, "restore_context"))
		g_error ("CAN'T GET TRAMP FOR %s", name);

	gpointer code = &wasm_restore_context;

	if (out_tinfo) {
		MonoTrampInfo *tinfo = g_new0 (MonoTrampInfo, 1);
		tinfo->code = code;
		tinfo->code_size = 1;
		tinfo->name = name;
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
