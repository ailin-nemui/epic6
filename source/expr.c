/*
 * expr.c -- The expression mode parser and the textual mode parser
 * #included by alias.c -- DO NOT DELETE
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1993, 2003 EPIC Software Labs
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

#include <math.h>

/* Function decls */
static	void	TruncateAndEscape (char **, const char *, ssize_t, const char *);
static	char *	alias_special_char (char **, char *, const char *, char *);
static	void	do_alias_string (const char *, const char *);

char *alias_string = NULL;

/************************** EXPRESSION MODE PARSER ***********************/
/* canon_number: canonicalizes number to something relevant */
/* If FLOATING_POINT_MATH isnt set, it truncates it to an integer */
char *canon_number (char *input)
{
	int end = strlen(input);

	if (end)
		end--;
	else
		return input;		/* nothing to do */

	if (get_int_var(FLOATING_POINT_MATH_VAR))
	{
		/* remove any trailing zeros */
		while (input[end] == '0')
			end--;

		/* If we removed all the zeros and all that is
		   left is the decimal point, remove it too */
		if (input[end] == '.')
			end--;

		input[end+1] = 0;
	}
	else
	{
		char *dotloc = strchr(input, '.');
		if (dotloc)
			*dotloc = 0;
	}

	return input;
}



/*
 * parse_inline:  This evaluates user-variable expression. 
 */
char	*parse_inline (char *str, const char *args)
{
	return matheval(str, args);
}



/**************************** TEXT MODE PARSER *****************************/
/*
 * next_statement: Determines the length of the first statement in 'string'.
 *
 * A statement ends at the first semicolon EXCEPT:
 *   -- Anything inside (...) or {...} doesn't count
 */
ssize_t	next_statement (const char *string)
{
	const char *ptr;
	int	paren_count = 0, brace_count = 0;

	if (!string || !*string)
		return -1;

	for (ptr = string; *ptr; ptr++)
	{
	    switch (*ptr)
	    {
		case ';':
		    if (paren_count + brace_count)
		    	break;
		    goto all_done;
		case LEFT_PAREN:
		    paren_count++;
		    break;
		case LEFT_BRACE:
		    brace_count++;
		    break;
		case RIGHT_PAREN:
		    if (paren_count)
			paren_count--;
		    break;
		case RIGHT_BRACE:
		    if (brace_count)
			brace_count--;
		    break;
		case '\\':
		    ptr++;
		    if (!*ptr)
			goto all_done;
		    break;
	    }
	}

all_done:
	if (paren_count != 0)
	{
		privileged_yell("[%d] More ('s than )'s found in this "
				"statement: \"%s\"", paren_count, string);
	}
	else if (brace_count != 0)
	{
		privileged_yell("[%d] More {'s than }'s found in this "
				"statement: \"%s\"", brace_count, string);
	}

	return (ssize_t)(ptr - string);
}

/*
 * expand_alias: Expands inline variables in the given string and returns the
 * expanded string in a new string which is malloced by expand_alias(). 
 *
 * Also unescapes anything that was quoted with a backslash
 *
 * Behaviour is modified by the following:
 *	Anything between brackets (...) {...} is left unmodified.
 *	Backslash escapes are unescaped.
 */
