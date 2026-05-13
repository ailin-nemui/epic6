/*
 * alist.h -- resizeable arrays (dicts)
 * Copyright 1997 EPIC Software Labs
 */

#ifndef __alist_h__
#define __alist_h__
#include "irc.h"
#include "ircaux.h"

/*
 * Gemini suggested replacements for these two, but they didn't work.  Oh well!
 */

#ifdef __need_cs_alist_hash__
/*
 * This hash routine is for case sensitive keys.  Specifically keys that
 * have been prefolded to an apppropriate case.
 */
static uint32_t cs_alist_hash (const char *s, uint32_t *mask)
{
	uint32_t 	m;
	uint32_t 	uint;

	m = 0;
	uint = 0;

	// 1. Build the mask and fill the buffer
	// This handles strings from length 0 to 4+ safely.
	if (s[0]) 
	{
		uint |= ((unsigned char) s[0] << 24);
		m |= 0xFF000000;
		if (s[1])
		{
			uint |= ((unsigned char) s[1] << 16);
			m |= 0x00FF0000;
			if (s[2])
			{
				uint |= ((unsigned char) s[2] << 8);
				m |= 0x0000FF00;
				if (s[3])
				{
					uint |= ((unsigned char) s[2]);
					m |= 0x000000FF;
				}
			}
		}
	}

	*mask = m;
	return uint;
}
#endif

#ifdef __need_ci_alist_hash__
extern unsigned char *stricmp_tables[3];
/*
 * This hash routine is for case insensitive keys.  Specifically keys that
 * cannot be prefolded to an appropriate case but are still insensitive
 */
static uint32_t ci_alist_hash (const char *s, uint32_t *mask)
{
	uint32_t 	m;
	uint32_t 	uint;

	m = 0;
	uint = 0;

	// 1. Determine the length-based mask first
	// This is clean, branchless-adjacent logic
	if (s[0])
	{
		uint |= ((unsigned char)stricmp_tables[0][(unsigned char)s[0]]) << 24;
		m = 0xFF000000;
		if (s[1])
		{
			uint |= ((unsigned char)stricmp_tables[0][(unsigned char)s[1]]) << 16;
			m |= 0x00FF0000;
			if (s[2])
			{
				uint |= ((unsigned char)stricmp_tables[0][(unsigned char)s[2]]) << 8;
				m |= 0x0000FF00;
				if (s[3])
				{
					uint |= ((unsigned char)stricmp_tables[0][(unsigned char)s[3]]);
					m |= 0x000000FF;
				}
			}
		}
	}

	*mask = m;
	return uint;
}
#endif

typedef struct 
{
	char *		name;
	uint32_t	hash;
	void *		data;
} alist_item_;

typedef int       (*alist_func) (const char *, const char *, size_t);
typedef enum {
	HASH_INSENSITIVE,
	HASH_SENSITIVE
} hash_type;

/*
 * This is the actual list, that contains structs that are of the
 * form described above.  It contains the current size and the maximum
 * size of the alist.
 */
typedef struct
{
	alist_item_ **	list;
	int 		max;
	int 		total_max;
	alist_func 	func;
	hash_type 	hash;
} alist;

void *	add_to_alist 		(alist *, const char *, void *);
void *	remove_from_alist 	(alist *, const char *);
void *	alist_lookup 		(alist *, const char *, int);
void *	find_alist_item 	(alist *, const char *, int *, int *);
void *	alist_pop		(alist *, int);
void *  get_alist_item 		(alist *, int);

#endif
