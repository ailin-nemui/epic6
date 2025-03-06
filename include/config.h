/* 
 * config.h:
 *
 * This file provides default values for runtime variables that must have them.
 * They are used to boot up and load your ~/.epicrc where you can change them
 *
 *   Don't fret if you don't like these values -- 
 *   you can override all of them at runtime.
 *   They're just here so the client can bootstrap itself.
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

/* 
 * If you're looking for where to specify the default servers, the client
 * doesn't do that any more.  See the script/servers script.  
 * If the user does not provide a server to connect to, the client will 
 * start up in disconnected mode and let the user take the initiative.
 */

/*
 * The /LOAD path is now generated at runtime, rather than at compile time.
 * This is to allow you to change IRCLIB and have its script library be
 * resepected without having to change IRCPATH as well.  This is a printf
 * format of what the default load path is to be.  The %s format indicates
 * the runtime IRCLIB value.  This value is only used at startup time.
 */
#define DEFAULT_IRCPATH "~/.epic:~/.irc:%s/script:."

/*
 * These are the compiled-in default values for /SET variables.
 * I cannot emphasize strongly enough that all of these can be changed
 * at runtime in your ~/.epicrc, so please don't feel you need to change
 * them here.
 *
 * - A /set value that is a string should be a C string or NULL
 * - A /set value that is a boolean should be 0 for OFF and 1 for ON
 * - A /set value that is an integer should be a C integer.
 * If you don't know, don't touch.
 */
