/*
 * term.c -- termios and (termcap || terminfo) handlers
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1998 J. Kean Johnston, used with permission.
 * Copyright 1995, 2026 EPIC Software Labs 
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
/*
 * Ben Winslow deserves specific praise for his fine work adding 
 * terminfo support to EPIC.
 */
#define __need_term_flush__
#include "irc.h"
#include "ircaux.h"
#include "vars.h"
#include "termx.h"
#include "window.h"
#include "screen.h"
#include "output.h"
#include "newio.h"

/* 
 * Historically ircII was strictly a termcap program (still is, as of early 2026).
 * EPIC however is a terminfo program and expects your system to have terminfo.
 * Usually terminfo support is provided by libncurses. 
 *
 * Irrespective of our use of libncurses to provide terminfo support,
 * EPIC is not and does not ever intend to be a curses program.
 *
 * EPIC6 in particular expects you to be using a terminal emulator (not a physical
 * terminal) and a reasonable terminal emulator at that.  If you need to fire up
 * your retro computing science project, may I recommend epic5?
 *
 * If your terminal emulator appears to not be up to snuff, then we will just 
 * pretend it is, and maybe it will out for you (or maybe it'll just be garbage)
 */

volatile 	sig_atomic_t	need_redraw;
	 static	struct termios	oldb, 
				newb;

static	void	term_establish_last_column	(void);
static	void	term_enable_last_column		(void);
static	void	term_disable_last_column	(void);
static cJSON *	export_terminfo_to_json 	(void);
static	const char *	term_get_capstr 	(const char *name);

#define tputs_x(s)	(tputs(s, 0, term_output_char))

/* These should be per-screen variables, not global */
static	cJSON *		terminfo_json;
static	TERMINAL *	terminfo_session;


/*
 * The old code assumed termcap. termcap is almost always present, but on
 * many systems that have both termcap and terminfo, termcap is deprecated
 * and its databases often out of date. Configure will try to use terminfo
 * if at all possible. We define here a mapping between the termcap / terminfo
 * names, and where we store the information.
 */
#define CAP_TYPE_BOOL   0
#define CAP_TYPE_INT    1
#define CAP_TYPE_STR    2

typedef struct cap2info
{
	const char *	longname;
	const char *	iname;
	const char *	tname;
	int 		type;
} cap2info;

