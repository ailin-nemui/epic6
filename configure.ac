dnl ----------------------------------------------------------
4nl configure.in for ircii, 2.3.  copyright (c) matthew green, 1993,
dnl except those bits attributed otherwise.
dnl thanks to the screen 3.3 configure.in for giving me examples to
dnl work from and steal ;)
dnl

AC_INIT([epic6],[0])
AC_CONFIG_SRCDIR([source/irc.c])
AC_CONFIG_HEADERS([include/defs.h])
AC_LANG(C)

dnl ----------------------------------------------------------
dnl
dnl grok the version number - from source/irc.c
dnl
VERSION=`sed -n -e 's/;$//' -e '/const unsigned long	commit_id = /s///p' -e '/const unsigned long	commit_id/q' < $srcdir/source/irc.c` 
AC_SUBST([VERSION])
echo this is EPIC6 commit $VERSION
echo

AC_PROG_CC
AC_PROG_CPP

dnl ----------------------------------------------------------
trydirs="/usr/local /usr/pkg /opt"
AC_MSG_CHECKING([for an extra library directory])
AC_ARG_WITH([localdir],
[  --with-localdir=/usr/local      An extra directory to look for stuff.],
[ if test "x$withval" = "xno"; then
      trydirs=""
   elif test "x$withval" = "x" || test "x$withval" = "xyes"; then
      true;
   else
      trydirs="$withval"
   fi
],[ ])

with_extra_libdir=""
for dir in $trydirs; do
    if test -d $dir ; then
       with_extra_libdir=$dir
       break
    fi
done

if test "x$with_extra_libdir" != "x" ; then
      localdir=$with_extra_libdir
      LIBS="-L$localdir/lib $LIBS"
      CFLAGS="-I$localdir/include/ $CFLAGS"
      AC_MSG_RESULT([$localdir])
else
      AC_MSG_RESULT([not found])
fi

dnl ----------------------------------------------------------
dnl ----------------------------------------------------------
dnl
dnl System specific checks
dnl
dnl You'll notice there are far fewer than there used to be.
dnl This is intentional.  I no longer go out of my way to keep
dnl support for systems that nobody uses any more.  If I have 
dnl over-reached and unsupported a platform you are using, please
dnl reach out to me so I can re-support it!
dnl
dnl ----------------------------------------------------------
dnl ----------------------------------------------------------

dnl ----------------------------------------------------------
dnl
dnl Check for libraries before we check for functions!
dnl

dnl Check to see if libarchive actually works
dnl XXX I don't understand why this is even necessary.
AC_ARG_WITH([libarchive], [  --without-libarchive            Disable libarchive support.],
	[], [with_libarchive=maybe])
if test "x$with_libarchive" != "xno" ; then
	have_libarchive=""
	orig_LIBS="$LIBS"
	AC_CHECK_LIB(archive, archive_read_new, [LIBS="$LIBS -larchive"])
	AC_MSG_CHECKING(whether libarchive works the way I expect)
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[
			#include <archive.h>
		]], [[
			struct archive *x; 
			const char *s = "bogus.zip";
			x = archive_read_new();
			archive_read_support_format_all(x);
			archive_read_open_file(x, s, 10240);
		]])],
		[have_libarchive="yes"])

	if test "x$have_libarchive" = "x"; then
		LIBS="$orig_LIBS"
		AC_MSG_RESULT(no)
		if test "x$with_libarchive" = "xyes" ; then
			AC_MSG_ERROR([--with-libarchive was specified but libarchive could not be found.  Please install libarchive, or do not specify --with-libarchive.])
                fi
	else
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_LIBARCHIVE], 1, [Define this if you have a working libarchive])
	fi
fi

