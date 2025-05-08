/*
 * timer.h: header for timer.c 
 *
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __timer_h__
#define __timer_h__

	BUILT_IN_COMMAND(timercmd);

typedef enum {
	SERVER_TIMER,
	WINDOW_TIMER,
	GENERAL_TIMER
} TimerDomain;
 
	void	ExecuteTimers 	(void);
	char *	add_timer	(int, const char *, double, long, 
				 int (*) (void *), void *, const char *, 
				 TimerDomain, int, int, int);
	int	timer_exists	(const char *);
	int     remove_timer	(const char *);
	Timespec	TimerTimeout 	(void);
	char *	timerctl	(char *);
	void	dump_timers	(void);
	void    timers_swap_windows (unsigned oldref, unsigned newref);
	void	timers_merge_windows (unsigned oldref, unsigned newref);
	void    unload_timers	(char *filename);

#endif /* _TIMER_H_ */