static const cap2info tcaps[] =
{
	{ "auto_left_margin",		"bw",		"bw",	CAP_TYPE_BOOL },
	{ "auto_right_margin",		"am",		"am",	CAP_TYPE_BOOL },
	{ "no_esc_ctlc",		"xsb",		"xb",	CAP_TYPE_BOOL },
	{ "ceol_standout_glitch",	"xhp",		"xs",	CAP_TYPE_BOOL },	
	{ "eat_newline_glitch",		"xenl",		"xn",	CAP_TYPE_BOOL },	
	{ "erase_overstrike",		"eo",		"eo",	CAP_TYPE_BOOL },	
	{ "generic_type",		"gn",		"gn",	CAP_TYPE_BOOL },	
	{ "hard_copy",			"hc",		"hc",	CAP_TYPE_BOOL },	
	{ "has_meta_key",		"km",		"km",	CAP_TYPE_BOOL },	
	{ "has_status_line",		"hs",		"hs",	CAP_TYPE_BOOL },	
	{ "insert_null_glitch",		"in",		"in",	CAP_TYPE_BOOL },	
	{ "memory_above",		"da",		"da",	CAP_TYPE_BOOL },	
	{ "memory_below",		"db",		"db",	CAP_TYPE_BOOL },	
	{ "move_insert_mode",		"mir",		"mi",	CAP_TYPE_BOOL },	
	{ "move_standout_mode",		"msgr",		"ms",	CAP_TYPE_BOOL },	
	{ "over_strike",		"os",		"os",	CAP_TYPE_BOOL },	
	{ "status_line_esc_ok",		"eslok",	"es",	CAP_TYPE_BOOL },	
	{ "dest_tabs_magic_smso",	"xt",		"xt",	CAP_TYPE_BOOL },	
	{ "tilde_glitch",		"hz",		"hz",	CAP_TYPE_BOOL },	
	{ "transparent_underline",	"ul",		"ul",	CAP_TYPE_BOOL },	
	{ "xon_xoff",			"xon",		"xo",	CAP_TYPE_BOOL },	
	{ "needs_xon_xoff",		"nxon",		"nx",	CAP_TYPE_BOOL },	
	{ "prtr_silent",		"mc5i",		"5i",	CAP_TYPE_BOOL },	
	{ "hard_cursor",		"chts",		"HC",	CAP_TYPE_BOOL },	
	{ "non_rev_rmcup",		"nrrmc",	"NR",	CAP_TYPE_BOOL },	
	{ "no_pad_char",		"npc",		"NP",	CAP_TYPE_BOOL },	
	{ "non_dest_scroll_region",	"ndscr",	"ND",	CAP_TYPE_BOOL },	
	{ "can_change",			"ccc",		"cc",	CAP_TYPE_BOOL },	
	{ "back_color_erase",		"bce",		"ut",	CAP_TYPE_BOOL },	
	{ "hue_lightness_saturation",	"hls",		"hl",	CAP_TYPE_BOOL },	
	{ "col_addr_glitch",		"xhpa",		"YA",	CAP_TYPE_BOOL },	
	{ "cr_cancels_micro_mode",	"crxm",		"YB",	CAP_TYPE_BOOL },	
	{ "has_print_wheel",		"daisy",	"YC",	CAP_TYPE_BOOL },	
	{ "row_addr_glitch",		"xvpa",		"YD",	CAP_TYPE_BOOL },	
	{ "semi_auto_right_margin",	"sam",		"YE",	CAP_TYPE_BOOL },	
	{ "cpi_changes_res",		"cpix",		"YF",	CAP_TYPE_BOOL },	
	{ "lpi_changes_res",		"lpix",		"YG",	CAP_TYPE_BOOL },	
	{ "columns",			"cols",		"co",	CAP_TYPE_INT },	
	{ "init_tabs",			"it",		"it",	CAP_TYPE_INT },	
	{ "lines",			"lines",	"li",	CAP_TYPE_INT },	
	{ "lines_of_memory",		"lm",		"lm",	CAP_TYPE_INT },	
	{ "magic_cookie_glitch",	"xmc",		"sg",	CAP_TYPE_INT },	
	{ "padding_baud_rate",		"pb",		"pb",	CAP_TYPE_INT },	
	{ "virtual_terminal",		"vt",		"vt",	CAP_TYPE_INT },	
	{ "width_status_line",		"wsl",		"ws",	CAP_TYPE_INT },	
	{ "num_labels",			"nlab",		"Nl",	CAP_TYPE_INT },	
	{ "label_height",		"lh",		"lh",	CAP_TYPE_INT },	
	{ "label_width",		"lw",		"lw",	CAP_TYPE_INT },	
	{ "max_attributes",		"ma",		"ma",	CAP_TYPE_INT },	
	{ "maximum_windows",		"wnum",		"MW",	CAP_TYPE_INT },	
	{ "max_colors",			"colors",	"Co",	CAP_TYPE_INT },	
	{ "max_pairs",			"pairs",	"pa",	CAP_TYPE_INT },	
	{ "no_color_video",		"ncv",		"NC",	CAP_TYPE_INT },	
	{ "buffer_capacity",		"bufsz",	"Ya",	CAP_TYPE_INT },	
	{ "dot_vert_spacing",		"spinv",	"Yb",	CAP_TYPE_INT },	
	{ "dot_horz_spacing",		"spinh",	"Yc",	CAP_TYPE_INT },	
	{ "max_micro_address",		"maddr",	"Yd",	CAP_TYPE_INT },	
	{ "max_micro_jump",		"mjump",	"Ye",	CAP_TYPE_INT },	
	{ "micro_col_size",		"mcs",		"Yf",	CAP_TYPE_INT },	
	{ "micro_line_size",		"mls",		"Yg",	CAP_TYPE_INT },	
	{ "number_of_pins",		"npins",	"Yh",	CAP_TYPE_INT },	
	{ "output_res_char",		"orc",		"Yi",	CAP_TYPE_INT },	
	{ "output_res_line",		"orl",		"Yj",	CAP_TYPE_INT },	
	{ "output_res_horz_inch",	"orhi",		"Yk",	CAP_TYPE_INT },	
	{ "output_res_vert_inch",	"orvi",		"Yl",	CAP_TYPE_INT },	
	{ "print_rate",			"cps",		"Ym",	CAP_TYPE_INT },	
	{ "wide_char_size",		"widcs",	"Yn",	CAP_TYPE_INT },	
	{ "buttons",			"btns",		"BT",	CAP_TYPE_INT },	
	{ "bit_image_entwining",	"bitwin",	"Yo",	CAP_TYPE_INT },	
	{ "bit_image_type",		"bitype",	"Yp",	CAP_TYPE_INT },	
	{ "back_tab",			"cbt",		"bt",	CAP_TYPE_STR },	
	{ "bell",			"bel",		"bl",	CAP_TYPE_STR },	
	{ "carriage_return",		"cr",		"cr",	CAP_TYPE_STR },	
	{ "change_scroll_region",	"csr",		"cs",	CAP_TYPE_STR },	
	{ "clear_all_tabs",		"tbc",		"ct",	CAP_TYPE_STR },	
	{ "clear_screen",		"clear",	"cl",	CAP_TYPE_STR },	
	{ "clr_eol",			"el",		"ce",	CAP_TYPE_STR },	
	{ "clr_eos",			"ed",		"cd",	CAP_TYPE_STR },	
	{ "column_address",		"hpa",		"ch",	CAP_TYPE_STR },	
	{ "command_character",		"cmdch",	"CC",	CAP_TYPE_STR },	
	{ "cursor_address",		"cup",		"cm",	CAP_TYPE_STR },	
	{ "cursor_down",		"cud1",		"do",	CAP_TYPE_STR },	
	{ "cursor_home",		"home",		"ho",	CAP_TYPE_STR },	
	{ "cursor_invisible",		"civis",	"vi",	CAP_TYPE_STR },	
	{ "cursor_left",		"cub1",		"le",	CAP_TYPE_STR },	
	{ "cursor_mem_address",		"mrcup",	"CM",	CAP_TYPE_STR },	
	{ "cursor_normal",		"cnorm",	"ve",	CAP_TYPE_STR },	
	{ "cursor_right",		"cuf1",		"nd",	CAP_TYPE_STR },	
	{ "cursor_to_ll",		"ll",		"ll",	CAP_TYPE_STR },	
	{ "cursor_up",			"cuu1",		"up",	CAP_TYPE_STR },	
	{ "cursor_visible",		"cvvis",	"vs",	CAP_TYPE_STR },	
	{ "delete_character",		"dch1",		"dc",	CAP_TYPE_STR },	
	{ "delete_line",		"dl1",		"dl",	CAP_TYPE_STR },	
	{ "dis_status_line",		"dsl",		"ds",	CAP_TYPE_STR },	
	{ "down_half_line",		"hd",		"hd",	CAP_TYPE_STR },	
	{ "enter_alt_charset_mode",	"smacs",	"as",	CAP_TYPE_STR },	
	{ "enter_blink_mode",		"blink",	"mb",	CAP_TYPE_STR },	
	{ "enter_bold_mode",		"bold",		"md",	CAP_TYPE_STR },	
	{ "enter_ca_mode",		"smcup",	"ti",	CAP_TYPE_STR },	
	{ "enter_delete_mode",		"smdc",		"dm",	CAP_TYPE_STR },	
	{ "enter_dim_mode",		"dim",		"mh",	CAP_TYPE_STR },	
	{ "enter_insert_mode",		"smir",		"im",	CAP_TYPE_STR },	
	{ "enter_secure_mode",		"invis",	"mk",	CAP_TYPE_STR },	
	{ "enter_protected_mode",	"prot",		"mp",	CAP_TYPE_STR },	
	{ "enter_reverse_mode",		"rev",		"mr",	CAP_TYPE_STR },	
	{ "enter_standout_mode",	"smso",		"so",	CAP_TYPE_STR },	
	{ "enter_underline_mode",	"smul",		"us",	CAP_TYPE_STR },	
	{ "erase_chars",		"ech",		"ec",	CAP_TYPE_STR },	
	{ "exit_alt_charset_mode",	"rmacs",	"ae",	CAP_TYPE_STR },	
	{ "exit_attribute_mode",	"sgr0",		"me",	CAP_TYPE_STR },	
	{ "exit_ca_mode",		"rmcup",	"te",	CAP_TYPE_STR },	
	{ "exit_delete_mode",		"rmdc",		"ed",	CAP_TYPE_STR },	
	{ "exit_insert_mode",		"rmir",		"ei",	CAP_TYPE_STR },	
	{ "exit_standout_mode",		"rmso",		"se",	CAP_TYPE_STR },	
	{ "exit_underline_mode",	"rmul",		"ue",	CAP_TYPE_STR },	
	{ "flash_screen",		"flash",	"vb",	CAP_TYPE_STR },	
	{ "form_feed",			"ff",		"ff",	CAP_TYPE_STR },	
	{ "from_status_line",		"fsl",		"fs",	CAP_TYPE_STR },	
	{ "init_1string",		"is1",		"i1",	CAP_TYPE_STR },	
	{ "init_2string",		"is2",		"is",	CAP_TYPE_STR },	
	{ "init_3string",		"is3",		"i3",	CAP_TYPE_STR },	
	{ "init_file",			"if",		"if",	CAP_TYPE_STR },	
	{ "insert_character",		"ich1",		"ic",	CAP_TYPE_STR },	
	{ "insert_line",		"il1",		"al",	CAP_TYPE_STR },	
	{ "insert_padding",		"ip",		"ip",	CAP_TYPE_STR },	
	{ "key_backspace",		"kbs",		"kb",	CAP_TYPE_STR },	
	{ "key_catab",			"ktbc",		"ka",	CAP_TYPE_STR },	
	{ "key_clear",			"kclr",		"kC",	CAP_TYPE_STR },	
	{ "key_ctab",			"kctab",	"kt",	CAP_TYPE_STR },	
	{ "key_dc",			"kdch1",	"kD",	CAP_TYPE_STR },	
	{ "key_dl",			"kdl1",		"kL",	CAP_TYPE_STR },	
	{ "key_down",			"kcud1",	"kd",	CAP_TYPE_STR },	
	{ "key_eic",			"krmir",	"kM",	CAP_TYPE_STR },	
	{ "key_eol",			"kel",		"kE",	CAP_TYPE_STR },	
	{ "key_eos",			"ked",		"kS",	CAP_TYPE_STR },	
	{ "key_f0",			"kf0",		"k0",	CAP_TYPE_STR },	
	{ "key_f1",			"kf1",		"k1",	CAP_TYPE_STR },	
	{ "key_f10",			"kf10",		"k;",	CAP_TYPE_STR },	
	{ "key_f2",			"kf2",		"k2",	CAP_TYPE_STR },	
	{ "key_f3",			"kf3",		"k3",	CAP_TYPE_STR },	
	{ "key_f4",			"kf4",		"k4",	CAP_TYPE_STR },	
	{ "key_f5",			"kf5",		"k5",	CAP_TYPE_STR },	
	{ "key_f6",			"kf6",		"k6",	CAP_TYPE_STR },	
	{ "key_f7",			"kf7",		"k7",	CAP_TYPE_STR },	
	{ "key_f8",			"kf8",		"k8",	CAP_TYPE_STR },	
	{ "key_f9",			"kf9",		"k9",	CAP_TYPE_STR },	
	{ "key_home",			"khome",	"kh",	CAP_TYPE_STR },	
	{ "key_ic",			"kich1",	"kI",	CAP_TYPE_STR },	
	{ "key_il",			"kil1",		"kA",	CAP_TYPE_STR },	
	{ "key_left",			"kcub1",	"kl",	CAP_TYPE_STR },	
	{ "key_ll",			"kll",		"kH",	CAP_TYPE_STR },	
	{ "key_npage",			"knp",		"kN",	CAP_TYPE_STR },	
	{ "key_ppage",			"kpp",		"kP",	CAP_TYPE_STR },	
	{ "key_right",			"kcuf1",	"kr",	CAP_TYPE_STR },	
	{ "key_sf",			"kind",		"kF",	CAP_TYPE_STR },	
	{ "key_sr",			"kri",		"kR",	CAP_TYPE_STR },	
	{ "key_stab",			"khts",		"kT",	CAP_TYPE_STR },	
	{ "key_up",			"kcuu1",	"ku",	CAP_TYPE_STR },	
	{ "keypad_local",		"rmkx",		"ke",	CAP_TYPE_STR },	
	{ "keypad_xmit",		"smkx",		"ks",	CAP_TYPE_STR },	
	{ "lab_f0",			"lf0",		"l0",	CAP_TYPE_STR },	
	{ "lab_f1",			"lf1",		"l1",	CAP_TYPE_STR },	
	{ "lab_f10",			"lf10",		"la",	CAP_TYPE_STR },	
	{ "lab_f2",			"lf2",		"l2",	CAP_TYPE_STR },	
	{ "lab_f3",			"lf3",		"l3",	CAP_TYPE_STR },	
	{ "lab_f4",			"lf4",		"l4",	CAP_TYPE_STR },	
	{ "lab_f5",			"lf5",		"l5",	CAP_TYPE_STR },	
	{ "lab_f6",			"lf6",		"l6",	CAP_TYPE_STR },	
	{ "lab_f7",			"lf7",		"l7",	CAP_TYPE_STR },	
	{ "lab_f8",			"lf8",		"l8",	CAP_TYPE_STR },	
	{ "lab_f9",			"lf9",		"l9",	CAP_TYPE_STR },	
	{ "meta_off",			"rmm",		"mo",	CAP_TYPE_STR },	
	{ "meta_on",			"smm",		"mm",	CAP_TYPE_STR },	
	{ "newline",			"nel",		"nw",	CAP_TYPE_STR },	
	{ "pad_char",			"pad",		"pc",	CAP_TYPE_STR },	
	{ "parm_dch",			"dch",		"DC",	CAP_TYPE_STR },	
	{ "parm_delete_line",		"dl",		"DL",	CAP_TYPE_STR },	
	{ "parm_down_cursor",		"cud",		"DO",	CAP_TYPE_STR },	
	{ "parm_ich",			"ich",		"IC",	CAP_TYPE_STR },	
	{ "parm_index",			"indn",		"SF",	CAP_TYPE_STR },	
	{ "parm_insert_line",		"il",		"AL",	CAP_TYPE_STR },	
	{ "parm_left_cursor",		"cub",		"LE",	CAP_TYPE_STR },	
	{ "parm_right_cursor",		"cuf",		"RI",	CAP_TYPE_STR },	
	{ "parm_rindex",		"rin",		"SR",	CAP_TYPE_STR },	
	{ "parm_up_cursor",		"cuu",		"UP",	CAP_TYPE_STR },	
	{ "pkey_key",			"pfkey",	"pk",	CAP_TYPE_STR },	
	{ "pkey_local",			"pfloc",	"pl",	CAP_TYPE_STR },	
	{ "pkey_xmit",			"pfx",		"px",	CAP_TYPE_STR },	
	{ "print_screen",		"mc0",		"ps",	CAP_TYPE_STR },	
	{ "prtr_off",			"mc4",		"pf",	CAP_TYPE_STR },	
	{ "prtr_on",			"mc5",		"po",	CAP_TYPE_STR },	
	{ "repeat_char",		"rep",		"rp",	CAP_TYPE_STR },	
	{ "reset_1string",		"rs1",		"r1",	CAP_TYPE_STR },	
	{ "reset_2string",		"rs2",		"r2",	CAP_TYPE_STR },	
	{ "reset_3string",		"rs3",		"r3",	CAP_TYPE_STR },	
	{ "reset_file",			"rf",		"rf",	CAP_TYPE_STR },	
	{ "restore_cursor",		"rc",		"rc",	CAP_TYPE_STR },	
	{ "row_address",		"vpa",		"cv",	CAP_TYPE_STR },	
	{ "save_cursor",		"sc",		"sc",	CAP_TYPE_STR },	
	{ "scroll_forward",		"ind",		"sf",	CAP_TYPE_STR },	
	{ "scroll_reverse",		"ri",		"sr",	CAP_TYPE_STR },	
	{ "set_attributes",		"sgr",		"sa",	CAP_TYPE_STR },	
	{ "set_tab",			"hts",		"st",	CAP_TYPE_STR },	
	{ "set_window",			"wind",		"wi",	CAP_TYPE_STR },	
	{ "tab",			"ht",		"ta",	CAP_TYPE_STR },	
	{ "to_status_line",		"tsl",		"ts",	CAP_TYPE_STR },	
	{ "underline_char",		"uc",		"uc",	CAP_TYPE_STR },	
	{ "up_half_line",		"hu",		"hu",	CAP_TYPE_STR },	
	{ "init_prog",			"iprog",	"iP",	CAP_TYPE_STR },	
	{ "key_a1",			"ka1",		"K1",	CAP_TYPE_STR },	
	{ "key_a3",			"ka3",		"K3",	CAP_TYPE_STR },	
	{ "key_b2",			"kb2",		"K2",	CAP_TYPE_STR },	
	{ "key_c1",			"kc1",		"K4",	CAP_TYPE_STR },	
	{ "key_c3",			"kc3",		"K5",	CAP_TYPE_STR },	
	{ "prtr_non",			"mc5p",		"pO",	CAP_TYPE_STR },	
	{ "char_padding",		"rmp",		"rP",	CAP_TYPE_STR },	
	{ "acs_chars",			"acsc",		"ac",	CAP_TYPE_STR },	
	{ "plab_norm",			"pln",		"pn",	CAP_TYPE_STR },	
	{ "key_btab",			"kcbt",		"kB",	CAP_TYPE_STR },	
	{ "enter_xon_mode",		"smxon",	"SX",	CAP_TYPE_STR },	
	{ "exit_xon_mode",		"rmxon",	"RX",	CAP_TYPE_STR },	
	{ "enter_am_mode",		"smam",		"SA",	CAP_TYPE_STR },	
	{ "exit_am_mode",		"rmam",		"RA",	CAP_TYPE_STR },	
	{ "xon_character",		"xonc",		"XN",	CAP_TYPE_STR },	
	{ "xoff_character",		"xoffc",	"XF",	CAP_TYPE_STR },	
	{ "ena_acs",			"enacs",	"eA",	CAP_TYPE_STR },	
	{ "label_on",			"smln",		"LO",	CAP_TYPE_STR },	
	{ "label_off",			"rmln",		"LF",	CAP_TYPE_STR },	
	{ "key_beg",			"kbeg",		"@1",	CAP_TYPE_STR },	
	{ "key_cancel",			"kcan",		"@2",	CAP_TYPE_STR },	
	{ "key_close",			"kclo",		"@3",	CAP_TYPE_STR },	
	{ "key_command",		"kcmd",		"@4",	CAP_TYPE_STR },	
	{ "key_copy",			"kcpy",		"@5",	CAP_TYPE_STR },	
	{ "key_create",			"kcrt",		"@6",	CAP_TYPE_STR },	
	{ "key_end",			"kend",		"@7",	CAP_TYPE_STR },	
	{ "key_enter",			"kent",		"@8",	CAP_TYPE_STR },	
	{ "key_exit",			"kext",		"@9",	CAP_TYPE_STR },	
	{ "key_find",			"kfnd",		"@0",	CAP_TYPE_STR },	
	{ "key_help",			"khlp",		"%1",	CAP_TYPE_STR },	
	{ "key_mark",			"kmrk",		"%2",	CAP_TYPE_STR },	
	{ "key_message",		"kmsg",		"%3",	CAP_TYPE_STR },	
	{ "key_move",			"kmov",		"%4",	CAP_TYPE_STR },	
	{ "key_next",			"knxt",		"%5",	CAP_TYPE_STR },	
	{ "key_open",			"kopn",		"%6",	CAP_TYPE_STR },	
	{ "key_options",		"kopt",		"%7",	CAP_TYPE_STR },	
	{ "key_previous",		"kprv",		"%8",	CAP_TYPE_STR },	
	{ "key_print",			"kprt",		"%9",	CAP_TYPE_STR },	
	{ "key_redo",			"krdo",		"%0",	CAP_TYPE_STR },	
	{ "key_reference",		"kref",		"&1",	CAP_TYPE_STR },	
	{ "key_refresh",		"krfr",		"&2",	CAP_TYPE_STR },	
	{ "key_replace",		"krpl",		"&3",	CAP_TYPE_STR },	
	{ "key_restart",		"krst",		"&4",	CAP_TYPE_STR },	
	{ "key_resume",			"kres",		"&5",	CAP_TYPE_STR },	
	{ "key_save",			"ksav",		"&6",	CAP_TYPE_STR },	
	{ "key_suspend",		"kspd",		"&7",	CAP_TYPE_STR },	
	{ "key_undo",			"kund",		"&8",	CAP_TYPE_STR },	
	{ "key_sbeg",			"kBEG",		"&9",	CAP_TYPE_STR },	
	{ "key_scancel",		"kCAN",		"&0",	CAP_TYPE_STR },	
	{ "key_scommand",		"kCMD",		"*1",	CAP_TYPE_STR },	
	{ "key_scopy",			"kCPY",		"*2",	CAP_TYPE_STR },	
	{ "key_screate",		"kCRT",		"*3",	CAP_TYPE_STR },	
	{ "key_sdc",			"kDC",		"*4",	CAP_TYPE_STR },	
	{ "key_sdl",			"kDL",		"*5",	CAP_TYPE_STR },	
	{ "key_select",			"kslt",		"*6",	CAP_TYPE_STR },	
	{ "key_send",			"kEND",		"*7",	CAP_TYPE_STR },	
	{ "key_seol",			"kEOL",		"*8",	CAP_TYPE_STR },	
	{ "key_sexit",			"kEXT",		"*9",	CAP_TYPE_STR },	
	{ "key_sfind",			"kFND",		"*0",	CAP_TYPE_STR },	
	{ "key_shelp",			"kHLP",		"#1",	CAP_TYPE_STR },	
	{ "key_shome",			"kHOM",		"#2",	CAP_TYPE_STR },	
	{ "key_sic",			"kIC",		"#3",	CAP_TYPE_STR },	
	{ "key_sleft",			"kLFT",		"#4",	CAP_TYPE_STR },	
	{ "key_smessage",		"kMSG",		"%a",	CAP_TYPE_STR },	
	{ "key_smove",			"kMOV",		"%b",	CAP_TYPE_STR },	
	{ "key_snext",			"kNXT",		"%c",	CAP_TYPE_STR },	
	{ "key_soptions",		"kOPT",		"%d",	CAP_TYPE_STR },	
	{ "key_sprevious",		"kPRV",		"%e",	CAP_TYPE_STR },	
	{ "key_sprint",			"kPRT",		"%f",	CAP_TYPE_STR },	
	{ "key_sredo",			"kRDO",		"%g",	CAP_TYPE_STR },	
	{ "key_sreplace",		"kRPL",		"%h",	CAP_TYPE_STR },	
	{ "key_sright",			"kRIT",		"%i",	CAP_TYPE_STR },	
	{ "key_srsume",			"kRES",		"%j",	CAP_TYPE_STR },	
	{ "key_ssave",			"kSAV",		"!1",	CAP_TYPE_STR },	
	{ "key_ssuspend",		"kSPD",		"!2",	CAP_TYPE_STR },	
	{ "key_sundo",			"kUND",		"!3",	CAP_TYPE_STR },	
	{ "req_for_input",		"rfi",		"RF",	CAP_TYPE_STR },	
	{ "key_f11",			"kf11",		"F1",	CAP_TYPE_STR },	
	{ "key_f12",			"kf12",		"F2",	CAP_TYPE_STR },	
	{ "key_f13",			"kf13",		"F3",	CAP_TYPE_STR },	
	{ "key_f14",			"kf14",		"F4",	CAP_TYPE_STR },	
	{ "key_f15",			"kf15",		"F5",	CAP_TYPE_STR },	
	{ "key_f16",			"kf16",		"F6",	CAP_TYPE_STR },	
	{ "key_f17",			"kf17",		"F7",	CAP_TYPE_STR },	
	{ "key_f18",			"kf18",		"F8",	CAP_TYPE_STR },	
	{ "key_f19",			"kf19",		"F9",	CAP_TYPE_STR },	
	{ "key_f20",			"kf20",		"FA",	CAP_TYPE_STR },	
	{ "key_f21",			"kf21",		"FB",	CAP_TYPE_STR },	
	{ "key_f22",			"kf22",		"FC",	CAP_TYPE_STR },	
	{ "key_f23",			"kf23",		"FD",	CAP_TYPE_STR },	
	{ "key_f24",			"kf24",		"FE",	CAP_TYPE_STR },	
	{ "key_f25",			"kf25",		"FF",	CAP_TYPE_STR },	
	{ "key_f26",			"kf26",		"FG",	CAP_TYPE_STR },	
	{ "key_f27",			"kf27",		"FH",	CAP_TYPE_STR },	
	{ "key_f28",			"kf28",		"FI",	CAP_TYPE_STR },	
	{ "key_f29",			"kf29",		"FJ",	CAP_TYPE_STR },	
	{ "key_f30",			"kf30",		"FK",	CAP_TYPE_STR },	
	{ "key_f31",			"kf31",		"FL",	CAP_TYPE_STR },	
	{ "key_f32",			"kf32",		"FM",	CAP_TYPE_STR },	
	{ "key_f33",			"kf33",		"FN",	CAP_TYPE_STR },	
	{ "key_f34",			"kf34",		"FO",	CAP_TYPE_STR },	
	{ "key_f35",			"kf35",		"FP",	CAP_TYPE_STR },	
	{ "key_f36",			"kf36",		"FQ",	CAP_TYPE_STR },	
	{ "key_f37",			"kf37",		"FR",	CAP_TYPE_STR },	
	{ "key_f38",			"kf38",		"FS",	CAP_TYPE_STR },	
	{ "key_f39",			"kf39",		"FT",	CAP_TYPE_STR },	
	{ "key_f40",			"kf40",		"FU",	CAP_TYPE_STR },	
	{ "key_f41",			"kf41",		"FV",	CAP_TYPE_STR },	
	{ "key_f42",			"kf42",		"FW",	CAP_TYPE_STR },	
	{ "key_f43",			"kf43",		"FX",	CAP_TYPE_STR },	
	{ "key_f44",			"kf44",		"FY",	CAP_TYPE_STR },	
	{ "key_f45",			"kf45",		"FZ",	CAP_TYPE_STR },	
	{ "key_f46",			"kf46",		"Fa",	CAP_TYPE_STR },	
	{ "key_f47",			"kf47",		"Fb",	CAP_TYPE_STR },	
	{ "key_f48",			"kf48",		"Fc",	CAP_TYPE_STR },	
	{ "key_f49",			"kf49",		"Fd",	CAP_TYPE_STR },	
	{ "key_f50",			"kf50",		"Fe",	CAP_TYPE_STR },	
	{ "key_f51",			"kf51",		"Ff",	CAP_TYPE_STR },	
	{ "key_f52",			"kf52",		"Fg",	CAP_TYPE_STR },	
	{ "key_f53",			"kf53",		"Fh",	CAP_TYPE_STR },	
	{ "key_f54",			"kf54",		"Fi",	CAP_TYPE_STR },	
	{ "key_f55",			"kf55",		"Fj",	CAP_TYPE_STR },	
	{ "key_f56",			"kf56",		"Fk",	CAP_TYPE_STR },	
	{ "key_f57",			"kf57",		"Fl",	CAP_TYPE_STR },	
	{ "key_f58",			"kf58",		"Fm",	CAP_TYPE_STR },	
	{ "key_f59",			"kf59",		"Fn",	CAP_TYPE_STR },	
	{ "key_f60",			"kf60",		"Fo",	CAP_TYPE_STR },	
	{ "key_f61",			"kf61",		"Fp",	CAP_TYPE_STR },	
	{ "key_f62",			"kf62",		"Fq",	CAP_TYPE_STR },	
	{ "key_f63",			"kf63",		"Fr",	CAP_TYPE_STR },	
	{ "clr_bol",			"el1",		"cb",	CAP_TYPE_STR },	
	{ "clear_margins",		"mgc",		"MC",	CAP_TYPE_STR },	
	{ "set_left_margin",		"smgl",		"ML",	CAP_TYPE_STR },	
	{ "set_right_margin",		"smgr",		"MR",	CAP_TYPE_STR },	
	{ "label_format",		"fln",		"Lf",	CAP_TYPE_STR },	
	{ "set_clock",			"sclk",		"SC",	CAP_TYPE_STR },	
	{ "display_clock",		"dclk",		"DK",	CAP_TYPE_STR },	
	{ "remove_clock",		"rmclk",	"RC",	CAP_TYPE_STR },	
	{ "create_window",		"cwin",		"CW",	CAP_TYPE_STR },	
	{ "goto_window",		"wingo",	"WG",	CAP_TYPE_STR },	
	{ "hangup",			"hup",		"HU",	CAP_TYPE_STR },	
	{ "dial_phone",			"dial",		"DI",	CAP_TYPE_STR },	
	{ "quick_dial",			"qdial",	"QD",	CAP_TYPE_STR },	
	{ "tone",			"tone",		"TO",	CAP_TYPE_STR },	
	{ "pulse",			"pulse",	"PU",	CAP_TYPE_STR },	
	{ "flash_hook",			"hook",		"fh",	CAP_TYPE_STR },	
	{ "fixed_pause",		"pause",	"PA",	CAP_TYPE_STR },	
	{ "wait_tone",			"wait",		"WA",	CAP_TYPE_STR },	
	{ "user0",			"u0",		"u0",	CAP_TYPE_STR },	
	{ "user1",			"u1",		"u1",	CAP_TYPE_STR },	
	{ "user2",			"u2",		"u2",	CAP_TYPE_STR },	
	{ "user3",			"u3",		"u3",	CAP_TYPE_STR },	
	{ "user4",			"u4",		"u4",	CAP_TYPE_STR },	
	{ "user5",			"u5",		"u5",	CAP_TYPE_STR },	
	{ "user6",			"u6",		"u6",	CAP_TYPE_STR },	
	{ "user7",			"u7",		"u7",	CAP_TYPE_STR },	
	{ "user8",			"u8",		"u8",	CAP_TYPE_STR },	
	{ "user9",			"u9",		"u9",	CAP_TYPE_STR },	
	{ "orig_pair",			"op",		"op",	CAP_TYPE_STR },	
	{ "orig_colors",		"oc",		"oc",	CAP_TYPE_STR },	
	{ "initialize_color",		"initc",	"Ic",	CAP_TYPE_STR },	
	{ "initialize_pair",		"initp",	"Ip",	CAP_TYPE_STR },	
	{ "set_color_pair",		"scp",		"sp",	CAP_TYPE_STR },	
	{ "set_foreground",		"setf",		"Sf",	CAP_TYPE_STR },	
	{ "set_background",		"setb",		"Sb",	CAP_TYPE_STR },	
	{ "change_char_pitch",		"cpi",		"ZA",	CAP_TYPE_STR },	
	{ "change_line_pitch",		"lpi",		"ZB",	CAP_TYPE_STR },	
	{ "change_res_horz",		"chr",		"ZC",	CAP_TYPE_STR },	
	{ "change_res_vert",		"cvr",		"ZD",	CAP_TYPE_STR },	
	{ "define_char",		"defc",		"ZE",	CAP_TYPE_STR },	
	{ "enter_doublewide_mode",	"swidm",	"ZF",	CAP_TYPE_STR },	
	{ "enter_draft_quality",	"sdrfq",	"ZG",	CAP_TYPE_STR },	
	{ "enter_italics_mode",		"sitm",		"ZH",	CAP_TYPE_STR },	
	{ "enter_leftward_mode",	"slm",		"ZI",	CAP_TYPE_STR },	
	{ "enter_micro_mode",		"smicm",	"ZJ",	CAP_TYPE_STR },	
	{ "enter_near_letter_quality",	"snlq",		"ZK",	CAP_TYPE_STR },	
	{ "enter_normal_quality",	"snrmq",	"ZL",	CAP_TYPE_STR },	
	{ "enter_shadow_mode",		"sshm",		"ZM",	CAP_TYPE_STR },	
	{ "enter_subscript_mode",	"ssubm",	"ZN",	CAP_TYPE_STR },	
	{ "enter_superscript_mode",	"ssupm",	"ZO",	CAP_TYPE_STR },	
	{ "enter_upward_mode",		"sum",		"ZP",	CAP_TYPE_STR },	
	{ "exit_doublewide_mode",	"rwidm",	"ZQ",	CAP_TYPE_STR },	
	{ "exit_italics_mode",		"ritm",		"ZR",	CAP_TYPE_STR },	
	{ "exit_leftward_mode",		"rlm",		"ZS",	CAP_TYPE_STR },	
	{ "exit_micro_mode",		"rmicm",	"ZT",	CAP_TYPE_STR },	
	{ "exit_shadow_mode",		"rshm",		"ZU",	CAP_TYPE_STR },	
	{ "exit_subscript_mode",	"rsubm",	"ZV",	CAP_TYPE_STR },	
	{ "exit_superscript_mode",	"rsupm",	"ZW",	CAP_TYPE_STR },	
	{ "exit_upward_mode",		"rum",		"ZX",	CAP_TYPE_STR },	
	{ "micro_column_address",	"mhpa",		"ZY",	CAP_TYPE_STR },	
	{ "micro_down",			"mcud1",	"ZZ",	CAP_TYPE_STR },	
	{ "micro_left",			"mcub1",	"Za",	CAP_TYPE_STR },	
	{ "micro_right",		"mcuf1",	"Zb",	CAP_TYPE_STR },	
	{ "micro_row_address",		"mvpa",		"Zc",	CAP_TYPE_STR },	
	{ "micro_up",			"mcuu1",	"Zd",	CAP_TYPE_STR },	
	{ "order_of_pins",		"porder",	"Ze",	CAP_TYPE_STR },	
	{ "parm_down_micro",		"mcud",		"Zf",	CAP_TYPE_STR },	
	{ "parm_left_micro",		"mcub",		"Zg",	CAP_TYPE_STR },	
	{ "parm_right_micro",		"mcuf",		"Zh",	CAP_TYPE_STR },	
	{ "parm_up_micro",		"mcuu",		"Zi",	CAP_TYPE_STR },	
	{ "select_char_set",		"scs",		"Zj",	CAP_TYPE_STR },	
	{ "set_bottom_margin",		"smgb",		"Zk",	CAP_TYPE_STR },	
	{ "set_bottom_margin_parm",	"smgbp",	"Zl",	CAP_TYPE_STR },	
	{ "set_left_margin_parm",	"smglp",	"Zm",	CAP_TYPE_STR },	
	{ "set_right_margin_parm",	"smgrp",	"Zn",	CAP_TYPE_STR },	
	{ "set_top_margin",		"smgt",		"Zo",	CAP_TYPE_STR },	
	{ "set_top_margin_parm",	"smgtp",	"Zp",	CAP_TYPE_STR },	
	{ "start_bit_image",		"sbim",		"Zq",	CAP_TYPE_STR },	
	{ "start_char_set_def",		"scsd",		"Zr",	CAP_TYPE_STR },	
	{ "stop_bit_image",		"rbim",		"Zs",	CAP_TYPE_STR },	
	{ "stop_char_set_def",		"rcsd",		"Zt",	CAP_TYPE_STR },	
	{ "subscript_characters",	"subcs",	"Zu",	CAP_TYPE_STR },	
	{ "superscript_characters",	"supcs",	"Zv",	CAP_TYPE_STR },	
	{ "these_cause_cr",		"docr",		"Zw",	CAP_TYPE_STR },	
	{ "zero_motion",		"zerom",	"Zx",	CAP_TYPE_STR },	
	{ "char_set_names",		"csnm",		"Zy",	CAP_TYPE_STR },	
	{ "key_mouse",			"kmous",	"Km",	CAP_TYPE_STR },	
	{ "mouse_info",			"minfo",	"Mi",	CAP_TYPE_STR },	
	{ "req_mouse_pos",		"reqmp",	"RQ",	CAP_TYPE_STR },	
	{ "get_mouse",			"getm",		"Gm",	CAP_TYPE_STR },	
	{ "set_a_foreground",		"setaf",	"AF",	CAP_TYPE_STR },	
	{ "set_a_background",		"setab",	"AB",	CAP_TYPE_STR },	
	{ "pkey_plab",			"pfxl",		"xl",	CAP_TYPE_STR },	
	{ "device_type",		"devt",		"dv",	CAP_TYPE_STR },	
	{ "code_set_init",		"csin",		"ci",	CAP_TYPE_STR },	
	{ "set0_des_seq",		"s0ds",		"s0",	CAP_TYPE_STR },	
	{ "set1_des_seq",		"s1ds",		"s1",	CAP_TYPE_STR },	
	{ "set2_des_seq",		"s2ds",		"s2",	CAP_TYPE_STR },	
	{ "set3_des_seq",		"s3ds",		"s3",	CAP_TYPE_STR },	
	{ "set_lr_margin",		"smglr",	"ML",	CAP_TYPE_STR },	
	{ "set_tb_margin",		"smgtb",	"MT",	CAP_TYPE_STR },	
	{ "bit_image_repeat",		"birep",	"Xy",	CAP_TYPE_STR },	
	{ "bit_image_newline",		"binel",	"Zz",	CAP_TYPE_STR },	
	{ "bit_image_carriage_return",	"bicr",		"Yv",	CAP_TYPE_STR },	
	{ "color_names",		"colornm",	"Yw",	CAP_TYPE_STR },	
	{ "define_bit_image_region",	"defbi",	"Yx",	CAP_TYPE_STR },	
	{ "end_bit_image_region",	"endbi",	"Yy",	CAP_TYPE_STR },	
	{ "set_color_band",		"setcolor",	"Yz",	CAP_TYPE_STR },	
	{ "set_page_length",		"slines",	"YZ",	CAP_TYPE_STR },	
	{ "display_pc_char",		"dispc",	"S1",	CAP_TYPE_STR },	
	{ "enter_pc_charset_mode",	"smpch",	"S2",	CAP_TYPE_STR },	
	{ "exit_pc_charset_mode",	"rmpch",	"S3",	CAP_TYPE_STR },	
	{ "enter_scancode_mode",	"smsc",		"S4",	CAP_TYPE_STR },	
	{ "exit_scancode_mode",		"rmsc",		"S5",	CAP_TYPE_STR },	
	{ "pc_term_options",		"pctrm",	"S6",	CAP_TYPE_STR },	
	{ "scancode_escape",		"scesc",	"S7",	CAP_TYPE_STR },	
	{ "alt_scancode_esc",		"scesa",	"S8",	CAP_TYPE_STR },	
	{ "enter_horizontal_hl_mode",	"ehhlm",	"Xh",	CAP_TYPE_STR },	
	{ "enter_left_hl_mode",		"elhlm",	"Xl",	CAP_TYPE_STR },	
	{ "enter_low_hl_mode",		"elohlm",	"Xo",	CAP_TYPE_STR },	
	{ "enter_right_hl_mode",	"erhlm",	"Xr",	CAP_TYPE_STR },	
	{ "enter_top_hl_mode",		"ethlm",	"Xt",	CAP_TYPE_STR },	
	{ "enter_vertical_hl_mode",	"evhlm",	"Xv",	CAP_TYPE_STR },	
	{ "set_a_attributes",		"sgr1",		"sA",	CAP_TYPE_STR },	
	{ "set_pglen_inch",		"slength",	"sL",	CAP_TYPE_STR },	
	{ "termcap_init2",		"OTi2",		"i2",	CAP_TYPE_STR },	
	{ "termcap_reset",		"OTrs",		"rs",	CAP_TYPE_STR },	
	{ "magic_cookie_glitch_ul",	"OTug",		"ug",	CAP_TYPE_INT },	
	{ "backspaces_with_bs",		"OTbs",		"bs",	CAP_TYPE_BOOL },	
	{ "crt_no_scrolling",		"OTns",		"ns",	CAP_TYPE_BOOL },	
	{ "no_correctly_working_cr",	"OTnc",		"nc",	CAP_TYPE_BOOL },	
	{ "carriage_return_delay",	"OTdC",		"dC",	CAP_TYPE_INT },	
	{ "new_line_delay",		"OTdN",		"dN",	CAP_TYPE_INT },	
	{ "linefeed_if_not_lf",		"OTnl",		"nl",	CAP_TYPE_STR },	
	{ "backspace_if_not_bs",	"OTbc",		"bc",	CAP_TYPE_STR },	
	{ "gnu_has_meta_key",		"OTMT",		"MT",	CAP_TYPE_BOOL },	
	{ "linefeed_is_newline",	"OTNL",		"NL",	CAP_TYPE_BOOL },	
	{ "backspace_delay",		"OTdB",		"dB",	CAP_TYPE_INT },	
	{ "horizontal_tab_delay",	"OTdT",		"dT",	CAP_TYPE_INT },	
	{ "number_of_function_keys",	"OTkn",		"kn",	CAP_TYPE_INT },	
	{ "other_non_function_keys",	"OTko",		"ko",	CAP_TYPE_STR },	
	{ "arrow_key_map",		"OTma",		"ma",	CAP_TYPE_STR },	
	{ "has_hardware_tabs",		"OTpt",		"pt",	CAP_TYPE_BOOL },	
	{ "return_does_clr_eol",	"OTxr",		"xr",	CAP_TYPE_BOOL },	
	{ "acs_ulcorner",		"OTG2",		"G2",	CAP_TYPE_STR },	
	{ "acs_llcorner",		"OTG3",		"G3",	CAP_TYPE_STR },	
	{ "acs_urcorner",		"OTG1",		"G1",	CAP_TYPE_STR },	
	{ "acs_lrcorner",		"OTG4",		"G4",	CAP_TYPE_STR },	
	{ "acs_ltee",			"OTGR",		"GR",	CAP_TYPE_STR },	
	{ "acs_rtee",			"OTGL",		"GL",	CAP_TYPE_STR },	
	{ "acs_btee",			"OTGU",		"GU",	CAP_TYPE_STR },	
	{ "acs_ttee",			"OTGD",		"GD",	CAP_TYPE_STR },	
	{ "acs_hline",			"OTGH",		"GH",	CAP_TYPE_STR },	
	{ "acs_vline",			"OTGV",		"GV",	CAP_TYPE_STR },	
	{ "acs_plus",			"OTGC",		"GC",	CAP_TYPE_STR },	
	{ "memory_lock",		"meml",		"ml",	CAP_TYPE_STR },	
	{ "memory_unlock",		"memu",		"mu",	CAP_TYPE_STR },	
	{ "box_chars_1",		"box1",		"bx",	CAP_TYPE_STR },	
};
static const int	numcaps = sizeof tcaps / sizeof tcaps[0];

