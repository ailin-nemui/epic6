/*
 * What we're doing here is defining a unique string that describes what
 * compile-time options are in use.  The string is then accessible though
 * a special builtin function.  The point is to allow script writers to
 * know what has been enabled/disabled so they don't try to use a
 * feature that in't available.  As such, only #define's that really
 * affect scripting or direct user interaction are included in the
 * string.  Each option is assigned a unique letter, all of which are
 * concatenated together to form a strig that looks like an irc server's
 * version string.  Some of the options are assigned to non-obvious letters
 * since the string has to be case insensitive.
 */
 
const char compile_time_options[] = {
 
/* Implied on hooks -- because loadformats requires it */
					'h',
#ifdef HAVE_PCRE2
					'p',
#endif
#ifdef HAVE_LIBARCHIVE
					'r',
#endif

					'\0'
};