dnl -----
AC_ARG_WITH(ssl,
[  --with-ssl[=PATH]                 Help me find your SSL installation (DIR is OpenSSL's install dir).],[
	if test -z "$withval"; then
		with_ssl="yes"
	fi
])

AC_MSG_CHECKING(whether to include SSL support)
if test "x$with_ssl" = "xno"; then
	AC_MSG_RESULT(no)
	AC_MSG_ERROR([You cannot explicitly turn off OpenSSL support any more.  This error is intentional.  Please remove --without-ssl and try again.])
else
	saved_LIBS="$LIBS"
	saved_CFLAGS="$CFLAGS"

	# First, let's see if the user overrode the openssl location
	ssldir="$with_ssl"
	if test "x$ssldir" != "x" -a "x$ssldir" != "xyes"; then
	    if test -f "$ssldir/include/openssl/ssl.h" && 
			test -f "$ssldir/lib/libssl.a"; then
		LIBS="$saved_LIBS -L$ssldir/lib -lssl -lcrypto"
	        CFLAGS="$saved_CFLAGS -I$ssldir/include"
		AC_LINK_IFELSE([AC_LANG_PROGRAM([[
				#include <openssl/ssl.h>
			]], [[
				SSL_library_init();
			]])],
			[have_ssl="yes"])
	    fi

	    if test "x$have_ssl" != "xyes"; then
		AC_MSG_RESULT([no])	
		AC_MSG_ERROR([I could not find the OpenSSL installed at $with_ssl you asked me to use with --with-ssl=$with_ssl.  Please ensure that directory contains the files 'include/openssl/ssl.h' and 'lib/libssl.a'.  If you are unsure, remove --with-ssl and just let me auto-detect it.])
	    fi
	else
		# Second, let's see if it just works by default
		LIBS="$saved_LIBS -lssl -lcrypto"
		CFLAGS="$saved_CFLAGS"
		AC_LINK_IFELSE([AC_LANG_PROGRAM([[
				#include <openssl/ssl.h>
			]], [[
				SSL_library_init();
			]])],
			[have_ssl="yes"])

		# Otherwise, go hunting for it.
		if test "x$have_ssl" = "x" ; then
		    for ssldir in $localdir/ssl $localdir/openssl; do
			if test "x$have_ssl" != "x"; then
				break;
			fi

			if test "x$ssldir" = "x"; then
				continue;
			fi

			if test -f "$ssldir/include/openssl/ssl.h" &&
				test -f "$ssldir/lib/libssl.a"; then
			    CFLAGS="$saved_CFLAGS -I$ssldir/include"
			    LIBS="$saved_LIBS -L$ssldir/lib -lssl -lcrypto"
			    AC_LINK_IFELSE([AC_LANG_PROGRAM([[
					#include <openssl/ssl.h>
				]], [[
					SSL_library_init();
				]])],
				[have_ssl="yes"])
			fi
		    done
		fi
	fi

	# If we found it somewhere, great!  Otherwise, revert.
	if test ! -n "$have_ssl"; then
		LIBS="$saved_LIBS"
		CFLAGS="$saved_CFLAGS"
		AC_MSG_RESULT([no])
		if test "x$with_ssl" != "x"; then AC_MSG_ERROR([You requested SSL support, but OpenSSL was not found. Please supply a pathname to OpenSSL])
		fi
		AC_MSG_ERROR([I was unable to find OpenSSL.  If it is not installed, please install it.  If it is installed, please help me find it using --with-ssl=/usr/local or similar.])
	else
		AC_DEFINE([HAVE_SSL], 1, [Define this if you have a working ssl])
		AC_MSG_RESULT([yes])
	fi
fi

dnl Look for common libraries that have common functions
AC_CHECK_LIB(m, pow, LIBS="$LIBS -lm",)
AC_CHECK_LIB(rt, clock_gettime, LIBS="$LIBS -lrt",)
AC_CHECK_LIB(sqlite, sqlite_open, LIBS="$LIBS -lsqlite",)
AC_CHECK_LIB(regex, regcomp, LIBS="$LIBS -lregex",)
AC_CHECK_LIB(ncurses, setupterm, LIBS="-lncurses $LIBS",)

dnl ----------------------------------------------------------
dnl
dnl Checking for headers, functions, and a type declarations
dnl sigh.  I used to check for sys/fcntl.h
dnl

AC_CHECK_HEADERS(fcntl.h inttypes.h locale.h math.h ndbm.h netdb.h regex.h stdargs.h stddef.h stdint.h sys/cdefs.h sys/file.h sys/filio.h sys/select.h sys/syslimits.h sys/time.h sys/un.h sys/param.h term.h termios.h sys/termios.h xlocale.h,)

AC_CHECK_FUNC(clock_gettime, AC_DEFINE([HAVE_CLOCK_GETTIME], 1, [Define if you have clock_gettime()]),)
AC_CHECK_FUNC(gettimeofday, AC_DEFINE([HAVE_GETTIMEOFDAY], 1, [Define if you have gettimeofday()]),)
AC_CHECK_FUNC(fpathconf, AC_DEFINE([HAVE_FPATHCONF], 1, [Define if you have fpathconf()] ),)
AC_CHECK_FUNC(getlogin, AC_DEFINE([HAVE_GETLOGIN], 1, [Define if you have getlogin()]),)
AC_CHECK_FUNC(getpgid, AC_DEFINE([HAVE_GETPGID], 1, [Define if you have getpgid()]),)
AC_CHECK_FUNC(killpg, AC_DEFINE([HAVE_KILLPG], 1, [Define if you have killpg()]),)
AC_CHECK_FUNC(strptime, AC_DEFINE([HAVE_STRPTIME], 1, [Define if you have strptime()]),)
AC_CHECK_FUNC(sysconf, AC_DEFINE([HAVE_SYSCONF], 1, [Define if you have sysconf()]),)
AC_CHECK_FUNC(unveil, AC_DEFINE([HAVE_UNVEIL], 1, [Define if you have unveil()]),)
AC_CHECK_FUNC(pledge, AC_DEFINE([HAVE_PLEDGE], 1, [Define if you have pledge()]),)
AC_CHECK_FUNC(newlocale, AC_DEFINE([HAVE_NEWLOCALE], 1, [Define if you have newlocale()]),)

dnl -- Because someone is trying to use CentOS 5.11...
AC_MSG_CHECKING(whether you are using a gnu libc that is so old it needs a hack to use newlocale)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
	#include <locale.h>
	#include <xlocale.h>
	#include <stdlib.h>
]],[[
	locale_t utf8_locale;
	utf8_locale = newlocale(LC_ALL_MASK, "en_US.UTF-8", 0);
	return 0;
]])],
   [ AC_MSG_RESULT(no, good) ],
   [ AC_MSG_RESULT(yes, ugh.)
     AC_MSG_CHECKING(whether we can fix that with _GNU_SOURCE)
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
	#define _GNU_SOURCE
	#include <locale.h>
	#include <xlocale.h>
	#include <stdlib.h>
     ]],[[
	locale_t utf8_locale;
	utf8_locale = newlocale(LC_ALL_MASK, "en_US.UTF-8", 0);
	return 0;
     ]])],
     [ AC_MSG_RESULT(yes) 
       AC_DEFINE(NEWLOCALE_REQUIRES__GNU_SOURCE, 1, [Define this if newlocale() requires #define __GNU_SOURCE to work]) ],
     [ AC_MSG_RESULT(no. well...)
       AC_MSG_CHECKING(whether we can fix that with _XOPEN_SOURCE 700)
       AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
	  #include <stdlib.h>
	  #define _XOPEN_SOURCE 700
	  #include <locale.h>
       ]],[[
	  locale_t utf8_locale;
	  utf8_locale = newlocale(LC_ALL_MASK, "en_US.UTF-8", 0);
	  return 0;
       ]])],
       [ AC_MSG_RESULT(yes.  Yay!) 
         AC_DEFINE(NEWLOCALE_REQUIRES__XOPEN_SOURCE_700, 1, [Define this if newlocale() requires #define __XOPEN_SOURCE 700 to work]) ],
         [ AC_MSG_RESULT(no.  bummer for you) 
           AC_DEFINE(NEWLOCALE_DOESNT_WORK, 1, [Define this if newlocale() doesn't work the way I expect]) ], :), ], :)
     ]
)

AC_FUNC_ALLOCA
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UID_T
AC_TYPE_INT8_T
AC_TYPE_UINT8_T
AC_TYPE_INT16_T
AC_TYPE_UINT16_T
AC_TYPE_INT32_T
AC_TYPE_UINT32_T
AC_TYPE_INT64_T
AC_TYPE_UINT64_T
AC_TYPE_INTMAX_T
AC_TYPE_UINTMAX_T
AC_TYPE_INTPTR_T

AC_CHECK_TYPE([socklen_t], 
	[AC_DEFINE([HAVE_SOCKLEN_T], 1, [Define if you have socklen_t])], ,
	[[ #include <sys/socket.h> ]])

AC_CHECK_TYPE([struct linger], ,
	[AC_DEFINE([NO_STRUCT_LINGER], 1, [Define if you do not have struct linger])],
	[[ #include <sys/socket.h> ]])

dnl ----------------------------------------------------------
dnl
dnl It makes a big difference if your system's (struct sockaddr)
dnl structures look like this:
dnl
dnl struct sockaddr {
dnl     u_char     sa_len;
dnl     u_char     sa_family;
dnl     char       sa_data[14];
dnl };
dnl
dnl 	or this
dnl
dnl struct sockaddr {
dnl     u_short    sa_family;
dnl     char       sa_data[14];
dnl };
dnl
dnl so check for that here.
dnl
dnl -----------------------------------------------------------
AC_MSG_CHECKING([to see if your struct sockaddr includes a length field])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <stdlib.h>
]],[[
	struct sockaddr sa;
	sa.sa_len = 0;
	return 0;
]])],
  	[ AC_DEFINE([HAVE_SA_LEN], 1, [Define this if your struct sockaddr has a sa_len field])
	  AC_MSG_RESULT([yes]) ],
  	[ AC_MSG_RESULT([no]) ], :)

dnl ----------------------------------------------------------
AC_CHECK_TYPE([struct sockaddr_storage], 
	[AC_DEFINE([HAVE_STRUCT_SOCKADDR_STORAGE], 1, [Define if you have struct sockaddr_storage])], ,
	[[ #include <sys/socket.h> ]])
AC_CHECK_TYPE([struct sockaddr_in6], 
	[AC_DEFINE([HAVE_STRUCT_SOCKADDR_IN6], 1, [Define if you have struct sockaddr_in6])], ,
	[[ #include <netinet/in.h> ]])
AC_CHECK_TYPE([struct addrinfo], 
	[AC_DEFINE([HAVE_STRUCT_ADDRINFO], 1, [Define if you have struct addrinfo])], ,
	[[ #include <netdb.h> ]])

dnl ----------------------------------------------------------
dnl
dnl Some implementations of getaddrinfo (*cough*KAME*cough*) don't 
dnl support the AF_UNIX protocol family.  For these partially incomplete 
dnl implementations, we must add our own support in a wrapper.
dnl

AC_MSG_CHECKING([to see if your getaddrinfo supports AF_UNIX])
AC_RUN_IFELSE([AC_LANG_PROGRAM([[
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netdb.h>
	#include <sys/un.h>
	#include <stdlib.h>
	#include <string.h>
]],[[
	struct addrinfo hints;
	struct addrinfo *results;
	int	retval;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = 0;
	hints.ai_family = AF_UNIX;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;

	retval = getaddrinfo(0, "/tmp/.ircd/6667", &hints, &results);
	if (retval != 0)
		return 1;
	else
		return 0;
]])],
  AC_MSG_RESULT([yes]),
  AC_DEFINE([GETADDRINFO_DOES_NOT_DO_AF_UNIX], 1, [Define this if your getaddrinfo() does not support AF_UNIX])
  AC_MSG_RESULT([no. ugh]), :)

dnl ----------------------------------------------------------
dnl
dnl Iconv support?
dnl
AC_ARG_WITH(iconv,
[  --with-iconv[=PATH]               Include iconv support (PATH is (eg) /usr/local).],[
	if test -z "$withval"; then
		with_iconv="/usr"
	fi
])

AC_MSG_CHECKING(whether to include iconv support)
if test "x$with_iconv" = "xno"; then
	AC_MSG_RESULT([Iconv support is required; I'm afraid I must insist.])
	AC_MSG_CHECKING([whether to include iconv support])
	with_iconv=yes
fi

	saved_LIBS="$LIBS"
	saved_CFLAGS="$CFLAGS"

	dnl First try to see if it's in libc
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[
		#include <iconv.h>
	]],[[
		iconv_open(0, 0);
	]])], 
		have_iconv="yes")

	dnl If it's not in libc, look in -liconv
	if test "x$have_iconv" = "x"; then
	   LIBS="$saved_LIBS -liconv"
	   CFLAGS="$saved_CFLAGS"
	   AC_LINK_IFELSE([AC_LANG_PROGRAM([[
		#include <iconv.h>
	   ]],[[
		iconv_open(0, 0);
	   ]])], 
		have_iconv="yes")
	fi

	dnl whatever.
	if test "x$with_iconv" = "xyes" ; then
		unset with_iconv
	fi

	dnl Otherwise, go hunting for it.
	if test "x$have_iconv" = "x"; then
	    for iconvdir in "$with_iconv" "$prefix" $localdir /usr/local /usr/pkg /opt /usr/opt; do
		if test "x$have_iconv" != "x"; then
			break
		fi

		if test "x$iconvdir" = "x"; then
			continue
		fi

dnl		if test -f "$iconvdir/include/iconv.h" &&
dnl			test -f "$iconvdir/lib/libiconv.a"; then
		if test -f "$iconvdir/include/iconv.h" ; then
		    CFLAGS="$saved_CFLAGS -I$iconvdir/include"
		    LIBS="$saved_LIBS -L$iconvdir/lib -liconv"
		    AC_LINK_IFELSE([AC_LANG_PROGRAM([[
			#include <iconv.h>
	   	    ]],[[
			iconv_open(0, 0);
	   	    ]])], 
			have_iconv="yes")
		fi

		if test "x$with_iconv" != "x"; then
			break
		fi
	    done
	fi

# If we found it somewhere, great!  Otherwise, revert.
if test "x$have_iconv" = "x"; then
	LIBS="$saved_LIBS"
	CFLAGS="$saved_CFLAGS"
	AC_MSG_RESULT(looked but did not find it)
	if test "x$with_iconv" != "x" ; then
		AC_MSG_ERROR([You specified --with-iconv=$with_iconv but that directory did not contain files include/iconv.h and lib/iconv.a.  Either specify a different directory or let me auto-detect it for you])
	else
		AC_MSG_ERROR([Iconv support is required.  I could not find your system's iconv support.  If you have iconv installed, please re-run configure with the --with-iconv=/path/to/dir option where include/iconv.h and lib/iconv.a exist.])
	fi
else
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_ICONV], 1, [Define this if you have a useful iconv()])
fi

dnl #sigh#
dnl AC_CHECK_FUNC(iconv_open, AC_DEFINE([HAVE_ICONV], 1, [Define this if you have a useful iconv())],)

dnl ---------------------------------------------------------
dnl
dnl I am sorry, but I'm not going to let you turn off wserv
dnl
WSERV_BIN="wserv4"
WSERV_INSTALL="installwserv4"

dnl ----------------------------------------------------------
dnl
dnl Valgrind assistance support?
dnl
AC_MSG_CHECKING(whether to include Valgrind Memcheck support)
AC_ARG_WITH(valgrind,
[  --with-valgrind                 Include support for Valgrind Memcheck ],
[],
[with_valgrind=no])
if test "x$with_valgrind" != "xno"; then
	AC_MSG_RESULT(yes)
	AC_CHECK_HEADERS([valgrind/memcheck.h])
else
	AC_MSG_RESULT(no)
fi

dnl ----------------------------------------------------------
dnl
dnl clang 15 sanitizing support?
dnl
AC_MSG_CHECKING(whether to include clang15 sanitizing support)
AC_ARG_WITH(clang-sanitizing,
[  --with-clang-sanitizing         Include support for clang sanitizing ],
[],
[with_clang_sanitizing=no])
if test "x$with_clang_sanitizing" != "xno"; then
	AC_MSG_RESULT(yes)
	AC_MSG_CHECKING(whether your compiler supports -fsanitize)
	old_cflags="$CFLAGS"
	CFLAGS="$CFLAGS -fsanitize=undefined,implicit-integer-truncation,implicit-integer-arithmetic-value-change,implicit-conversion,integer,nullability,object-size,local-bounds"
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],[[ return 0; ]])],
		[AC_MSG_RESULT(yes)],
		[AC_MSG_RESULT(no)
		CFLAGS="$old_cflags"])
else
	AC_MSG_RESULT(no)
fi


dnl ----------------------------------------------------------
dnl
dnl Python support?
dnl
AC_ARG_WITH([python],
[  --with-python[=PATH_TO_PYTHON_CONFIG_EXE]   Compile with Python support.],
       [], [with_python=maybe])

with_python_errormsg="--with-python was specified but I could not locate python3-config. Please try specifying --with-path=/path/to/python3-config or --without-python."
AC_MSG_CHECKING([whether to support Python])
if test "x$with_python" = "xno" ; then
	AC_MSG_CHECKING(whether to support Python)
	AC_MSG_RESULT(no)
else
	AC_MSG_RESULT(yes)
	if test "x$with_python" = "xyes" -o "x$with_python" = "xmaybe" ; then
		python_cfg_exe=python3-config
	else
		python_cfg_exe="$with_python"
	fi
	dnl Support --with-python being passed a full path instead of just an executable name.
	if test -x "$python_cfg_exe" ; then
		PYTHON_CFG_EXE=yes
	else
		AC_CHECK_PROG([PYTHON_CFG_EXE], [$python_cfg_exe], [yes], [no])
	fi
	if test "x$PYTHON_CFG_EXE" = xno ; then
		if test "x$with_python" != "xmaybe" ; then
			AC_MSG_ERROR([$with_python_errormsg])
		fi
	else
	    dnl ---
            dnl First, we have to determine if this is 3.7 or lower; or 3.8 or higher
	    dnl The 3.8 or higher versions require --embed and 3.7 or lower versions
	    dnl forbid it.  What fun!
	    dnl
	    if $python_cfg_exe --cflags --embed > /dev/null ; then
		use_embed="--embed"
	    else
		use_embed=""
	    fi

	    dnl ---- 
	    dnl Look first for Python 3
            PYTHON_CFLAGS=`$python_cfg_exe --cflags $use_embed`
            PYTHON_LDFLAGS=`$python_cfg_exe --ldflags $use_embed`
	    PYTHON_O="python.o"

	    AC_MSG_CHECKING(whether embedded Python works the way I expect)
	    have_embedded_python="no"
	    old_CFLAGS="$CFLAGS"
	    old_LIBS="$LIBS"
	    CFLAGS="$CFLAGS $PYTHON_CFLAGS"
	    LIBS="$LIBS $PYTHON_LDFLAGS"
	    AC_LINK_IFELSE([AC_LANG_PROGRAM([[
		#include <Python.h>
		static  PyObject *      epic_null (PyObject *self, PyObject *args)
		{
			char *  str;
			if (!PyArg_ParseTuple(args, "s", &str)) {
				return PyLong_FromLong(-1);
			}
			Py_INCREF(Py_None);	/* This test is important! */
			Py_XDECREF(Py_None);
			return Py_BuildValue("s", "");
		}

		static  PyMethodDef     epicMethods[] = {
			{ "null",       epic_null,      METH_VARARGS,   "null" },
			{ NULL,         NULL,           0,              NULL }
		};

		static  PyModuleDef     epicModule = {
			PyModuleDef_HEAD_INIT,  
			"_epic",        NULL,           -1,             epicMethods,
			NULL,           NULL,           NULL,           NULL
		};

		static  PyObject *      PyInit_epic (void)
		{
			return PyModule_Create(&epicModule);
		}
	    ]], [[
		PyImport_AppendInittab("_epic", &PyInit_epic);
		Py_Initialize();
	    ]])],
		have_embedded_python="yes")

	    if test $have_embedded_python = "yes" ; then
		AC_MSG_RESULT(yes)
		AC_DEFINE([PYTHON_O], 1, [Define this add stuff....])
		AC_DEFINE([HAVE_PYTHON], 1, [Define this to support python])
		AC_DEFINE([PYTHON_CFLAGS], 1, [Stuff to add to CFLAGS to support python])
		AC_DEFINE([PYTHON_LDFLAGS], 1, [Stuff to add to LDFLAGS to support python])
            else
		if test "x$with_python" != "xmaybe" ; then
			AC_MSG_ERROR([$with_python_errormsg])
		fi
	        PYTHON_O=""
		HAVE_PYTHON=""
		PYTHON_CFLAGS=""
		PYTHON_LDFLAGS=""
		AC_MSG_RESULT(no, sorry)
            fi

	    CFLAGS="$old_CFLAGS"
	    LIBS="$old_LIBS"
	fi
fi

dnl ----------------------------------------------------------
dnl ----------------------------------------------------------
dnl
dnl closing stuff
dnl
dnl ----------------------------------------------------------
dnl ----------------------------------------------------------


dnl ----------------------------------------------------------
dnl 
dnl set some last minute sanity defaults
dnl

if test -z "$CFLAGS"; then CFLAGS="-g -O"; fi
if test -z "$LDFLAGS"; then LDFLAGS= ; fi
if test -z "$PYTHON_O"; then PYTHON_OH= ; fi
if test -z "$WSERV_BIN"; then WSERV_BIN= ; fi
if test -z "$WSERV_INSTALL"; then WSERV_INSTALL= ; fi
if test -z "$bindir"; then bindir=\${prefix}/bin; fi
if test -z "$libdir"; then libdir=\${prefix}/lib; fi
if test -z "$irclibdir"; then irclibdir=\${libdir}/irc; fi
if test -z "$libexecdir"; then libexecdir=\${prefix}/libexec; fi

epic6=`echo "epic6" | sed -e "$program_transform_name"`
if test "$program_prefix" = "NONE" ; then
	program_prefix=
fi

AC_MSG_CHECKING(whether your compiler supports -fno-strict-aliasing)
old_cflags="$CFLAGS"
CFLAGS="$CFLAGS -fno-strict-aliasing"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM( [[]],[[ return 0; ]])],
	[AC_MSG_RESULT(yes)],
        [AC_MSG_RESULT(no)])

AC_MSG_CHECKING(whether your compiler supports -fwrapv)
old_cflags="$CFLAGS"
CFLAGS="$CFLAGS -fwrapv"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM( [[]],[[ return 0; ]])],
	[AC_MSG_RESULT(yes)],
        [AC_MSG_RESULT(no)])

AC_MSG_CHECKING(whether your compiler supports -fno-delete-null-pointer-checks)
old_cflags="$CFLAGS"
CFLAGS="$CFLAGS -fno-delete-null-pointer-checks"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM( [[]],[[ return 0; ]])],
	[AC_MSG_RESULT(yes)],
        [AC_MSG_RESULT(no)])

AC_MSG_CHECKING(whether your compiler supports __attribute__((fallthrough)))
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ 
	]],[[
		int x = 1; 
		switch (x) { 
			case 1: 
				x++; 
				__attribute__((fallthrough));
			case 0: 
				x++; 
		}
	]])],
        [have_attribute_fallthrough=1],
        [have_attribute_fallthrough=0])