/*
 * term_inputline_putchar: This is what handles the outputting of the input 
 * buffer.
 * It used to handle more, but slowly the stuff in screen.c prepared away
 * all of the nasties that this was intended to handle.  Well anyhow, all
 * we need to worry about here is making sure nothing suspcious, like an
 * escape, makes its way to the output stream.
 */
void	term_inputline_putchar (unsigned char c)
{
	/*
	 * Any nonprintable characters we lop off.  In addition to this,
	 * we catch the nasty 0x9b which is the escape character with
	 * the high bit set.  
	 */
	if (c < 0x20/* || c == 0x9b*/)
	{
		term_reverse_on();
		term_output_char((c | 0x40) & 0x7f);
		term_reverse_off();
	}

	/*
	 * The delete character is handled especially as well
	 */
	else if (c == 0x7f) 	/* delete char */
	{
		term_reverse_on();
		term_output_char('?');
		term_reverse_off();
	}

	/*
	 * Everything else is passed through.
	 */
	else
		term_output_char(c);
}


/*
 * term_reset: sets terminal attributed back to what they were before the
 * program started 
 */
void	term_reset (void)
{
	tcsetattr(STDIN_FILENO, TCSADRAIN, &oldb);

	tputs_x(tiparm(term_get_capstr("csr"), 0, get_screen_lines(main_screen) - 1));
	term_move_cursor(0, get_screen_lines(main_screen) - 1);
	term_disable_last_column();
	term_flush();
}

