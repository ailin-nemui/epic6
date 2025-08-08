/*
 * ircII: a new irc client.  I like it.  I hope you will too!
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1994 Jake Khuon.
 * Copyright 1993, 2016 EPIC Software Labs.
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
#define __need_term_h__
#include "irc.h"

/*
 * ɔɩɗɚ -- For an upside down world...
 */

/*
 * irc_version is what $J returns, its the common-name for the version.
 */
const char irc_version[] = "EPIC6-0.0.1";
const char useful_info[] = "epic6 0 0 1";

/*
 * internal_version is what $V returns, its the integer-id for the
 * version, and corresponds to the date of release, YYYYMMDD.
 */ 
const char internal_version[] = "20240826";

/*
 * In theory, this number is incremented for every commit.
 */
const unsigned long	commit_id = 3067;

/*
 * As a way to poke fun at the current rage of naming releases after
 * english words, which often have little or no correlation with outside
 * reality, I have decided to start doing that with EPIC.  These names
 * are intentionally and maliciously silly.  Complaints will be ignored.
 */
const char ridiculous_version_name[] = "Otiose";

#define __need_putchar_x__
#include "status.h"
#include "clock.h"
#include "names.h"
#include "vars.h"
#include "input.h"
#include "alias.h"
#include "output.h"
#include "termx.h"
#include "exec.h"
#include "screen.h"
#include "lastlog.h"
#include "log.h"
#include "server.h"
#include "hook.h"
#include "keys.h"
#include "ircaux.h"
#include "commands.h"
#include "window.h"
#include "exec.h"
#include "timer.h"
#include "newio.h"
#include "parse.h"
#include "levels.h"
#include "extlang.h"
#include "files.h"
#include "ctcp.h"

/*
 * Global variables
 */

	/* The ``DEFAULT'' port used for irc server connections. */
	/*
	 * Set by:	parse_args:  IRCPORT env, or -p command line argument
	 * Used by:	serverinfo_to_newserv() - default port for new servers
	 */
	int		irc_port = 6667;

	/* 
	 * When a numeric is being processed, this holds the negative value
	 * of the numeric.  Its negative so if we pass it to do_hook, it can
	 * tell its a numeric and not a named ON event.
	 */
	/*
	 * Set by:	numbered_command  (numeric handling)
	 * Used by:	$H -- alias_current_numeric() 
	 *		banner()  (with /set show_numerics for banner)
	 * Thoughts:	Should be per-server, not global.
	 */
	int		current_numeric = -1;

	/*
	 * THESE NEXT FOUR VARIABLES SOULD BE COLLAPSED INTO ONE
	 */
	/* Set if the client is not using a termios interface */
	/*
	 * Set by:	parse_args --  -d and -b command line arguments
	 *		main() -- if we're not connected to a terminal
	 * Used by:	/botmode - you must be in dumb mode to use /botmode.
	 *		update_input() - input line is suppressed in dumb mode
	 *		irc_exit() - screen is reset if in dumb mode
	 *		on 464 - do not ask for password in dumb mode
	 *		add_to_screen() - simplify output in dumb mode
	 *		scroll_window() - window scrolling gated by dumb mode
	 *		repaint_window_body() - gated by dumb mode
	 *		do_screens() - simplify user input in dumb mode
	 *		user_input_codepoint() - gated by dumb mode
	 *		redraw_status() - gated by dumb mode
	 *		term_init() - panics if called in dumb mode
	 * 		set_automargin_override() - gated by dumb mode
	 * Thoughts:	Should be renamed. 
	 */
	int		dumb_mode = 0;

	/* Set if the client is supposed to fork(). (a bot.)  Probably bogus. */
	/*
	 * Set by:	/botmode
	 *		parse_args -- -b command line argument
	 * Used by:	main() -- switching to bot mode, redirecting stdout to /dev/null
	 *		p_kill -- If killed and bot didn't handle, then exit program
	 *			^^^ This is bogus.
	 */
	int		background = 0;

	/* This is 1 if we are in the foreground process group */
	/*
	 * Set by:	term_cont -- if we are the fg or bg process
	 * Used by:	cursor_to_input() -- gated on foreground
	 *		update_input -- gated on foreground
	 *		rite() -- controls whether output_with_count() outputs or just counts.
	 *			^^^ This is bogus
	 *		scroll_window -- physical output gated on foreground
	 *		repaint_window_body() -- passed to output_with_count().
	 *		redraw_status() - gated on foreground
	 * Notes: Nothing sets this to low!  That's heinous and wrong.
	 */
	int		foreground = 1;

	/* Set if the client is checking fd 0 for input. (usu. op of "background") */
	/*
	 * Set by:	/botmode
	 *		parse_args -- -b command line argument
	 *		term_cont -- if we are the fg or bg process
	 * Used by:	parse_args -- -b and -q cannot be used together
	 *			      -b and -s cannot be used together
	 *		main -- causes a fork()  [why not background?]
	 *		create_new_screen -- to decide whether to open stdin for reading
	 *				^^^ this is absolutely wrong
	 *		kill_screen -- to decide whether to close fdin for reading
	 *		do_screens -- gated on use_input
	 * Notes: background and use_input should be merged
	 */
	int		use_input = 1;
	/*
	 * END GROUP OF VARIABLES TO COLLAPSE
	 */

	/*
	 * Set when an OPER command is sent out, reset when umode +o or 464 reply
	 * comes back.  This is *seriously* bogus.
	 */
	/*
	 * Set by:	/oper
	 *		/on 464 - to reset to 0
	 * Used by:	/on 464 - to differentiate OPER password from PASS password.
	 * Notes: BURN IT WITH FIRE
	 */
	int		oper_command = 0;

	/* Set if your IRCRC file is NOT to be loaded on startup. */
	/*
	 * Set by:	parse_args -- -q command line argument
	 * Used by:	parse_args -- cannot use -q and -b; or -q and -s at the same time.
	 *		load_ircrc -- loading your ~/.epicrc is gated on it
	 * Notes: This is heinous
	 */
	int		quick_startup = 0;

	/* Set if user does not want to auto-connect to a server upon startup */
	/*
	 * Set by:	parse_args() -- -s command line argument
	 * Used by:	parse_args() -- cannot use -b -and -s at the same time
	 *		main() -- To decide whether to connect or show server list at startup.
	 */
	int		dont_connect = 0;

	/* Set to the time the client booted up */
	/*
	 * Set by:	main() -- as part of the boot process
	 * Used by:	alias_online() -- $F
	 */
	Timespec	start_time;

	/* Set to the current time, each time you press a key. */
	/*
	 * Set by:	main() as part of the boot process
	 *		do_screens() each time you press a key
	 * Used by:	reset_standard_clock -- for /ON IDLE
	 *		alias_idle -- for $E
	 */
	Timespec	idle_time = { 0, 0 };

