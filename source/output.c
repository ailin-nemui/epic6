/*
 * output.c: handles a variety of tasks dealing with the output from the irc
 * program 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1997, 2002 EPIC Software Labs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, the above paragraph (the one permitting redistribution),
 *    this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 */

#include "irc.h"
#include "output.h"
#include "vars.h"
#include "input.h"
#include "termx.h"
#include "lastlog.h"
#include "window.h"
#include "screen.h"
#include "hook.h"
#include "ctcp.h"
#include "log.h"
#include "ircaux.h"
#include "alias.h"
#include "commands.h"
#include "server.h"
#include "levels.h"

/* make this buffer *much* bigger than needed */
#define OBNOXIOUS_BUFFER_SIZE BIG_BUFFER_SIZE * 10
static	char	putbuf[OBNOXIOUS_BUFFER_SIZE + 1];

/* 
 * unflash: sends a ^[c to the screen
 * Must be defined to be useful, cause some vt100s really *do* reset when
 * sent this command. >;-)
 * Now that you can send ansi sequences, this is much less inportant.. 
 */
static void	unflash (void)
{
#if 0
	fwrite("\033c", 2, 1, stdout);		/* hard reset */
#endif
	fwrite("\033)0", 3, 1, stdout);		/* soft reset */
}

/* sig_refresh_screen: the signal-callable version of refresh_screen */
SIGNAL_HANDLER(sig_refresh_screen)
{
	need_redraw = 1;
}

/*
 * refresh_screen: Whenever the REFRESH_SCREEN function is activated, this
 * swoops into effect 
 */
BUILT_IN_KEYBINDING(refresh_screen)
{
	need_redraw = 1;
}

void	redraw_all_screens (void)
{
	int	old_os;
	int	s_;

	old_os = output_screen;
	for (s_ = 0; traverse_all_screens(&s_); )
	{
		if (!get_screen_alive(s_))
			continue;

		output_screen = s_;
		unflash();
		term_clear_screen();
		if (s_ == main_screen && term_resize())
			recalculate_windows(s_);
	}

	/* Logically mark all windows as needing a redraw */
	redraw_all_windows();

	/* Physically redraw all windows and status bars */
	update_all_windows();

	/* Physically redraw all input lines */
	update_input(-1, UPDATE_ALL);

	output_screen = old_os;
	need_redraw = 0;
}

/* extern_write -- controls whether others may write to our terminal or not. */
/* This is basically stolen from bsd -- so its under the bsd copyright */
BUILT_IN_COMMAND(extern_write)
{
	char *tty;
	Stat sbuf;
	const int OTHER_WRITE = 020;

	if (!(tty = ttyname(2)))
	{
		yell("Could not figure out the name of your tty device!");
		return;
	}
	if (stat(tty, &sbuf) < 0)
	{
		yell("Could not get the information about your tty device!");
		return;
	}
	if (!args || !*args)
	{
		if (sbuf.st_mode & 020)
			say("Mesg is 'y'");
		else
			say("Mesg is 'n'");
		return;
	}
	switch (args[0])
	{
		case 'y' :
		{
			if (chmod(tty, sbuf.st_mode | OTHER_WRITE) < 0)
			{
				yell("Could not set your tty's mode!");
				return;
			}
			break;
		}
		case 'n' :
		{
			if (chmod(tty, sbuf.st_mode &~ OTHER_WRITE) < 0)
			{
				yell("Could not set your tty's mode!");
				return;
			}
			break;
		}
		default :
			say("Usage: /%s [Y|N]", command);
	}
}

/*
 * init_screen() sets up a full screen display for normal display mode.
 * It will fail if your TERM setting doesn't have all of the necessary
 * capabilities to run in full screen mode.  The client is expected to 
 * fail-over to dumb mode in this case.
 * This may only be called once, at initial startup (by main()).
 */
int	init_screen (void)
{
	/* Investigate TERM and put the console in full-screen mode */
	if (term_init())
		return -1;

	term_clear_screen();
	term_resize();

	/*
	 * System independant stuff
	 */
	create_new_screen(1);
	new_window(main_screen);
	update_all_windows();

	term_move_cursor(0, 0);
	return 0;
}