#define DEFAULT_ACCEPT_INVALID_SSL_CERT 1
#define DEFAULT_ALLOW_C1_CHARS 0
#define DEFAULT_ALWAYS_SPLIT_BIGGEST 1
#define DEFAULT_AUTOMARGIN_OVERRIDE 0
#define DEFAULT_BANNER "***"
#define DEFAULT_BANNER_EXPAND 0
#define DEFAULT_BEEP 1
#define DEFAULT_BLANK_LINE_INDICATOR NULL
#define DEFAULT_BROKEN_AIXTERM 0
#define DEFAULT_CHANNEL_NAME_WIDTH 0
#define DEFAULT_CLOCK 1
#define DEFAULT_CLOCK_24HOUR 0
#define DEFAULT_CLOCK_FORMAT NULL
#define DEFAULT_CLOCK_INTERVAL 60
#define DEFAULT_CMDCHARS "/"
#define DEFAULT_COMMENT_HACK 1
#define DEFAULT_CONTINUED_LINE "+"
#define DEFAULT_CURRENT_WINDOW_LEVEL NULL
#define DEFAULT_DISPATCH_UNKNOWN_COMMANDS 0
#define DEFAULT_DISPLAY 1
#define DEFAULT_FIRST_LINE NULL
#define DEFAULT_FLOATING_POINT_MATH 0
#define DEFAULT_FLOATING_POINT_PRECISION 16
#define DEFAULT_HIDE_PRIVATE_CHANNELS 0
#define DEFAULT_HOLD_SLIDER 100
#define DEFAULT_INDENT 0
#define DEFAULT_INPUT_INDICATOR_LEFT "+ "
#define DEFAULT_INPUT_INDICATOR_RIGHT " +"
#define DEFAULT_INPUT_PROMPT "> "
#define DEFAULT_INSERT_MODE 1
#define DEFAULT_KEY_INTERVAL 1000
#define DEFAULT_LASTLOG 256
#define DEFAULT_LASTLOG_LEVEL "ALL"
#define DEFAULT_LASTLOG_REWRITE NULL
#define DEFAULT_LOG 0
#define DEFAULT_LOGFILE "irc.log"
#define DEFAULT_METRIC_TIME 0
#define DEFAULT_MODE_STRIPPER 0
#define DEFAULT_MOUSE 0
#define DEFAULT_NEW_SERVER_LASTLOG_LEVEL "ALL"
#define DEFAULT_NOTIFY_LEVEL "ALL"
#define DEFAULT_NOTIFY_ON_TERMINATION 1
#define DEFAULT_NO_CONTROL_LOG 0
#define DEFAULT_NO_FAIL_DISCONNECT 0
#define DEFAULT_OLD_SERVER_LASTLOG_LEVEL "NONE"
#define DEFAULT_PAD_CHAR ' '
#define DEFAULT_QUIT_MESSAGE "ircII %s -- What next?"
#define DEFAULT_SCREEN_OPTIONS NULL
#define DEFAULT_SCROLLBACK 256
#define DEFAULT_SCROLLBACK_RATIO 50
#define DEFAULT_SCROLL_LINES 1
#define DEFAULT_SHELL "/bin/sh"
#define DEFAULT_SHELL_FLAGS "-c"
#define DEFAULT_SHELL_LIMIT 0
#define DEFAULT_SHOW_CHANNEL_NAMES 1
#define DEFAULT_SHOW_NUMERICS 0
#define	DEFAULT_SHOW_STATUS_ALL 0
#define DEFAULT_SSL_CIPHERS NULL
#define DEFAULT_SSL_ROOT_CERTS_LOCATION NULL
#define DEFAULT_STATUS_AWAY " (Away)"
#define DEFAULT_STATUS_CHANNEL " %C"
#define DEFAULT_STATUS_CHANOP "@"
#define DEFAULT_STATUS_CLOCK " %T"
#define DEFAULT_STATUS_FORMAT REV_TOG_STR "%T [%R] %*%=%@%N%#%S%{1}H%H%B%Q%A%C%+%I%O%M%F%L %D %U %W"
#define DEFAULT_STATUS_FORMAT1 REV_TOG_STR "%T [%R] %*%=%@%N%#%S%{1}H%H%B%Q%A%C%+%I%O%M%F%L %U "
#define DEFAULT_STATUS_FORMAT2 REV_TOG_STR "%W %X %Y %Z "
#define DEFAULT_STATUS_HALFOP "%"
#define DEFAULT_STATUS_HOLD " Held: "
#define DEFAULT_STATUS_HOLD_LINES "%B"
#define DEFAULT_STATUS_HOLDMODE " (Hold)"
#define DEFAULT_STATUS_INSERT ""
#define DEFAULT_STATUS_MAIL " (Mail: %M)"
#define DEFAULT_STATUS_MODE " (+%+)"
#define DEFAULT_STATUS_NICKNAME "%N"
#define	DEFAULT_STATUS_NOSWAP "(NOSWAP)"
#define	DEFAULT_STATUS_NOTIFY " (W: %F)"
#define DEFAULT_STATUS_NO_REPEAT 0
#define DEFAULT_STATUS_OPER "*"
#define DEFAULT_STATUS_OVERWRITE " (Overwrite)"
#define DEFAULT_STATUS_PREFIX_WHEN_CURRENT ""
#define DEFAULT_STATUS_PREFIX_WHEN_NOT_CURRENT ""
#define DEFAULT_STATUS_QUERY " (Query: %Q)"
#define DEFAULT_STATUS_SCROLLBACK " (Scroll)"
#define DEFAULT_STATUS_SEQUENCE_POINT " {{{%P}}}"
#define DEFAULT_STATUS_SERVER " (%S)"
#define DEFAULT_STATUS_SSL_OFF "*RAW*"
#define DEFAULT_STATUS_SSL_ON "*SSL*"
#define DEFAULT_STATUS_UMODE " (+%#)"
#define DEFAULT_STATUS_USER "EPIC6 -- Visit https://epicsol.org/ for help"
#define DEFAULT_STATUS_USER1 ""
#define DEFAULT_STATUS_USER2 ""
#define DEFAULT_STATUS_USER3 ""
#define DEFAULT_STATUS_USER4 ""
#define DEFAULT_STATUS_USER5 ""
#define DEFAULT_STATUS_USER6 ""
#define DEFAULT_STATUS_USER7 ""
#define DEFAULT_STATUS_USER8 ""
#define DEFAULT_STATUS_USER9 ""
#define DEFAULT_STATUS_USER10 ""
#define DEFAULT_STATUS_USER11 ""
#define DEFAULT_STATUS_USER12 ""
#define DEFAULT_STATUS_USER13 ""
#define DEFAULT_STATUS_USER14 ""
#define DEFAULT_STATUS_USER15 ""
#define DEFAULT_STATUS_USER16 ""
#define DEFAULT_STATUS_USER17 ""
#define DEFAULT_STATUS_USER18 ""
#define DEFAULT_STATUS_USER19 ""
#define DEFAULT_STATUS_USER20 ""
#define DEFAULT_STATUS_USER21 ""
#define DEFAULT_STATUS_USER22 ""
#define DEFAULT_STATUS_USER23 ""
#define DEFAULT_STATUS_USER24 ""
#define DEFAULT_STATUS_USER25 ""
#define DEFAULT_STATUS_USER26 ""
#define DEFAULT_STATUS_USER27 ""
#define DEFAULT_STATUS_USER28 ""
#define DEFAULT_STATUS_USER29 ""
#define DEFAULT_STATUS_USER30 ""
#define DEFAULT_STATUS_USER31 ""
#define DEFAULT_STATUS_USER32 ""
#define DEFAULT_STATUS_USER33 ""
#define DEFAULT_STATUS_USER34 ""
#define DEFAULT_STATUS_USER35 ""
#define DEFAULT_STATUS_USER36 ""
#define DEFAULT_STATUS_USER37 ""
#define DEFAULT_STATUS_USER38 ""
#define DEFAULT_STATUS_USER39 ""
#define DEFAULT_STATUS_VOICE "+"
#define DEFAULT_STATUS_WINDOW "^^^^^^^^"
#define DEFAULT_SUPPRESS_FROM_REMOTE_SERVER 0
#define DEFAULT_SWITCH_CHANNELS_BETWEEN_WINDOWS 1
#define DEFAULT_TERM_DOES_BRIGHT_BLINK 0
#define DEFAULT_TMUX_OPTIONS NULL
#define DEFAULT_USER_INFORMATION "EPIC6 -- Did we get lost again?"
#define DEFAULT_WORD_BREAK " \t"
#define DEFAULT_WSERV_TYPE "screen"
#define DEFAULT_XTERM "xterm"
#define DEFAULT_XTERM_OPTIONS NULL

#endif /* _CONFIG_H_ */

