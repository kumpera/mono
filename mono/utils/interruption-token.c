#include <mono/utils/interruption-token.h>

#define INTERRUPT_TOKEN ((void*)(size_t)-1)

/*
Install an interruption token for the current thread.

Returns TRUE if installed, FALSE if thread is in interrupt state.

The callback in token must be able to be called from another thread and always cancel the thread.
It will be called while holding the interrution token lock.
*/
gboolean
mono_interruption_token_install (MonoInterruptionToken *token)
{
	void *prev_token;
	MonoThreadInfo *info = mono_thread_info_current ();

	g_assert (token->callback);

retry:
	prev_token = info->interruption_token;
	//Can't install if allready interrupted
	if (prev_token == INTERRUPT_TOKEN)
		return FALSE;

	//interruption tokens don't nest
	g_assert (prev_token == NULL);

	prev_token = InterlockedCompareExchangePointer (&info->interruption_token, token, NULL);
	if (prev_token != NULL)
		goto retry;
	return TRUE;
}

gboolean
mono_interruption_token_uninstall (void)
{
	void *prev_token;
	MonoThreadInfo *info = mono_thread_info_current ();

retry:
	prev_token = info->interruption_token;
	//only the installer can uninstall the token
	g_assert (prev_token);

	prev_token = InterlockedCompareExchangePointer (&info->interruption_token, NULL, prev_token);

	//If we got interrupted, sync with the interrupt initiator as the token holds stack memory.
	if (prev_token == INTERRUPT_TOKEN) {
		interruption_token_lock ();
		interruption_token_unlock ();
	}
}

MonoInterruptionToken*
mono_thread_info_begin_interrupt (MonoThreadInfo *info)
{
	MonoInterruptionToken *token, prev_token;

retry:
	token = info->interruption_token;

	/* Already interrupted */
	if (token == INTERRUPT_TOKEN)
		return NULL;
	/* 
	 * Atomically obtain the token the thread is waiting on, and
	 * change it to a flag value.
	 */
	prev_token = (MonoInterruptToken*)InterlockedCompareExchangePointer (&info->interruption_token, INTERRUPT_TOKEN, token);
	if (prev_token != token)
		goto retry;

	return token;
}

void
mono_thread_info_finish_interrupt (MonoInterruptionToken *token)
{
	if (token == NULL)
		return;

	interruption_token_lock ();
	token->callback (token->data);
	interruption_token_unlock ();
}