/* Set to 0 when you want to suppress all beeps (such as window repaints) */
int		global_beep_ok = 1;

/* The unknown userhost value.  Bogus, but DONT CHANGE THIS!  */
const char	*unknown_userhost = "<UNKNOWN>@<UNKNOWN>";

/* Whether or not the client is dying. */
int		dead = 0;

/* The number of pending SIGINTs (^C) still unprocessed. */
volatile sig_atomic_t	cntl_c_hit = 0;

/* This is 1 if you want all logging to be inhibited. Dont leave this on! */
int		inhibit_logging = 0;


/* Output which is displayed without modification by the user */
int		privileged_output = 0;

/* Output which should not trigger %F in hidden windows */
int		do_window_notifies = 1;

jmp_buf		panic_jumpseat;
int		system_reset = 0;

intmax_t	sequence_point = 0;

char *		tmp_hostname = NULL;

/*
 * If set, outbound connections will be bind()ed to the address
 * specified.  if unset, the default address for your host will
 * be used.  VHosts can be set by the /HOSTNAME command 
 * or via the IRCHOST environment variable.  These variables should
 * be considered read-only.  Dont ever change them.
 *
 * Its important (from a user's point of view) that these never be
 * set to addresses that do not belong to the current hostname.
 * If that happens, outbound connections will fail, and its not my fault.
 */
char *		LocalIPv4HostName = NULL;
char *		LocalIPv6HostName = NULL;

int		inbound_line_mangler = 0,
		outbound_line_mangler = 0;

static char	*epicrc_file = NULL,		/* full path .epicrc file */
		*ircrc_file = NULL;		/* full path .ircrc file */
char		*startup_file = NULL,		/* Set when epicrc loaded */
		*my_path = (char *) 0,		/* path to users home dir */
		*irc_lib = (char *) 0,		/* path to the ircII library */
		*default_channel = NULL,	/* Channel to join on connect */
		nickname[NICKNAME_LEN + 1],	/* users nickname */
		*send_umode = NULL;		/* sent umode */
char		*cut_buffer = (char *) 0;	/* global cut_buffer */