/*
 * term_cont: sets the terminal back to IRCII stuff when it is restarted
 * after a SIGSTOP.  Somewhere, this must be used in a signal() call 
 */
SIGNAL_HANDLER(term_cont)
{
	foreground = (tcgetpgrp(0) == getpgrp());
	if (foreground)
	{
		term_establish_last_column();
		need_redraw = 1;
		tcsetattr(STDIN_FILENO, TCSADRAIN, &newb);
	}
}

/*
 * term_init: does all terminal initialization... reads termcap info, sets
 * the terminal to CBREAK, no ECHO mode.   Chooses the best of the terminal
 * attributes to use ..  for the version of this function that is called for
 * wserv, we set the termial to RAW, no ECHO, so that all the signals are
 * ignored.. fixes quite a few problems...  -phone, jan 1993..
 */
int 	term_init (void)
{
	int	i;
	char	*term;

	/* This does not need to be a panic. */
	/*
	 * XXX TODO - This is wrong.
	 * Not being in fullscreen mode does not mean we don't want to interrogate TERM,
	 * it just means we don't want to move the cursor and stuff.
	 */
	if (!fullscreen_mode)
		return -1;

	/*
	 *
	 * PHASE 1: DETERMINE THE TERMINAL'S CAPABILITIES
	 *
	 */

	/* Phase 1 Step 1 - You must have a TERM setting */
	if (!(term = getenv("TERM")))
	{
		fprintf(stderr, "\n");
		fprintf(stderr, "You do not have a TERM environment variable.\n");
		fprintf(stderr, "So we'll be running in non-fullscreen mode...\n");
		return -1;
	}
	else
		printf("Using terminal type [%s]\n", term);

	/* Phase 1 Step 2 -- Your TERM setting must pass muster through setupterm() */
	if ((setupterm(NULL, STDOUT_FILENO, &i)) || i != 1)
	{
		fprintf(stderr, "setupterm failed: %d\n", i);
		fprintf(stderr, "So we'll be running in non-fullscreen mode...\n");
		return -1;
	}
	/* cur_term is a magic global variable declared in <term.h> */
	terminfo_session = cur_term;

	/* Phase 1 Step 3 - Download the terminfo data into a JSON */
	terminfo_json = export_terminfo_to_json();

	/* Phase 1 Step 4 -- Correct for TERM deficiencies */
	/* I reviewed several TERM settings used by people to ensure these were consistent */
	if (!cJSON_GetObjectItem(terminfo_json, "cup"))
		cJSON_AddStringToObject(terminfo_json, "cup", "\033[%i%p1%d;%p2%dH");
	if (!cJSON_GetObjectItem(terminfo_json, "ed"))
		cJSON_AddStringToObject(terminfo_json, "ed", "\033[J");
	if (!cJSON_GetObjectItem(terminfo_json, "el"))
		cJSON_AddStringToObject(terminfo_json, "el", "\033[K");
	if (!cJSON_GetObjectItem(terminfo_json, "csr"))
		cJSON_AddStringToObject(terminfo_json, "csr", "\033[%i%p1%d;%p2%dr");
	if (!cJSON_GetObjectItem(terminfo_json, "ri"))
		cJSON_AddStringToObject(terminfo_json, "ri", "\033M");
	if (!cJSON_GetObjectItem(terminfo_json, "ind"))
		cJSON_AddStringToObject(terminfo_json, "ind", "\n");

	/* Our extensions */
	if (!cJSON_GetObjectItem(terminfo_json, "rmrev"))
		cJSON_AddStringToObject(terminfo_json, "rmrev", "\03327m");
	if (!cJSON_GetObjectItem(terminfo_json, "c7c1t"))
		cJSON_AddStringToObject(terminfo_json, "c7c1t", "\033F");


	/* Phase 1 Step 5 -- Figure out colors (eww, gross) */
	/* I sourced these from infocmp, under the guidance of gemini */
	if (!cJSON_GetObjectItem(terminfo_json, "setaf"))
		cJSON_AddStringToObject(terminfo_json, "setaf", "\033[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m");
	if (!cJSON_GetObjectItem(terminfo_json, "setab"))
		cJSON_AddStringToObject(terminfo_json, "setaf", "\033[%?%p1%{8}%<%t4%p1%d%e%p1%{16}%<%t10%p1%{8}%-%d%e48;5;%p1%d%;m");
	if (!cJSON_GetObjectItem(terminfo_json, "setrgbf"))
		cJSON_AddStringToObject(terminfo_json, "setrgbf", "\033[38;2;%p1%d;%p2%d;%p3%dm");
	if (!cJSON_GetObjectItem(terminfo_json, "setrgbb"))
		cJSON_AddStringToObject(terminfo_json, "setrgbb", "\033[48;2;%p1%d;%p2%d;%p3%dm");

	/* Phase 1 Step 6 -- Let's ensure we have default values for everything */
	cJSON_UpsertNumberToObject(terminfo_json, "li", 24);
	cJSON_UpsertNumberToObject(terminfo_json, "co", 80);

	if (!cJSON_GetObjectItem(terminfo_json, "nel"))
		cJSON_AddStringToObject(terminfo_json, "nel", "\n");
	if (!cJSON_GetObjectItem(terminfo_json, "cr"))
		cJSON_AddStringToObject(terminfo_json, "cr", "\r");
	if (!cJSON_GetObjectItem(terminfo_json, "bel"))
		cJSON_AddStringToObject(terminfo_json, "bel", "\007");

	/*
	 *
	 * PHASE 2: ESTABLISH THE TERMINAL DISCIPLINE
	 *
	 */
	tcgetattr(STDIN_FILENO, &oldb);

	/*
	 * So by default, the kernel intercepts a great deal of keypresses
	 * and does in-kernel input editing.  These keypresses are handled
	 * by the kernel, and the result is a line of input which is then 
	 * passed to the application.  Those special keypresses are never
	 * sent to the application.
	 *
	 * So we need to turn all that off, since we need to be able to 
	 * bind anything.
	 */

	/*
	 * Turning off ICANON tells the kernel not to buffer input from
	 * the user, but to pass it on to us directly.  Further, it tells
	 * the kernel not to process the EOF (^D), EOL (^J), ERASE (^?)
	 * and KILL (^U) keys, but to pass them on.
	 *
	 * Turning off ECHO tells the kernel not to output characters as
	 * they are typed; we take responsibility for that.
	 */
	newb = oldb;
	newb.c_lflag &= ~(unsigned int)(ICANON | ECHO); /* set equ. of CBREAK, no ECHO */

	/*
	 * Turning off IEXTEN tells the kernel not to honor any posix 
	 * extensions it might know about.  On 4.4BSD, this tells the 
	 * kernel not to withhold the EOL2 (undefined), WERASE (^W), 
	 * REPRINT (^R), LNEXT (^V), DISCARD (^O), and STATUS (^T) 
	 * characters from us.  You wouldn't be able to /bind those keys
	 * if this was left on.
	 */
	newb.c_lflag &= ~(unsigned int)(IEXTEN);  /* Turn off DISCARD/LNEXT */

	/*
	 * Since we turned off ICANON, we have to tell the kernel how
	 * we want the read() call to be satisifed.  We want read() to
	 * return 0 seconds after 1 byte has been received (ie, immediately)
	 */
	newb.c_cc[VMIN] = 1;	          /* read() satified after 1 char */
	newb.c_cc[VTIME] = 0;	          /* No timer */

	/*
	 * Next, we have to reclaim all of the signal-generating keys.
	 * Normally you would do that by turning off ISIG, but we can't do
	 * that, because we want ^C to generate SIGINT; that's how the user
	 * can bail out of infinite recursion.
	 *
	 * So we have to actually turn off all of the signal-generating
	 * keys *except* ^C by hand:  QUIT (^\), DSUSP (^Y), and SUSP (^Z).
	 */
	newb.c_cc[VQUIT] = _POSIX_VDISABLE;
#	ifdef VDSUSP
		newb.c_cc[VDSUSP] = _POSIX_VDISABLE;
# 	endif
#	ifdef VSUSP
		newb.c_cc[VSUSP] = _POSIX_VDISABLE;
#	endif

	/*
	 * Now we have to turn off hardware flow control, in order to reclaim the 
	 * ^S (XOFF) and ^Q (XON) keys.  Hardware flow control is not your friend,
	 * because while the flow control is off, epic will block when it tries 
	 * to output something, which could lead to you pinging out all your servers.
	 */
	newb.c_iflag &= ~(unsigned long)(IXON | IXOFF);	/* No XON/XOFF */

	/*
	 * Next we tell the kernel that it should support 8 bits both
	 * coming in and going out, and it should not strip the high bit
	 * from any keypress.
	 */
	newb.c_cflag |= CS8;
	newb.c_iflag &= ~(unsigned long)ISTRIP;

	/* Commit our changes and we're done! */
	tcsetattr(STDIN_FILENO, TCSADRAIN, &newb);


	/*
	 *
	 * PHASE 3 - ESTABLISHING OUR APPLICATION SETTINGS
	 *
	 */

	/*
	 * When you write a character to the last column of a line, most
	 * terminal emulators will advance the cursor to the start of the
	 * next line ("automargins") but some will leave the cursor at the end
	 * of the line ("no automargins").  It's not always obvious which
	 * state you're in, and it's not always obvious whether the emulator
	 * supports one, or both modes.
	 *
	 * The client avoids all this by refusing to use the last column.  
	 * But if you want to, you can define this and epic will turn off 
	 * auto-margins and will attempt to use the final column in each line.
	 * There are no guarantees it will work, since some emulators say they 
	 * support turning off auto-margins, but they lie.
	 */
	term_establish_last_column();

	return 0;
}


