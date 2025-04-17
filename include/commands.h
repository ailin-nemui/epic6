/*
 * commands.h: header for commands.c
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1994 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __commands_h__
#define __commands_h__

extern	int	will_catch_break_exceptions;
extern	int	will_catch_continue_exceptions;
extern	int	will_catch_return_exceptions;
extern	int	break_exception;
extern	int	continue_exception;
extern	int	return_exception;
extern	volatile sig_atomic_t	system_exception;
extern	const char *		current_command;

extern	int	need_defered_commands;

	void	init_commands		(void);

        char *  call_lambda_function    (const char *, const char *, const char *);
        void    call_lambda_command     (const char *, const char *, const char *);
        char *  call_user_function      (const char *, const char *, char *, void *);
        void    call_user_command       (const char *, const char *, char *, void *);
	void	runcmds			(const char *, const char *);
        void    runcmds_with_arglist    (const char *, char *, const char *);

	int     parse_statement 	(const char *, int, const char *);

	BUILT_IN_COMMAND(load);
	void	send_text	 	(int, const char *, const char *, const char *, int, int);
	int	command_exist		(char *);
	BUILT_IN_COMMAND(e_channel);
	void	do_defered_commands	(void);
	char	*get_command		(const char *);

        void    dump_load_stack         (int);
const   char *  current_filename        (void);
const   char *  current_loader          (void);
        int     current_line            (void);
const   char *  current_package         (void);
	void	help_topics_commands	(FILE *);

#endif /* __commands_h__ */
