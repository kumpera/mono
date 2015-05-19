#ifndef __INTERRUPTION_TOKEN__
#define __INTERRUPTION_TOKEN__

#include <glib.h>
#include <mono/metadata/mono-threads.h>

typedef gboolean (*mono_cancellation_cb) (void *user_data);

typedef struct {
	void *data;
	mono_cancellation_cb callback;
} MonoInterruptionToken;


gboolean mono_interruption_token_install (MonoInterruptionToken *token);
gboolean mono_interruption_token_uninstall (void);
MonoInterruptionToken * mono_thread_info_begin_interrupt (MonoThreadInfo *info);
void mono_thread_info_finish_interrupt (MonoInterruptionToken *token);
gboolean mono_thread_info_cancel_interrupt (MonoThreadInfo *info);
gboolean mono_thread_info_is_interrupted (void);

#endif