char	*expand_alias	(const char *string, const char *args)
{
	char	*buffer = NULL,
		*ptr,
		*stuff = NULL,
		*escape_str = NULL;
	char	escape_temp[2];
	char	ch;
	int	is_quote = 0;

	if (!string || !*string)
		return malloc_strdup(empty_string);

	escape_temp[1] = 0;

	ptr = stuff = LOCAL_COPY(string);

	while (ptr && *ptr)
	{
		if (is_quote)
		{
			is_quote = 0;
			++ptr;
			continue;
		}

		switch (*ptr)
		{
		    case '$':
		    {
			char *buffer1 = NULL;

			/*
			 * Replace the $ with a nul, then ptr points 
			 * at the char after the $ (the expando)
			 */
			*ptr++ = 0;
			if (!*ptr)
			{
				/* 
				 * But if it's an $ at the end of
				 * the string -> ignore it.
				 */
				malloc_strcat(&buffer, empty_string);
				break;		/* Hrm. */
			}

			/* Append the stuff before the $ to the work buffer. */
			malloc_strcat_ues(&buffer, stuff, empty_string);

			/* 
			 * After a $ may be any number of ^x sequences,
			 * which cause 'x' to be escaped (x -> \x)
			 */
			for (; *ptr == '^'; ptr++)
			{
				ptr++;
				if (!*ptr)	/* Blah */
					break;
				escape_temp[0] = *ptr;
				malloc_strcat(&escape_str, escape_temp);
			}

			/* Now expand (and quote) the expando into 'buffer1' */
			/* The retval (stuff) is the byte after the expando */
			stuff = alias_special_char(&buffer1, ptr, args, escape_str);

			/* And then append that to the work buffer. */
			malloc_strcat(&buffer, buffer1);

			new_free(&buffer1);
			if (escape_str)		/* Why ``stuff''? */
				new_free(&escape_str);

			/* Set the next char to after the expando */
			ptr = stuff;
			break;
		    }

		    case LEFT_PAREN:
		    case LEFT_BRACE:
		    {
			ssize_t	span;

			ch = *ptr;
			*ptr = 0;
			malloc_strcat_ues(&buffer, stuff, empty_string);
			stuff = ptr;

			if ((span = MatchingBracket(stuff + 1, ch, 
					(ch == LEFT_PAREN) ?
					RIGHT_PAREN : RIGHT_BRACE)) < 0)
			{
				privileged_yell("Unmatched %c starting at [%-.20s]", ch, stuff + 1);
				/* 
				 * DO NOT ``OPTIMIZE'' THIS BECAUSE
				 * *STUFF IS NUL SO STRLEN(STUFF) IS 0!
				 */
				ptr = stuff + 1 + strlen(stuff + 1);
			}
			else
				ptr = stuff + 1 + span + 1;

			*stuff = ch;
			ch = *ptr;
			*ptr = 0;
			malloc_strcat(&buffer, stuff);
			stuff = ptr;
			*ptr = ch;
			break;
		    }

		    case '\\':
		    {
			is_quote = 1;
			ptr++;
			break;
		    }

		    default:
			ptr++;
			break;
		}
	}

	if (stuff)
		malloc_strcat_ues(&buffer, stuff, empty_string);

	if (!buffer)
		buffer = malloc_strdup(empty_string);

	if (get_int_var(DEBUG_VAR) & DEBUG_EXPANSIONS)
		privileged_yell("Expanded " BOLD_TOG_STR "[" BOLD_TOG_STR "%s" BOLD_TOG_STR "]" BOLD_TOG_STR " to " BOLD_TOG_STR "[" BOLD_TOG_STR "%s" BOLD_TOG_STR "]" BOLD_TOG_STR, string, buffer);

	return buffer;
}

/*
 * alias_special_char: Here we determine what to do with the character after
 * the $ in a line of text. The special characters are described more fully
 * in the help/ALIAS file.  But they are all handled here. Parameters are the
 * return char ** pointer to which things are placed,
 * a ptr to the string (the first character of which is the special
 * character), the args to the alias, and a character indication what
 * characters in the string should be quoted with a backslash.  It returns a
 * pointer to the character right after the converted alias.
 */
