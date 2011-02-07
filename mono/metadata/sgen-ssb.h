/*
 * Copyright 2001-2003 Ximian, Inc
 * Copyright 2003-2011 Novell, Inc.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __MONO_SGENGC_SSB_H__
#define __MONO_SGENGC_SSB_H__

#include <metadata/sgen-gc.h>

void sgen_ssb_evacuate_remset_buffer (void) MONO_INTERNAL;
void sgen_ssb_scan (void *start_nursery, void *end_nursery, SgenGrayQueue *queue) MONO_INTERNAL;
void sgen_ssb_clear (void) MONO_INTERNAL;
void sgen_ssb_init (void) MONO_INTERNAL;
void sgen_ssb_record_pointer (gpointer ptr) MONO_INTERNAL;
gboolean sgen_ssb_find (char *addr) MONO_INTERNAL;


void sgen_ssb_register_thread (SgenThreadInfo *info) MONO_INTERNAL;
void sgen_ssb_unregister_thread (SgenThreadInfo *info) MONO_INTERNAL;

MonoMethod* sgen_ssb_get_write_barrier (void) MONO_INTERNAL;

#endif /* __MONO_SGENGC_SSB_H__ */