if test "x$have_attribute_fallthrough" = "x1" ; then
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_ATTRIBUTE_FALLTHROUGH], 1, [Define if you can use __attribute__((fallthrough))])
else
	AC_MSG_RESULT(no)
fi

AC_MSG_CHECKING(whether your compiler supports __attribute__((may_alias)))
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ 
		typedef int __attribute__ ((may_alias)) int_;
	]],[[
		long a = 0x12345678;
		int_ *b = (int_ *)&a;
	]])],
        [have_attribute_may_alias=1],
        [have_attribute_may_alias=0])

if test "x$have_attribute_may_alias" = "x1" ; then
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_ATTRIBUTE_MAY_ALIAS], 1, [Define if you can use __attribute__((may_alias))])
else
	AC_MSG_RESULT(no)
fi

AC_MSG_CHECKING(whether your compiler supports extra warning flags)
old_cflags="$CFLAGS"
CFLAGS="$CFLAGS -Wwrite-strings -W -Wall -Wextra"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],[[ return 0; ]])],
	[AC_MSG_RESULT(yes)],
        [AC_MSG_RESULT(no)
	CFLAGS="$old_cflags"])

AC_MSG_CHECKING(whether your compiler supports -Wno-unused-parameter)
old_cflags="$CFLAGS"
CFLAGS="$CFLAGS -Wno-unused-parameter"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],[[ return 0; ]])],
	[AC_MSG_RESULT(yes)],
        [AC_MSG_RESULT(no)
	CFLAGS="$old_cflags"])