/*
 * put_echo - The chokepoint for all output everywhere
 *
 * Arguments:
 *	str	- A string to display on the screen, of any length
 *
 * History:
 *	Historically, the main entry to add_to_screen() was put_it(), which
 *	had size limits, and required everything to be expressed as a printf
 *	format.  Unfortunately, put_it() still has line limits,
 *	(although they're higher than they used to be), but it is no longer 
 *	the entry point to add_to_screen().  
 *	This function lets you output an unlimited length string.
 *
 * Notes:
 *	THIS IS THE ONLY FUNCTION PERMITTED TO CALL ADD_TO_SCREEN().
 *	ALL OUTPUT OF EVERY KIND EVERYWHERE WHATSOEVER MUST RESOLVE
 *	TO A CALL TO THIS FUNCTION.
 */
void	put_echo (const char *str)
{
	add_to_log(0, irclog_fp, -1, str, 0, NULL);
	add_to_screen(str);
}

/*
 * put_it - The primary irc display routine for anything requiring printf
 *
 * Arguments:
 *	format	- A printf() type format
 *	...	- Things that fulfill the 'format'.
 *
 * Notes:
 *	Because put_it() honors window_display, the output will be
 *	shown only if the user is not suppressing it.
 *
 * Bogons:
 *	Regretably, this function still has a 10k size limit.
 *	I should do something about that.  The reason it still
 *	exists was because of ancient OSs that had problems with 
 *	floats in va_lists.
 */
void	put_it (const char *format, ...)
{
	if (get_window_display() && format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, sizeof putbuf, format, args);
		va_end(args);
		put_echo(putbuf);
	}
}

/*
 * file_put_it - Output something to a file, and maybe the screen
 *
 * Arguments:
 *	fp	- A file to output to
 *	format	- A printf() type format
 *	...	- Things that fulfill the 'format'.
 *
 * Notes:
 *	This is only used by /lastlog -file.
 */
void	file_put_it (FILE *fp, const char *format, ...)
{
	if (format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, sizeof putbuf, format, args);
		va_end(args);
		if (fp)
		{
			fputs(putbuf, fp);
			fputs("\n", fp);
		}
		else if (get_window_display())
			put_echo(putbuf);
	}
}

/*
 * vsay - A put_it() wrapper that adds the /set banner
 *
 * Arguments:
 *	format	- A printf() type format
 *	args	- Things that fulfill the 'format'.
 *
 * Notes:
 *	Because put_it() honors window_display, the output will be
 *	shown only if the user is not suppressing it.
 *
 * Bogons:
 *	Regretably, this function also has size limits.
 */
static void 	vsay (const char *format, va_list args)
{
	if (get_window_display() && format)
	{
		const char *str;

		*putbuf = 0;
		if ((str = get_string_var(BANNER_VAR)))
		{
			if (get_int_var(BANNER_EXPAND_VAR))
			{
			    char *foo;

			    foo = expand_alias(str, empty_string);
			    strlcpy(putbuf, foo, sizeof putbuf);
			    new_free(&foo);
			}
			else
			    strlcpy(putbuf, str, sizeof putbuf);

			strlcat(putbuf, " ", sizeof putbuf);
		}

		vsnprintf(putbuf + strlen(putbuf), 
			sizeof(putbuf) - strlen(putbuf) - 1, 
			format, args);

		put_echo(putbuf);
	}
}

/*
 * say - The trivial wrapper around vsay() [and put_it()]
 *
 * Arguments:
 *	format	- A printf() type format
 *	...	- Things that fulfill the 'format'.
 *
 * Notes:
 *	Because put_it() honors window_display, the output will be
 *	shown only if the user is not suppressing it.
 *
 * Bogons:
 *	Regretably, this function also has size limits.
 */
void	say (const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsay(format, args);
	va_end(args);
}

/*
 * yell - A way to output important info that the user might veto
 *
 * Arguments:
 *	format	- A printf() type format
 *	...	- Things that fulfill the 'format'.
 *
 * Notes:
 *	- The message is offered to the user via /on yell.
 *	  This is helpful for suppressing unwanted chatter.
 *	- Because this uses put_echo() and not put_it(), it will always
 *	  output the message unless the user vetos it.
 *
 * Bogons:
 *	Regretably, this function also has size limits.
 */