const char	empty_string[] = "",		/* just an empty string */
		space[] = " ",			/* just a lonely space */
		on[] = "ON",
		zero[] = "0",
		one[] = "1",
		star[] = "*",
		dot[] = ".",
		comma[] = ",";

static		char	switch_help[] =
"Usage: epic [switches] [nickname] [server list]                      \n\
  The [nickname] can be up to 30 characters long                      \n\
  The [server list] are one or more server descriptions               \n\
  The [switches] are zero or more of the following:                   \n\
      -a\tThe [server list] adds to default server list               \n"
"      -b\tThe program should run in the background ``bot mode''       \n"
"      -B\tLoads your .ircrc file before you connect to a server.      \n\
      -d\tThe program should run in ``dumb mode'' (no fancy screen)   \n\
      -h\tPrint this help message                                     \n\
      -q\tThe program will not load your .ircrc file                  \n\
      -s\tThe program will not connect to a server upon startup       \n\
      -S\tEach argument will be tokenised by whitechar                \n\
      -v\tPrint the version of this irc client and exit               \n\
      -x\tRun the client in full X_DEBUG mode                         \n\
      -c <chan>\tJoin <chan> after first connection to a server       \n\
      -H <host>\tUse a virtual host instead of default hostname	      \n\
      -l <file>\tLoads <file> instead of your .ircrc file             \n\
      -L <file>\tLoads <file> instead of your .ircrc file             \n\
      -n <nick>\tThe program will use <nick> as your default nickname \n\
      -p <port>\tThe program will use <port> as the default portnum   \n\
      -z <user>\tThe program will use <user> as your default username \n";



typedef void (*AtExitFunction) (void);
AtExitFunction	at_exit_functions[128];
int	next_at_exit_function_refnum = 0;

void	at_irc_exit (AtExitFunction f)
{
	if (next_at_exit_function_refnum >= 127)
		return;

	at_exit_functions[next_at_exit_function_refnum++] = f;
}

static SIGNAL_HANDLER(sig_irc_exit)
{
	irc_exit (1, NULL);
}

/* irc_exit: cleans up and leaves */
void	irc_exit (int really_quit, const char *format, ...)
{
	char 	buffer[BIG_BUFFER_SIZE];
	char *	quit_message = NULL;
	int	old_window_display;
	int	value;

	/*
	 * If we get called recursively, something is hosed.
	 * Each recursion we get more insistant.
	 */
	if (dead == 1)
		exit(1);			/* Take that! */
	if (dead == 2)
		_exit(1);			/* Try harder */
	if (dead >= 3)
		kill(getpid(), SIGKILL);	/* DIE DIE DIE! */

	/* Faults in the following code are just silently punted */
	dead++;	

	if (really_quit == 0)	/* Don't clean up if we're crashing */
		goto die_now;

	if (format)
	{
		va_list arglist;
		va_start(arglist, format);
		vsnprintf(buffer, sizeof(buffer), format, arglist);
		va_end(arglist);
		quit_message = buffer;
	}
	else
	{
		strlcpy(buffer, "Default", sizeof(buffer));
		quit_message = NULL;
	}

	/* Do some clean up */
	do_hook(EXIT_LIST, "%s", buffer);

	servers_close_all(quit_message);
	value = 0;
	logger(&value);
	get_child_exit(-1);  /* In case some children died in the exit hook. */
	clean_up_processes();
	close_all_dbms();

	/* Arrange to have the cursor on the input line after exit */
	if (!dumb_mode)
	{
		cursor_to_input();
		term_cr();
		term_clear_to_eol();
		term_reset();
	}
	
	/* Try to free as much memory as possible */
	old_window_display = swap_window_display(0);
/*	dumpcmd(NULL, NULL, NULL); */
	remove_channel(NULL, 0);
	set_lastlog_size(&value);
	delete_all_windows();
	server_list_remove_all();

	remove_bindings();
	flush_on_hooks();
	flush_all_symbols();
	swap_window_display(old_window_display);

	for (int i = 0; i < next_at_exit_function_refnum; i++)
		at_exit_functions[i]();
		
	printf("\r");
	fflush(stdout);

	if (really_quit)
		exit(0);

die_now:
	my_signal(SIGABRT, SIG_DFL);
	kill(getpid(), SIGABRT);
	kill(getpid(), SIGQUIT);
	exit(1);
}

volatile sig_atomic_t	dead_children_processes;

/* 
 * This is needed so that the fork()s we do to read compressed files dont
 * sit out there as zombies and chew up our fd's while we read more.
 */
static SIGNAL_HANDLER(child_reap)
{
	dead_children_processes = 1;
}

volatile sig_atomic_t	segv_recurse = 0;