/*
 * term_resize: gets the terminal height and width.  Trys to get the info
 * from the tty driver about size, if it can't... uses the termcap values. If
 * the terminal size has changed since last time term_resize() has been
 * called, 1 is returned.  If it is unchanged, 0 is returned. 
 */
int	term_resize (void)
{
	static	int	old_li = -1,
			old_co = -1;
	struct winsize window;
		int	retval = 0;

	/*
	 * This hack is required by a race condition within screen;
	 * if you have a "caption always" bar, when you reattach to
	 * a session, it will send us a SIGWINCH, before it has 
	 * accounted for the "caption bar" stealing a line from the
	 * screen.  So we race screen, and if we ask for the size of
	 * the screen before it accounts for the caption bar, then
	 * we lose, because we'll get the wrong number of lines, and
	 * that will screw up the status bar.  So we do this small 
	 * sleep to increase the chances of us losing the race, 
	 * which means we win.  Got it?
	 */
	my_sleep(0.05);

#ifdef HAVE_TCGETWINSIZE
	if (tcgetwinsize(STDOUT_FILENO, &window))
#else 
#ifdef TIOCGWINSZ
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window))
#else
	if (1)
#endif
#endif
		return retval;

	old_li =  cJSON_GetNumberValue(cJSON_GetObjectItem(terminfo_json, "li"));
	old_co =  cJSON_GetNumberValue(cJSON_GetObjectItem(terminfo_json, "co"));

	if (window.ws_row > 0 && window.ws_row != old_li)
	{
		cJSON_UpsertNumberToObject(terminfo_json, "li", window.ws_row);
		set_screen_lines(main_screen, window.ws_row);
		retval = 1;
	}
	if (window.ws_col > 0 && window.ws_col != old_co)
	{
		cJSON_UpsertNumberToObject(terminfo_json, "co", window.ws_col);
		set_screen_columns(main_screen, window.ws_col);
		term_establish_last_column();
		retval = 1;
	}

	return retval;
}