AC_MSG_CHECKING(whether your compiler supports -Wno-unused-function)
old_cflags="$CFLAGS"
CFLAGS="$CFLAGS -Wno-unused-function"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],[[ return 0; ]])],
	[AC_MSG_RESULT(yes)],
        [AC_MSG_RESULT(no)
	CFLAGS="$old_cflags"])

AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(PYTHON_O)
AC_SUBST(PYTHON_CFLAGS)
AC_SUBST(PYTHON_LDFLAGS)
AC_SUBST(WSERV_BIN)
AC_SUBST(WSERV_INSTALL)
AC_SUBST(bindir)
AC_SUBST(irclibdir)
AC_SUBST(libexecdir)
AC_SUBST(srcdir)
AC_SUBST(includedir)
AC_SUBST(epic6)
AC_SUBST(program_transform_name)
AC_SUBST(program_prefix)
AC_SUBST(mandir)
AC_SUBST(datadir)
AC_SUBST(datarootdir)

AC_CONFIG_FILES([Makefile source/Makefile source/info.c.sh])
AC_OUTPUT

dnl ----------------------------------------------------------
dnl
dnl Commencement
dnl

echo
echo "If your build fails, make sure you have all the necessary "
echo "'*-devel' type packages installed.  This is particularly "
echo "important for ncurses-devel"
echo
echo "You may now do 'make' and 'make install'"
echo 