/* sigsegv: something to handle segfaults in a nice way */
static SIGNAL_HANDLER(coredump)
{
	if (segv_recurse++)
		exit(1);

	if (!dead)
	{
		term_reset();
		fprintf(stderr, "\
									\n\
									\n\
									\n\
* * * * * * * * * * * * * * * * * * * * * * * *				\n\
EPIC has trapped a critical protection error.				\n\
This is probably due to a bug in the program.				\n\
									\n\
If you have access to the 'BUG_FORM' in the ircII source distribution,	\n\
we would appreciate your filling it out if you feel doing so would	\n\
be helpful in finding the cause of your problem.			\n\
									\n\
If you do not know what the 'BUG_FORM' is or you do not have access	\n\
to it, please dont worry about filling it out.  You might try talking	\n\
to the person who is in charge of IRC at your site and see if you can	\n\
get them to help you.							\n\
									\n\
This version of EPIC is --->[%s (%lu)]					\n\
The date of release is  --->[%s]					\n\
									\n\
* * * * * * * * * * * * * * * * * * * * * * * *				\n\
The program will now terminate.						\n", irc_version, commit_id, internal_version);

		fflush(stdout);
		panic_dump_call_stack();

		while ((x_debug & DEBUG_CRASH) && !sleep(1)) {};
	}

        if (x_debug & DEBUG_CRASH)
                irc_exit(0, "Hmmm. %s (%lu) has another bug.  Go figure...",
			irc_version, commit_id);
        else
                irc_exit(1, "Hmmm. %s (%lu) has another bug.  Go figure...",
			irc_version, commit_id);
}

/*
 * cntl_c: emergency exit.... if somehow everything else freezes up, hitting
 * ^C five times should kill the program.   Note that this only works when
 * the program *is* frozen -- if it doesnt die when you do this, then the
 * program is functioning correctly (ie, something else is wrong)
 */
static SIGNAL_HANDLER(cntl_c)
{
	/* after 5 hits, we stop whatever were doing */
	if (cntl_c_hit++ >= 4)
		irc_exit(1, "User pressed ^C five times.");
	else if (cntl_c_hit > 1)
		kill(getpid(), SIGUSR2);
}

static SIGNAL_HANDLER(nothing)
{
	/* nothing to do! */
}

static SIGNAL_HANDLER(sig_user1)
{
	say("Got SIGUSR1, closing EXECed processes");
	clean_up_processes();
}

static SIGNAL_HANDLER(sig_user2)
{
	system_exception++;
}

static	void	show_version (void)
{
	printf("ircII %s (Commit id: %lu) (Date of release: %s) (git: %s)\n\r", 
			irc_version, commit_id, internal_version, git_commit);
	printf("Compile metadata: %s\n", compile_info);
	printf("Compiler: %s\n", compiler_version);
	printf("Configure options: %s\n", configure_args);
	printf("Compilation FLAGS: %s\n", compile_cflags);
	printf("Compilation LIBS: %s\n", compile_libs);
	printf("Compilation link: %s\n", final_link);
	printf("OpenSSL version: %#8.8lx\n", OpenSSL_version_num());
	printf("OpenSSL version: %s\n", OpenSSL_version(OPENSSL_VERSION));
	exit(0);
}

/*
 * parse_args: parse command line arguments for irc, and sets all initial
 * flags, etc. 
 *
 * major rewrite 12/22/94 -jfn
 * major rewrite 02/18/97 -jfn
 *
 * Sanity check:
 *   Supported flags: -a, -b, -B, -d, -f, -F, -h, -q, -s -v, -x
 *   Flags that take args: -c, -l, -L, -n, -p, -z
 *
 * We use getopt() so that your local argument passing convension
 * will prevail.  The first argument that occurs after all of the normal 
 * arguments have been parsed will be taken as a default nickname.
 * All the rest of the args will be taken as servers to be added to your
 * default server list.
 */