static	void	term_establish_last_column (void)
{
	if (get_int_var(AUTOMARGIN_OVERRIDE_VAR))
		term_enable_last_column();
	else if (cJSON_GetObjectItem(terminfo_json, "am") && cJSON_GetObjectItem(terminfo_json, "rmam"))
		term_enable_last_column();
	else
	{
		int	col;

		term_disable_last_column();
		col =  cJSON_GetNumberValue(cJSON_GetObjectItem(terminfo_json, "co"));
		cJSON_UpsertNumberToObject(terminfo_json, "co", col - 1);
	}
}

static void	term_enable_last_column (void)
{
	term_emit("rmam");
}

static void	term_disable_last_column (void)
{
	term_emit("smam");
}

void	set_automargin_override (void *__U(stuff))
{
	/* This is probably ok */
	if (!fullscreen_mode)
		return;

	/*
	 * This is called whenever you /set automargin_override,
	 * either way.  
	 *
	 * ON - The client should assume it can use the last column
	 *	no matter what the circumstances appear to support
	 * OFF - The client should use (or not use) the last column
	 *	based on whether it appears to be safe to do so.
	 */
	term_establish_last_column();
	need_redraw = 1;
}

/* term_CE_clear_to_eol(): the clear to eol function, right? */
void	term_clear_to_eol	(void)
{
	term_emit("el");
}


