/*
 * ircsig.c: has a `my_signal()' that uses sigaction().
 *
 * Copyright (c) 1993-1996 Matthew R. Green.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Extensive modifications in 2026 by EPIC Software Labs
 */
#include "irc.h"
#include "irc_std.h"

/* DELIVERY OF SIGNALS [sigprocmask()] */
/*
 * block_signal	- Temporarily prohibit delivery of a signal
 *
 * Arguments:
 *	sig_no	- A signal to suspend further delivery on
 *
 * By default, all signals start off their life "unblocked"
 *
 * During the handling of certain signals (for us, SIGCHILD), it may be catastrophic
 * for a new signal to be generated while handling previous signals.
 * For that reason, you may need to block/suspend a signal temporarily.
 */
int	block_signal (int sig_no)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, sig_no);
	return sigprocmask(SIG_BLOCK, &set, NULL);
}

/*
 * unblock_signal - Resume delivery of a signal
 *
 * Arguments:
 *	sig_no	- A signal you previously passed to block_signal
 *
 * Of course after you're done with your critical section you need to resume
 * delivery of the signal!
 */
int	unblock_signal (int sig_no)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, sig_no);
	return sigprocmask(SIG_UNBLOCK, &set, NULL);
}

/* CALLBACKS OF SIGNALS [sigaction()] */

/* array of application signal handlers */
	 sigfunc *	signal_handlers [MY_SIG_MAX];
volatile sig_atomic_t	signals_caught  [MY_SIG_MAX];


/*
 * This is the signal handler we use for all signals.
 * It manages the global signal state table (above)
 * and calls your application callback synchronously
 */
static void	unified_signal_handler (int sig_no)
{
	signals_caught[0] = 1;
	signals_caught[sig_no]++;

	if (signal_handlers[sig_no])
		signal_handlers[sig_no](sig_no);
}

/*
 * my_signal - Register a callback for a signal handler
 *
 * Arguments:
 *	sig_no	- A signal for this callback
 *	handler	- The callback when the signal is fired
 *
 * Notes:
 *	EPIC always uses "unified_signal_handler()" for all signals.
 *	It updates the global variables and then calls "handler".
 *
 * 	Although there are errors and failures, nobody would do anything 
 *	about them anyhow.  But maybe someday that can change
 */
void	my_signal (int sig_no, sigfunc *handler)
{
	struct sigaction	sa, 
				osa;

	/* Ensure the signal is valid */
	if (sig_no < 0 || sig_no >= MY_SIG_MAX)
		return;
	if (sig_no == SIGKILL || sig_no == SIGSTOP)
		return;

	/* 
	 * If you want a callback, then we save your callback.
	 * If you want default/suppressed handling, then we pass that through.
	 * Default/suppressed handling a signal inhibits the user getting /on signal.
	 */
	if (handler == SIG_IGN || handler == SIG_DFL)
		signal_handlers[sig_no] = NULL;
	else
	{
		signal_handlers[sig_no] = handler;
		handler = unified_signal_handler;
	}

	/* Fill out a sigaction item */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, sig_no);

	/* Make sure only SIGALRM and SIGINT cause EINTR */
	sa.sa_flags = 0;
	if (sig_no != SIGALRM && sig_no != SIGINT)
		sa.sa_flags |= SA_RESTART;

	/* Although sigaction can fail, we wouldn't do anything about it anyways */
	sigaction(sig_no, &sa, &osa);
}


/* 
 * hook_all_signals needs to be called in main() before my_signal()
 * if any signal hooks fail, it returns SIG_ERR, otherwise it returns
 * NULL. - pegasus
 */
void	init_signals (void)
{
	int		sig_no;

	memset((void *)&signals_caught, 0, sizeof(signals_caught));
	memset((void *)&signal_handlers, 0, sizeof(signal_handlers));

	for (sig_no = 1; sig_no < MY_SIG_MAX; sig_no++)
		my_signal(sig_no, NULL);
}