static	void	parse_args (int argc, char **argv)
{
	int 		ch;
	int 		append_servers = 0;
	struct passwd *	entry;
	char *		ptr = (char *) 0;
	const char *	cptr = NULL;
	char *		the_path = NULL;

	/* 
	 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/unistd.h.html
	 * says that <unistd.h> shall define 'optarg' and 'optind'.
	 * In the past, we used to declare them extern ourselves.
	 */

	*nickname = 0;

	/* 
	 * Its probably better to parse the environment variables
	 * first -- that way they can be used as defaults, but can 
	 * still be overriden on the command line.
	 */
	if ((entry = getpwuid(getuid())))
	{
		if (entry->pw_gecos && *(entry->pw_gecos))
		{
			if ((ptr = strchr(entry->pw_gecos, ',')))
				*ptr = 0;
			set_var_value(DEFAULT_REALNAME_VAR, entry->pw_gecos, 0);
		}

		if (entry->pw_name && *(entry->pw_name))
			set_var_value(DEFAULT_USERNAME_VAR, entry->pw_name, 0);

		if (entry->pw_dir && *(entry->pw_dir))
			malloc_strcpy(&my_path, entry->pw_dir);
	}


	if ((cptr = getenv("IRCNICK")))
		strlcpy(nickname, cptr, sizeof nickname);

	/* XXX Rewrite this XXX */
	/*
	 * We now allow users to use IRCUSER or USER if we couldnt get the
	 * username from the password entries.  For those systems that use
	 * NIS and getpwuid() fails (boo, hiss), we make a last ditch effort
	 * to see what LOGNAME is (defined by POSIX.2 to be the canonical 
	 * username under which the person logged in as), and if that fails,
	 * we're really tanked, so we just let the user specify their own
	 * username.  I think everyone would have to agree this is the most
	 * reasonable way to handle this.
	 */
	if (empty(get_string_var(DEFAULT_USERNAME_VAR)))
		if ((cptr = getenv("LOGNAME")) && *cptr)
			set_var_value(DEFAULT_USERNAME_VAR, cptr, 0);

	if ((cptr = getenv("IRCUSER")) && *cptr) 
		set_var_value(DEFAULT_USERNAME_VAR, cptr, 0);
	else if (empty(get_string_var(DEFAULT_USERNAME_VAR)))
		;
	else if ((cptr = getenv("USER")) && *cptr) 
		set_var_value(DEFAULT_USERNAME_VAR, cptr, 0);
	else if ((cptr = getenv("HOME")) && *cptr)
	{
		const char *cptr2 = strrchr(cptr, '/');
		if (cptr2)
			set_var_value(DEFAULT_USERNAME_VAR, cptr2, 0);
		else
			set_var_value(DEFAULT_USERNAME_VAR, cptr, 0);
	}
	else
	{
		fprintf(stderr, "I dont know what your user name is.\n");
		fprintf(stderr, "Set your LOGNAME environment variable\n");
		fprintf(stderr, "and restart EPIC.\n");
		exit(1);
	}
	/* XXX Rewrite this ^^^^ */

	if ((cptr = getenv("IRCNAME")))
		set_var_value(DEFAULT_REALNAME_VAR, cptr, 0);
	else if ((cptr = getenv("NAME")))
		set_var_value(DEFAULT_REALNAME_VAR, cptr, 0);
	else if ((cptr = getenv("REALNAME")))
		set_var_value(DEFAULT_REALNAME_VAR, cptr, 0);
	else
	{
		cptr = get_string_var(DEFAULT_REALNAME_VAR);
		if (!cptr || !*cptr)
			set_var_value(DEFAULT_REALNAME_VAR, "*Unknown*", 0);
	}

	if ((cptr = getenv("HOME")))
		malloc_strcpy(&my_path, cptr);
	else if (!my_path)
		malloc_strcpy(&my_path, "/");



	if ((cptr = getenv("IRCPORT")))
		irc_port = my_atol(cptr);

	if ((cptr = getenv("EPICRC")))
		epicrc_file = malloc_strdup(cptr);
	else
		epicrc_file = malloc_strdup2(my_path, EPICRC_NAME);

	if ((cptr = getenv("IRCRC")))
		ircrc_file = malloc_strdup(cptr);
	else
		ircrc_file = malloc_strdup2(my_path, IRCRC_NAME);

	if ((cptr = getenv("IRCLIB")))
		irc_lib = malloc_strdup2(cptr, "/");
	else
		irc_lib = malloc_strdup(IRCLIB);

	if ((cptr = getenv("IRCUMODE")))
		send_umode = malloc_strdup(cptr);

	if ((cptr = getenv("IRCPATH")))
		the_path = malloc_strdup(cptr);
	else
		the_path = malloc_sprintf(NULL, DEFAULT_IRCPATH, irc_lib);

	set_var_value(LOAD_PATH_VAR, the_path, 0);
	new_free(&the_path);

	if ((cptr = getenv("IRCHOST")) && *cptr)
		tmp_hostname = malloc_strdup(cptr);

	/*
	 * Parse the command line arguments.
	 */
	while ((ch = getopt(argc, argv, "aBbc:dhH:l:L:n:p:qsSvxz:")) != EOF)
	{
		switch (ch)
		{
			case 'v':	/* Output ircII version */
				show_version();
				/* NOTREACHED */
				exit(0);

			case 'p': /* Default port to use */
				irc_port = my_atol(optarg);
				break;

			case 'd': /* use dumb mode */
				dumb_mode = 1;
				break;

			case 'l': /* Load some file instead of ~/.ircrc */
			case 'L': /* Same as above. Doesnt work like before */
				malloc_strcpy(&epicrc_file, optarg);
				break;

			case 'a': /* append server, not replace */
				append_servers = 1;
				break;

			case 'q': /* quick startup -- no .ircrc */
				quick_startup = 1;
				break;

			case 's': /* dont connect - let user choose server */
				dont_connect = 1;
				break;
			
			case 'S': /* dummy option - to not choke on -S */
				break;

			case 'b':
				dumb_mode = 1;
				use_input = 0;
				background = 1;
				break;

			case 'n':
				strlcpy(nickname, optarg, sizeof nickname);
				break;

			case 'x': /* x_debug flag */
				x_debug = (unsigned long)0x0fffffff;
				break;

			case 'z':
				set_var_value(DEFAULT_USERNAME_VAR, optarg, 0);
				break;

			case 'B':
				/* Historical option */
				break;

			case 'c':
				malloc_strcpy(&default_channel, optarg);
				break;

			case 'H':
				tmp_hostname = malloc_strdup(optarg);
				break;

			default:
			case 'h':
			case '?':
				fputs(switch_help, stderr);
				exit(1);
		} /* End of switch */
	}
	argc -= optind;
	argv += optind;

	if (argc && **argv && !strchr(*argv, '.'))
		strlcpy(nickname, *argv++, sizeof nickname), argc--;

	/*
 	 * "nickname" needs to be valid before we call build_server_list,
	 * so do a final check on whatever nickname we're going to use.
	 */
	if (!*nickname)
		strlcpy(nickname, get_string_var(DEFAULT_USERNAME_VAR), 
				sizeof nickname);

	for (; *argv; argc--, argv++)
		if (**argv)
			serverdesc_insert(*argv);

	if (!use_input && quick_startup)
	{
		fprintf(stderr, "Cannot use -b and -q at the same time\n");
		exit(1);
	}
	if (!use_input && dont_connect)
	{
		fprintf(stderr, "Cannot use -b and -s at the same time\n");
		exit(1);
	}

	if (!check_nickname(nickname))
	{
		fprintf(stderr, "Invalid nickname: [%s]\n", nickname);
		fprintf(stderr, "Please restart EPIC with a valid nickname\n");
		exit(1);
	}

	/*
	 * Find and build the server lists...
	 */
	if ((cptr = getenv("IRCSERVER")))
	{
		char *	arg;
		char *	ptr_;

		ptr_ = ptr = malloc_strdup(cptr);
		while ((arg = next_arg(ptr, &ptr)))
			serverdesc_insert(arg);
		new_free(&ptr_);
	}

	if (!server_list_size() || append_servers)
		serverdesc_import_default_file();

	return;
}