void	term_beep (void)
{
	if (get_int_var(BEEP_VAR) && global_beep_ok)
	{
		term_emit("bel");
		/* XXX It seems bogus to force a flush here */
		term_flush();
	}
}

/* Set the cursor position */
void	term_move_cursor (int col, int row)
{
	tputs_x(tiparm(term_get_capstr("cup"), row, col));
}

/* A no-brainer. Clear the screen. */
void	term_clear_screen (void)
{
	/* We can clear by going home and doing an erase-to-end-of-display */
	term_move_cursor(0, 0);
	term_emit("ed");
}

/*
 * term_scroll - Scroll a window (part of the screen) up
 *
 * When you /echo to a window, and the window is full, you need to scroll
 * the window up to make a blank line for the new line of /echo.
 * You tell this function the top and bottom lines of the scrollable part
 * of the window (ie, not including the status bars) and it scroll sit up 
 * N lines, leaving N blank lines at the bottom.
 *
 * Arguments:
 *	top	- The top line (row) of the scrollable part of a window
 *	bot	- The bottom line (row) of the scrollable part of a window
 *	n	- How many lines to scroll the window (and thus how many 
 *		  lines to leave blank at the bottom of the window)
 *		  - This comes from /SET SCROLL_LINES
 *
 * Constraints:
 * 	- 'n' has to be greater than 0.  We don't scroll "down"
 * 	- 'top' has to be less than 'bot'.
 */