static	char	*alias_special_char (char **buffer, char *ptr, const char *args, char *quote_em)
{
	char	*tmp,
		c;
	int	my_upper,
		my_lower;
	ssize_t	length;
	ssize_t	span;

	length = 0;
	if ((c = *ptr) == LEFT_BRACKET)
	{
		ptr++;
		if ((span = MatchingBracket(ptr, '[', ']')) >= 0)
		{
			tmp = ptr + span;
			*tmp++ = 0;
			if (*ptr == '$')
			{
				char *	str;
				str = expand_alias(ptr, args);
				length = my_atol(str);
				new_free(&str);
			}
			else
				length = my_atol(ptr);

			ptr = tmp;
			c = *ptr;
		}
		else
		{
			say("Missing %c", RIGHT_BRACKET);
			return (ptr);
		}
	}

	/*
	 * Dont ever _ever_ _EVER_ break out of this switch.
	 * Doing so will catch the panic() at the end of this function. 
	 * If you need to break, then youre doing something wrong.
	 */
	tmp = ptr+1;
	switch (c)
	{
		/*
		 * Nothing to expand?  Hrm...
		 */
		case 0:
		{
			tmp = malloc_strdup(empty_string);
			TruncateAndEscape(buffer, tmp, length, quote_em);
			new_free(&tmp);
			return ptr;
		}

		/*
		 * $(...) is the "dereference" case.  It allows you to 
		 * expand whats inside of the parens and use *that* as a
		 * variable name.  The idea can be used for pointers of
		 * sorts.  Specifically, if $foo == [bar], then $($foo) is
		 * actually the same as $(bar), which is actually $bar.
		 * Got it?
		 *
		 * epic4pre1.049 -- I changed this somewhat.  I dont know if
		 * itll get me in trouble.  It will continue to expand the
		 * inside of the parens until the first character isnt a $.
		 * since by all accounts the result of the expansion is
		 * SUPPOSED to be an lvalue, obviously a leading $ precludes
		 * this.  However, there are definitely some cases where you
		 * might want two or even three levels of indirection.  I'm
		 * not sure I have any immediate ideas why, but Kanan probably
		 * does since he's the one that needed this. (esl)
		 */
		case LEFT_PAREN:
		{
			char 	*sub_buffer = NULL,
				*tmp2 = NULL, 
				*tmpsav = NULL,
				*ph = ptr + 1;

			if ((span = MatchingBracket(ph, '(', ')')) >= 0)
				ptr = ph + span;
			else if ((ptr = strchr(ph, ')')))
				ptr = strchr(ph, ')');
			else
			{
				yell("Unmatched ( after $ starting at [%-.20s] (continuing anyways)", ph);
				ptr = ph;
			}

			if (ptr)
				*ptr++ = 0;

			/*
			 * Keep expanding as long as neccesary.
			 */
			do
			{
				tmp2 = expand_alias(tmp, args);
				if (tmpsav)
					new_free(&tmpsav);
				tmpsav = tmp = tmp2;
			}
			while (tmp && *tmp == '$');

			if (tmp)
				alias_special_char(&sub_buffer, tmp, args, quote_em);

			/* Some kind of bogus expando */
			if (sub_buffer == NULL)
				sub_buffer = malloc_strdup(empty_string);

			if (!(x_debug & DEBUG_SLASH_HACK))
				TruncateAndEscape(buffer, sub_buffer, 
						length, quote_em);

			new_free(&sub_buffer);
			new_free(&tmpsav);
			return (ptr);
		}

		/*
		 * ${...} is the expression parser.  Given a mathematical
		 * expression, it will evaluate it and then return the result
		 * of the expression as the expando value.
		 */
		case LEFT_BRACE:
		{
			char	*ph = ptr + 1;

			/* 
			 * This didnt allow for nesting before.
			 */
			if ((span = MatchingBracket(ph, '{', '}')) >= 0)
				ptr = ph + span;
			else
				ptr = strchr(ph, '}');

			if (ptr)
				*ptr++ = 0;
			else
				yell("Unmatched { after $ starting at [%-.20s] (continuing anyways)", ph);

			if ((tmp = parse_inline(tmp, args)) != NULL)
			{
				TruncateAndEscape(buffer, tmp, length, quote_em);
				new_free(&tmp);
			}
			return (ptr);
		}

		/*
		 * $"..." is the syncrhonous input mechanism.  Given a prompt,
		 * This waits until the user enters a full line of text and
		 * then returns that text as the expando value.  Note that 
		 * this requires a recursive call to io(), so you have all of
		 * the synchronous problems when this is mixed with /wait and
		 * /redirect and all that jazz waiting for io() to unwind.
		 *
		 * This has been changed to use add_wait_prompt instead of
		 * get_line, and hopefully get_line will go away.  I dont 
		 * expect any problems, but watch out for this anyways.
		 *
		 * Also note that i added ' to this, so now you can also
		 * do $'...' that acts just like $"..." except it only
		 * waits for the next key rather than the next return.
		 */
		case DOUBLE_QUOTE:
		case '\'':
		{
			if ((ptr = strchr(tmp, c)))
				*ptr++ = 0;

			alias_string = NULL;
			add_wait_prompt(tmp, do_alias_string, empty_string,
				(c == DOUBLE_QUOTE) ? WAIT_PROMPT_LINE
						    : WAIT_PROMPT_KEY, 1);
			while (!alias_string)
				io("Input Prompt");

			TruncateAndEscape(buffer, alias_string, length,quote_em);
			new_free(&alias_string);
			return (ptr);
		}

		/*
		 * $* is the special "all args" expando.  You really
		 * shouldnt ever use $0-, because its a lot more involved
		 * (see below).  $* is handled here; quickly, quietly,
		 * and without any fuss.
		 */
		case '*':
		{
			if (args == NULL)
				args = LOCAL_COPY(empty_string);
			TruncateAndEscape(buffer, args, length, quote_em);
			return (ptr + 1);
		}

		/*
		 * $# and $@ are the "word count" and "length" operators.
		 * Given any normal rvalue following the # or @, this will
		 * return the number of words/characters in that result.
		 * As a special case, if no rvalue is specified, then the
		 * current args list is the default.  So, $# all by its
		 * lonesome is the number of arguments passed to the alias.
		 */
		case '#':
		case '@':
		{
			char 	c2 = 0;
			char 	*sub_buffer = NULL;
			char 	*rest, *val;
			int	my_dummy;

			rest = after_expando(ptr + 1, 0, &my_dummy);
			if (rest == ptr + 1)
			{
			    sub_buffer = malloc_strdup(args);
			}
			else
			{
			    c2 = *rest;
			    *rest = 0;
			    alias_special_char(&sub_buffer, ptr + 1, 
						args, quote_em);
			    *rest = c2;
			}

			if (!sub_buffer)
			    val = malloc_strdup(zero);
			else if (c == '#')
			    val = malloc_strdup(ltoa(count_words(sub_buffer, DWORD_EXTRACTW, "\"")));
			else
			    val = malloc_strdup(ltoa(strlen(sub_buffer)));

			TruncateAndEscape(buffer, val, length, quote_em);
			new_free(&val);
			new_free(&sub_buffer);

			if (c2)
			    *rest = c2;

			return rest;
		}

		/*
		 * Do some magic for $$.
		 */
		case '$':
		{
			TruncateAndEscape(buffer, "$", length, quote_em);
			return ptr + 1;
		}

		/*
		 * Ok.  So its some other kind of expando.  Generally,
		 * its either a numeric expando or its a normal rvalue.
		 */
		default:
		{
			/*
			 * Is it a numeric expando?  This includes the
			 * "special" expando $~.
			 */
			if (isdigit(c) || (c == '-') || c == '~')
			{
			    char *tmp2;

			    /*
			     * Handle $~.  EOS especially handles this
			     * condition.
			     */
			    if (c == '~')
			    {
				my_lower = my_upper = EOS;
				ptr++;
			    }

			    /*
			     * Handle $-X where X is some number.  Note that
			     * any leading spaces in the args list are
			     * retained in the result, even if X is 0.  The
			     * stock client stripped spaces out on $-0, but
			     * not for any other case, which was judged to be
			     * in error.  We always retain the spaces.
			     *
			     * If there is no number after the -, then the
			     * hyphen is slurped and expanded to nothing.
			     */
			    else if (c == '-')
			    {
				my_lower = SOS;
				ptr++;
				my_upper = parse_number(&ptr);
				if (my_upper == -1)
				    return endstr(ptr); /* error */
			    }

			    /*
			     * Handle $N, $N-, and $N-M, where N and M are
			     * numbers.
			     */
			    else
			    {
				my_lower = parse_number(&ptr);
				if (*ptr == '-')
				{
				    ptr++;
				    my_upper = parse_number(&ptr);
				    if (my_upper == -1)
					my_upper = EOS;
				}
				else
				    my_upper = my_lower;
			    }

			    /*
			     * Protect against a crash.  There
			     * are some gross syntactic errors
			     * that can be made that will result
			     * in ''args'' being NULL here.  That
			     * will crash the client, so we have
			     * to protect against that by simply
			     * chewing the expando.
			     */
			    if (!args)
				tmp2 = malloc_strdup(empty_string);
			    else
				tmp2 = extractew2(args, my_lower, my_upper);

			    TruncateAndEscape(buffer, tmp2, length, quote_em);
			    new_free(&tmp2);
			    if (!ptr)
				panic(1, "ptr is NULL after parsing numeric expando");
			    return ptr;
			}

			/*
			 * Ok.  So we know we're doing a normal rvalue
			 * expando.  Slurp it up.
			 */
			else
			{
			    char  *rest, d = 0;
			    int	  function_call = 0;

			    rest = after_expando(ptr, 0, &function_call);
			    if (*rest)
			    {
				d = *rest;
				*rest = 0;
			    }

			    if (function_call)
				tmp = call_function(ptr, args);
			    else
				tmp = get_variable_with_args(ptr, args);

			    if (!tmp)
				tmp = malloc_strdup(empty_string);

			    TruncateAndEscape(buffer, tmp, length, quote_em);

			    if (function_call)
				MUST_BE_MALLOCED(tmp, "BOGUS RETVAL FROM FUNCTION CALL");
			    else
				MUST_BE_MALLOCED(tmp, "BOGUS RETVAL FROM VARIABLE EXPANSION");

			    new_free(&tmp);	/* Crash here */

			    if (d)
				*rest = d;

			    return(rest);
			}
		}
	}
	panic(1, "Returning NULL from alias_special_char");
	return NULL;
}