static	void	init_vhosts_stage2 (void)
{
	/*
	 * Figure out our virtual hostname, if any.
	 */
	if (tmp_hostname)
	{
		char *s = set_default_hostnames(tmp_hostname);
		fprintf(stderr, "%s\n", s);
		new_free(&s);
	}
	new_free(&tmp_hostname);
}

/* fire scripted signal events -pegasus */
static void	do_signals(void)
{
	int sig_no;

	signals_caught[0] = 0;
	for (sig_no = 1; sig_no < NSIG; sig_no++)
	{
		while (signals_caught[sig_no])
		{
			do_hook(SIGNAL_LIST, "%d %ld", sig_no,
				(long)signals_caught[sig_no]);
			do_hook(SIGNAL_LIST, "%s %ld", get_signal_name(sig_no),
				(long)signals_caught[sig_no]);
			signals_caught[sig_no]--;
		}
	}
}

/*
 * io - Handle one event (the event looper)
 *
 * Arguments:
 *	what	- Who the caller is.  Gives the user context in case of a runaway
 *
 * The main event looper calls this function in an infinite loop.
 *	It handles one "cycle" or "sequence point".
 *	1. Handle a "system reset"
 *	2. Handle user pressing ^C 5 times if we're "stuck"
 *	3. Keep track of the recursion level
 * 	4. Figure out when the next /TIMER expires (this may be 0)
 *	5. SLEEP until something needs to happen
 *	   a. If the /TIMER expired, run it
 *	   b. If we got a signal, keep track of that
 *	   c. If an fd is ready, data from it was buffered up
 *	        What we're expected to do is consume that data.
 *	6. Do routine post-processing tasks:
 *	   a. Process signals
 *	   b. Reap dead children processes
 *	   c. Run /DEFERed commands
 *	   d. Verify referential integrity of windows and servers
 *	   e. Verify referential integrity of windows and channels
 *	   f. If necessary, redraw the entire screen
 *	   g. If necessary, redraw specific windows
 *	   h. Move the cursor back to the input line
 *	7. Unwind the recursion level and sanity check it.
 *
 * In theory, every time through the loop is "clean" -- one thing happens,
 * and there is no carry-over state to the next loop.  However, that is not
 * perfect, and the loop is self-correcting, and has some failsafes:
 *	1. Polling loop
 *	2. User presses ^C 5 times when the client is jammed
 *	3. Excessive recursion (in a script or otherwise)
 *
 * Notes:
 *	This is re-entrant, and you can call it, but you should only call it
 *	when you are waiting for something specific to happen before you continue.
 *	Callbacks are better than recursion.
 */