void	term_scroll (int top, int bot, int n)
{
	int	i;
static	char 	*start = NULL,
		*final = NULL;
	int	lines_;

	/* Some basic sanity checks */
	if (n < 1 || top >= bot)
		return;

	/*
	 * We will start by setting the scrolling region to (top, bottom)
	 * 
	 * XXX The use of static variables and malloc_strcpy()
	 * is a hack so we do not have to new_free() them every time,
	 * and we take advantage of malloc_strcpy() devolving to a strcpy()
	 * if the buffer is already big enough.
	 * This can go away when we switch to arena allocations.
	 */
	malloc_strcpy(&start, tiparm(term_get_capstr("csr"), top, bot));
	lines_ =  (int)cJSON_GetNumberValue(cJSON_GetObjectItem(terminfo_json, "li"));
	malloc_strcpy(&final, tiparm(term_get_capstr("csr"), 0, lines_ - 1));

	/* Do the actual work here */
	tputs_x(start);			/* Set the scroll region */
	term_move_cursor(0, bot);	/* Go to the bottom line */

	/* Then do "cursor down" N times, which does the scrolling */
	for (i = 0; i < n; i++)
		term_emit("ind");

	term_move_cursor(0, top);	/* Got to the top line */
	tputs_x(final);			/* And reset the scroll region */
}

/*
 * control_mangle - A helper function for get_term_capability
 *
 * Arguments:
 *	text		- A value from your TERM setting
 *	retval		- Where to put the copied/mangled string
 *	retval_size	- How big retval is
 *
 * Return value:
 *	'text' but with all the control keys mangled (ie, \001 -> "^A")
 */
static void	control_mangle (const char *text, char *retval, size_t retval_size)
{
	size_t	pos;

	/* NULLs become zero-length strings */
	if (!text)
	{
		*retval = 0;
		return;
	}

	/* Copy/mangle 'text' into 'retval */
	for (pos = 0; *text && (pos < retval_size - 1); text++, pos++)
	{
		if ((unsigned char)*text < 32) 
		{
			retval[pos++] = '^';
			retval[pos] = *text + 64;
		}
		else if ((unsigned char)*text == 127)
		{
			retval[pos++] = '^';
			retval[pos] = '?';
		}
		else
			retval[pos] = *text;
	}

	retval[pos] = 0;
}

/*
 * get_term_capability - Lookup a TERM value by key
 * 
 * Arguments:
 *	name		- The name of a key in the TERM
 *	querytype 	- Either CAP_TYPE_BOOL, CAP_TYPE_INT, or CAP_TYPE_STR
 *			  Yes, you are expected to know the type is is.
 *	mangle		- 0 - I want the raw string in binary
 *				This is if you intend to tputs_x() it.
 *			- 1 - I need you to make it printable (quote control chars)
 *				This is if you intend to say() it.
 *
 * Return value:
 *	The value for the "name" key in your current TERM setting.
 *
 * Used by:
 *	init_termkeys	- To get keybindings for pageup, pagedown, cursor keys, etc.
 *	$getcap()	- So the user can query it for /bind or whatever!
 */
const char *	get_term_capability (const char *name, int querytype, int mangle)
{
static	char		*retval = NULL;
	const char *	compare = empty_string;
	const cap2info *t;
	int		x;

	for (x = 0; x < numcaps; x++) 
	{
		t = &tcaps[x];
		if (querytype == 0)
			compare = t->longname;
		else if (querytype == 1)
			compare = t->iname;
		else if (querytype == 2)
			compare = t->tname;

		(void)t->type;
		if (!strcmp(name, compare)) 
		{
			const char *s;

			if (!(s = cJSON_GetStringValue(cJSON_GetObjectItem(terminfo_json, t->iname))))
				return NULL;

			if (mangle)
			{
				char *	mangled;
				size_t	mangled_size;

				mangled_size = strlen(s) * 2 + 3;
				mangled = alloca(mangled_size);
				control_mangle(s, mangled, mangled_size);
				malloc_strcpy(&retval, mangled);
			}
			else
				malloc_strcpy(&retval, s);

			return retval;
		}
	}
	return NULL;
}

static	const char *	term_get_capstr (const char *name)
{
	return cJSON_GetStringValue(cJSON_GetObjectItem(terminfo_json, name));
}


/*
 * term_sgr - Conslidated attribute outputting
 * 
 * Arguments
 *	<various highlight attributes, in order>
 *
 * Notes:
 * 	If you provide 9 attributes in the correct order, you can use "sgr" to create a single
 * 	string that outputs all of them at once.  This can save a lot of bytes on output!
 *
 * Used by: 
 *	term_attributes() [in screen.c] to output the Attribute change
 *
 * Notes:
 *	We could pass in "italics" and colors, and then create one big string.
 *	We could also pass in "old attribute" and "new attribute" and then look for differences.
 *		That would be very optimizing.
 */
void	term_sgr (int standout, int underline, int reverse, int blink, int dim, int bold, int invisible, int protected, int altcharset)
{
	char *combined;
	const char *ti_sgr;

	ti_sgr = term_get_capstr("sgr");

	combined = tiparm(ti_sgr, standout, underline, reverse, blink, dim, bold, invisible, protected, altcharset);
	tputs_x(combined);
}

void	get_term_geometry (int *li_, int *co_)
{
	*li_ =  cJSON_GetNumberValue(cJSON_GetObjectItem(terminfo_json, "li"));
	*co_ =  cJSON_GetNumberValue(cJSON_GetObjectItem(terminfo_json, "co"));
}


/*
 * See https://pubs.opengroup.org/onlinepubs/7908799/xcurses/tigetflag.html
 * for more information about the magic values used here
 */
static cJSON *	export_terminfo_to_json (void) 
{
	cJSON *root;
	int	i, 
		ival;
	char *	sval;

	/*
	 * 'boolnames', 'numnames' and 'strnames' 
	 * are magic global variables defined by the terminfo system.
	 * They are declared/exposed in <term.h>
	 */
	root = cJSON_CreateObject();
	for (i = 0; boolnames[i]; i++) 
	{
		ival = tigetflag(boolnames[i]);
		if (ival == -1)
			continue;

		cJSON_AddBoolToObject(root, boolnames[i], ival);
	}

	for (i = 0; numnames[i]; i++) 
	{
		ival = tigetnum(numnames[i]);
		if (ival == -1 || ival == -2)
			continue;

		cJSON_AddNumberToObject(root, numnames[i], ival);
	}

	for (i = 0; strnames[i]; i++) 
	{
		sval = tigetstr(strnames[i]);
		if (sval == NULL || sval == (char *) -1)
			continue;

		cJSON_AddStringToObject(root, strnames[i], sval);
	}

	return root;
}

/*
 * term_emit - output a terminfo attribute string that doesn't require parameters
 *
 * Arguments:
 *	attribute	- A terminfo attribute, like "rs2" 
 *			  The value must be a literal string.
 */
void	term_emit (const char *attribute)
{
	cJSON *		object;
	const char *	str;

	if (!attribute)
		return;

	if (!(object = cJSON_GetObjectItem(terminfo_json, attribute)))
		return;

	str = cJSON_GetValueAsString(object);
	tputs_x(str);
}

/*
 * term_raw_bytes - send raw bytes to the terminal (dangerous!)
 *
 * Arguments:
 *	bytes	- bytes to send to the terminal.  You better know what you're doing!
 */
void	term_raw_bytes (const char *bytes)
{
	tputs_x(bytes);
}

void	term_set_foreground (int color)
{
	tputs_x(tiparm(term_get_capstr("setaf"), color));
}

void	term_set_background (int color)
{
	tputs_x(tiparm(term_get_capstr("setab"), color));
}

void	term_set_rgb_foreground (int red, int green, int blue)
{
	tputs_x(tiparm(term_get_capstr("setrgbf"), red, green, blue));
}

void	term_set_rgb_background (int red, int green, int blue)
{
	tputs_x(tiparm(term_get_capstr("setrgbb"), red, green, blue));
}

/*
 * For now, this will live here, but eventually it will grow awareness of output_screen
 * and screens will have output buffers which this will use.  one thing at a time for now
 */
int	term_output_char (int c)
{
static 	char 	buffer[BIG_BUFFER_SIZE];
static 	size_t 	pos = 0;

	buffer[pos++] = (char)c;

	if (c == 0 || c == '\n' || pos == BIG_BUFFER_SIZE)
	{
		size_t	bytes_to_write;
		size_t	total_written;
		int	fd;

		bytes_to_write = pos;
		total_written = 0;
		fd = get_screen_fdout(output_screen);

		// Standard write loop to handle partial writes
		while (total_written < bytes_to_write) 
		{
			ssize_t		result;

			result = write(fd, buffer + total_written, bytes_to_write - total_written);

			/* Error handling needs to be better than this. */
			if (result < 0) {
				pos = 0; 
				return -1;
			}
			total_written += (size_t)result;
		}

		// Reset buffer position after successful flush
		pos = 0;
	}

	return 0;
}