/*
 * TruncateAndEscape: This handles string width formatting and \-escaping for irc 
 * variables when [] or ^x is specified.
 */
static	void	TruncateAndEscape (char **buff, const char *add, ssize_t length, const char *quote_em)
{
	char *	buffer;
	char *	free_me = NULL;
	int	justify;
	int	pad;

	if (!add)
		panic(1, "add is NULL");

	/* 
	 * Semantics:
	 *	If length is nonzero, then "add" will be truncated
	 *	   to "length" characters
	 * 	If length is zero, nothing is done to "add"
	 *	If quote_em is not NULL, then the resulting string
	 *	   will be quoted and appended to "buff"
	 *	If quote_em is NULL, then the value of "add" is
	 *	   appended to "buff"
	 */
	if (length)
	{
		/* -1 is left justify, 1 is right justify */
		justify = (length > 0) ? -1 : 1;
		if (length < 0)
			length = -length;
		pad = get_int_var(PAD_CHAR_VAR);
		add = free_me = fix_string_width(add, justify, pad, length, 1);
	}
	if (quote_em && add)	/* add shouldnt ever be NULL, though! */
	{
		/* Don't pass retval from "alloca" as func arg, use local. */
		size_t	bufsiz = strlen(add) * 2 + 2;
		buffer = alloca(bufsiz);
		escape_chars(add, quote_em, buffer, bufsiz);
		add = buffer;
	}

	if (buff)
		malloc_strcat(buff, add);
	if (free_me)
		new_free(&free_me);
	return;
}

static void	do_alias_string (const char *unused, const char *input)
{
	malloc_strcpy(&alias_string, input);
}