void	io (const char *what)
{
static	const	char	*caller[51] = { NULL }; /* XXXX */
static	int		level = 0,
			old_level = 0,
			last_warn = 0;
	Timespec	timer;

	sequence_point++;

	if (system_reset)
	{
		int	i;

		system_reset = 0;
		for (i = 0; i < 51; i++)
			caller[i] = NULL;
		check_context_queue(1);
		level = 0;
	}

	level++;

	/* Don't let this accumulate behind the user's back. */
	cntl_c_hit = 0;

	if (x_debug & DEBUG_WAITS)
	{
	    if (level != old_level)
	    {
		debug(DEBUG_WAITS, "Moving from io level [%d] to level [%d] from [%s]", 
				old_level, level, what);
		old_level = level;
	    }
	}

	/* Try to avoid letting recursion of io() to get out of control */
	if (level && (level - last_warn == 5))
	{
	    last_warn = level;
	    yell("io's recursion level is [%d],  [%s]<-[%s]<-[%s]<-[%s]<-[%s]",
			level, what, caller[level-1], caller[level-2], 
					caller[level-3], caller[level-4]);
	    if (level % 50 == 0)
		panic(1, "Ahoy there matey!  Abandon ship!");
	}
	else if (level && (last_warn - level == 5))
	    last_warn -= 5;

	caller[level] = what;

	/* Calculate the time to the next timer timeout */
	timer = TimerTimeout();

	/* GO AHEAD AND WAIT FOR SOME DATA TO COME IN */
	make_window_current_by_refnum(0);
	switch (do_wait(&timer))
	{
		/* Timeout -- Need to do timers */
		case 0:
		{
			ExecuteTimers();
			break;
		}

		/* Interrupted system call -- check for SIGINT */
		case -1:
		{
			if (cntl_c_hit)		/* SIGINT is useful */
			{
				user_input_byte('\003');
				cntl_c_hit = 0;
			}
			else if (errno != EINTR) /* Deal with EINTR */
				yell("Select failed with [%s]", strerror(errno));
			break;
		}

		/* Check it out -- something is on one of our descriptors. */
		default:
		{
			do_filedesc();
			break;
		} 
	}

	/* 
	 * Various things that need to be done synchronously...
	 */

	/* deal with caught signals - pegasus */
	if (signals_caught[0] != 0)
		do_signals();

	/* Account for dead child processes */
	get_child_exit(-1);

	/* Run /DEFERed commands */
	if (level == 1 && need_defered_commands)
		do_defered_commands();

	/* Make sure all the servers are connected that ought to be */
	window_check_servers();

	/* Make sure all the channels are joined that ought to be */
	window_check_channels();

	/* Redraw the screen after a SIGCONT */
	if (need_redraw)
		redraw_all_screens();

	if (x_debug & DEBUG_SEQUENCE_POINTS)
		update_all_status();

	/* Make sure all the windows and status bars are made current */
	update_all_windows();

	/* Move the cursor back to the input line */
	cursor_to_input();

	/* Release this io() accounting level */
	caller[level] = NULL;
	level--;

	if (level == 0)
		check_context_queue(0);

	return;
}