void	yell (const char *format, ...)
{
	if (format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, sizeof putbuf, format, args);
		va_end(args);
		if (do_hook(YELL_LIST, "%s", putbuf))
			put_echo(putbuf);
	}
}

/*
 * privileged_yell - A way to output important info that is never suppressed
 *
 * Arguments:
 *	format	- A printf() type format
 *	...	- Things that fulfill the 'format'.
 *
 * Notes:
 *	- Because this uses put_echo() and not put_it(), it will always
 *	  output the message, and the user cannot suppress it.
 *
 * Bogons:
 *	Regretably, this function also has size limits.
 */
void	privileged_yell (const char *format, ...)
{
	if (format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, sizeof putbuf, format, args);
		va_end(args);

		privileged_output++;
		put_echo(putbuf);
		privileged_output--;
	}
}

/*
 * my_error - A yell() replacement to be used during /LOADs
 *
 * Arguments:
 *	format	- A printf() type format
 *	...	- Things that fulfill the 'format'.
 *
 * Notes:
 *	- This function will output status info about the /LOAD.
 *	- The message is offered to the user via /on yell.
 *	  This is helpful for suppressing unwanted chatter.
 *	- Because this uses put_echo() and not put_it(), it will always
 *	  output the message unless the user vetos it.
 *
 * Bogons:
 *	Regretably, this function also has size limits.
 */
void 	my_error (const char *format, ...)
{
	dump_load_stack(0);
	if (format)
	{
		va_list args;
		va_start (args, format);
		vsnprintf(putbuf, sizeof putbuf, format, args);
		va_end(args);
		do_hook(YELL_LIST, "%s", putbuf);
		put_echo(putbuf);
	}
}

/******************************************************************/
/*
 * vsyserr - A mix between say() and yell() for diagnostic errors
 *
 * Arguments:
 *	server	- The server that generated this diagnostic
 *	format	- A printf() type format
 *	...	- Things that fulfill the 'format'.
 *
 * Notes:
 *	- Like say(), this will not output if the user is suppressing output
 *	- Like say(), this will prefix /SET BANNER
 *	- It also adds the "INFO --" badge.
 *	- The output is always output at level SYSERR
 *	- Like yell(), the user can veto the output with /on yell
 *
 * Bogons:
 *	Regretably, this function also has size limits.
 */
static void     vsyserr (int server, const char *format, va_list args)
{
	const char *  	str;
	int     	l, 
			old_from_server,
			i_set_from_server;

        if (!get_window_display() || !format)
		return;

	old_from_server = from_server;
	i_set_from_server = 0;

	*putbuf = 0;
	if ((str = get_string_var(BANNER_VAR)))
	{
		if (get_int_var(BANNER_EXPAND_VAR))
		{
		    char *foo;

		    foo = expand_alias(str, empty_string);
		    strlcpy(putbuf, foo, sizeof putbuf);
		    new_free(&foo);
		}
		else
		    strlcpy(putbuf, str, sizeof putbuf);

		strlcat(putbuf, " INFO -- ", sizeof putbuf);
	}

	vsnprintf(putbuf + strlen(putbuf),
		sizeof(putbuf) - strlen(putbuf) - 1,
		format, args);

	if (is_server_valid(server))
	{
		old_from_server = from_server;
		from_server = server;
		i_set_from_server = 1;
	}

	l = set_context(from_server, -1, get_who_sender(), get_who_from(), LEVEL_SYSERR);
	if (do_hook(YELL_LIST, "%s", putbuf))
		put_echo(putbuf);
	pop_context(l);

	if (i_set_from_server)
		from_server = old_from_server;
}

void    syserr (int server, const char *format, ...)
{
        va_list args;
        va_start(args, format);
        vsyserr(server, format, args);
        va_end(args);
}

/*****/
/*
 * This was migrated over from window.h
 */
/*
 * This is set to 1 if output is to be dispatched normally.  This is set to
 * 0 if all output is to be suppressed (such as when the system wants to add
 * and alias and doesnt want to blab to the user, or when you use ^ to
 * suppress the output of a command.)
 */
static  unsigned window_display = 1;

int     get_window_display (void)
{
        return window_display;
}

int     set_window_display (int value)
{
        window_display = value;
        return 0;
}

int     swap_window_display (int value)
{
        int     old_value = window_display;
        window_display = value;
        return old_value;
}

