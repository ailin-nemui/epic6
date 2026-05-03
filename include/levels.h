/*
 * levels.h: Unified levels system
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997, 2003 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __levels_h__
#define __levels_h__

extern int	LEVEL_NONE;
extern int	LEVEL_OTHER;
extern int	LEVEL_PUBLIC;
extern int	LEVEL_MSG;
extern int	LEVEL_NOTICE;
extern int	LEVEL_WALL;
extern int	LEVEL_WALLOP;
extern int	LEVEL_OPNOTE;
extern int	LEVEL_SNOTE;
extern int	LEVEL_ACTION;
extern int	LEVEL_CTCP;
extern int	LEVEL_INVITE;
extern int	LEVEL_JOIN;
extern int	LEVEL_NICK;
extern int	LEVEL_TOPIC;
extern int	LEVEL_PART;
extern int	LEVEL_QUIT;
extern int	LEVEL_KICK;
extern int	LEVEL_MODE;
extern int	LEVEL_OPERWALL;
extern int	LEVEL_SYSERR;
extern int	LEVEL_USER1;
extern int	LEVEL_USER2;
extern int	LEVEL_USER3;
extern int	LEVEL_USER4;
extern int	LEVEL_USER5;
extern int	LEVEL_USER6;
extern int	LEVEL_USER7;
extern int	LEVEL_USER8;
extern int	LEVEL_USER9;
extern int	LEVEL_USER10;
extern int	LEVEL_ALL;


/* XXX Revisit this */
#define BIT_MAXBIT	(63)
#define BIT_VALID(i)	((i) >= 0 && (i) <= (BIT_MAXBIT))

typedef struct Mask {
	uint64_t	__bits;
} Mask;

	int		mask_setall 		(Mask *set);
	int		mask_unsetall 		(Mask *set);
	int		mask_set 		(Mask *set, int bit);
	int		mask_unset 		(Mask *set, int bit);
	int		mask_isall 		(const Mask *set);
	int		mask_isnone 		(const Mask *set);
	int		mask_isset 		(const Mask *set, int bit);

	void		init_levels		(void);
	int		add_new_level		(const char *);
	int		add_new_level_alias 	(int, const char *);
	char *		get_all_levels		(void);
	const char *	mask_to_str		(const Mask *);
	int		str_to_mask		(Mask *, const char *, char **);
	void    	standard_level_warning 	(const char *, char **);
	const char *	level_to_str		(int);
	int		str_to_level		(const char *);
	char *		levelctl		(char *);

#endif