/*************************************************************************/
static void    load_ircrc (void)
{
	if (startup_file || quick_startup)
		return;

	/* 
	 * epicrc_file and ircrc_file can't be NULL here, but try 
	 * telling clang's static analyzer that... bleh
	 */
	if (epicrc_file && access(epicrc_file, R_OK) == 0)
		startup_file = malloc_strdup(epicrc_file);
	else if (ircrc_file && access(ircrc_file, R_OK) == 0)
		startup_file = malloc_strdup(ircrc_file);
	else
		startup_file = malloc_strdup("global");

	load("LOAD", startup_file, empty_string);
}

/*************************************************************************/
int 	main (int argc, char *argv[])
{
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");

	create_utf8_locale();
	setvbuf(stdout, NULL, _IOLBF, 1024);
        get_time(&start_time);

	init_levels();
	init_transforms();
	init_recodings();
	init_variables_stage1();
	init_vhosts_stage1();
	parse_args(argc, argv);
	init_binds();
	init_keys();
	init_commands();
	init_functions();
	init_expandos();
	init_newio();
	init_ctcp();
	init_ares();
	init_vhosts_stage2();

	fprintf(stderr, "EPIC VI -- %s\n", ridiculous_version_name);
	fprintf(stderr, "EPIC Software Labs (2006)\n");
	fprintf(stderr, "Version (%s), Commit Id (%lu) -- Date (%s)\n", 
				irc_version, commit_id, internal_version);
	fprintf(stderr, "%s\n", compile_info);
	fprintf(stderr, "OpenSSL version: %#8.8lx\n", OpenSSL_version_num());

	/* If we're a bot, do the bot thing. */
	if (!use_input)
	{
		pid_t	child_pid;

		child_pid = fork();
		if (child_pid == -1)
		{
			fprintf(stderr, "Could not fork a child process: %s\n",
					strerror(errno));
			_exit(1);
		}
		else if (child_pid > 0)
		{
			fprintf(stderr, "Process [%d] running in background\n",
					child_pid);
			_exit(0);
		}
	}
	else
	{
		fprintf(stderr, "Process [%d]", getpid());
		if (isatty(0))
			fprintf(stderr, " connected to tty [%s]", ttyname(0));
		else
			dumb_mode = 1;
		fprintf(stderr, "\n");
	}

	init_signals();
	init_signal_names();

	/* these should be taken by both dumb and smart displays */
	my_signal(SIGSEGV, coredump);
	my_signal(SIGBUS, coredump);
	my_signal(SIGQUIT, SIG_IGN);
	my_signal(SIGHUP, sig_irc_exit);
	my_signal(SIGTERM, sig_irc_exit);
	my_signal(SIGPIPE, SIG_IGN);
	my_signal(SIGCHLD, child_reap);
	my_signal(SIGINT, cntl_c);
	my_signal(SIGALRM, nothing);
	my_signal(SIGUSR1, sig_user1);
	my_signal(SIGUSR2, sig_user2);

	set_context(-1, -1, NULL, NULL, LEVEL_OTHER);

	/* 
	 * We use dumb mode for -d, -b, when stdout is redirected to a file,
	 * or as a failover if init_screen() fails. 
	 */
	if ((dumb_mode == 0) && (init_screen() == 0))
	{
		my_signal(SIGCONT, term_cont);
		my_signal(SIGWINCH, sig_refresh_screen);

		init_variables_stage2();
		permit_status_update(1);
		build_status(NULL);
		update_input(-1, UPDATE_ALL);
	}
	else
	{
		if (background)
		{
			my_signal(SIGHUP, SIG_IGN);
			if (!freopen("/dev/null", "w", stdout)) 
				(void) 0;
		}
		dumb_mode = 1;		/* Just in case */
		create_new_screen(1);
		new_window(main_screen);
		init_variables_stage2();
		build_status(NULL);
	}

	/* Get the terminal-specific keybindings now */
	init_termkeys();

	/* The all-collecting stack frame */
	make_local_stack("TOP");

	load_ircrc();

	init_input();

	if (dont_connect)
		server_list_display();		/* Let user choose server */
	else
		set_window_server(0, 0);	/* Connect to default server */

	/* The user may have used -S and /server in their startup script */
	window_check_servers();

	get_time(&idle_time);
	update_system_timer(NULL);

	/*
	 * Have you ever seen a setjmp() before?
	 * The first time it returns 0 (thus is false and falls through)
	 * The second time it returns != 0 (thus is true, and calls itself twice)
	 * Consequently, this loop assures that panic_jumpseat is initialized.
	 * You can return here later with longjmp(panic_jumpseat);
	 */
	while (setjmp(panic_jumpseat))
		system_reset = 1;

	for (;;system_exception = 0)
		io("main");
	/* NOTREACHED */
}
