/*
 * server.c:  Things dealing with that program we call ircd.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1993, 2014 EPIC Software Labs.
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

#define NEED_SERVER_LIST
#include "irc.h"
#include "commands.h"
#include "functions.h"
#include "alias.h"
#include "parse.h"
#include "ssl.h"
#include "server.h"
#include "ircaux.h"
#include "lastlog.h"
#include "exec.h"
#include "window.h"
#include "output.h"
#include "names.h"
#include "hook.h"
#include "alist.h"
#include "screen.h"
#include "status.h"
#include "vars.h"
#include "newio.h"
#include "reg.h"
#include "cJSON.h"

/*
 * Some vocabulary:
 *	SERVER DESCRIPTIONS - String representations
 *	SERVERINFO - A data structure and API for server descriptions
 *	SERVER - A state machine  and API for using serverinfos
 *	SERVERLIST - A collection and API for mapping servers to refnums
 */

/*
 * A "server description" is the user's representation of "a server".
 * It's the thing that looks like "irc.host.com:6697" or "5" (refnums)
 * It is always a string.  When getting data from or giving data to the
 *   user, it is always converted to/from a server description.
 */
/*
 * A "serverinfo" is a data structure which stores a "server description".
 * Most string representations are not innately useful.  They have to be
 * parsed into data elements which can be referenced.  For example, if 
 * someone wants the hostname of "irc.host.com:6697", how do you know 
 * which field is the host?  how do you extract it from its context?
 *
 * A "serverinfo" is a data structure and CRUD API for the data contained 
 * in a server description.
 */
/* 
 * A "server" is a state machine which operationalizes a serverinfo.
 * Knowing that a server exists at irc.host.com port 6697 does not make
 * you connected to it.  A "server" is a data structure and API that 
 * brings a serverinfo to life as a connection to irc.
 */
/*
 * A "serverlist" is the collection of servers that the client knows 
 * about and the API that arranges for IO to and from the servers.
 */

/************************ SERVERLIST STUFF ***************************/

/* 
 * The "server_list" is a global list of all of your servers.
 * This is a dynamically resiable array, and it may contain gaps.
 * A server refnum is an index into this array!  Ie, server refnum 0
 * literally means server_list[0].  Server refnums may _never change_
 * during their lifetime.  If you delete a server, it leaves a gap
 * (server_list[x] == NULL).
 */
	Server **server_list = (Server **) 0;

/*
 * "number of servers" is the size of server_list.  This is used to 
 * guard buffer overrun against server_list.
 */
	int	number_of_servers = 0;

/*
 * The "primary_server" is an old ircII concept when multi-servering
 * was unusual.  Your "Primary Server" is the server of last resort
 * when the client wants to do something in the context of a server,
 * but does not have an appropriate one to use.  
 */
	int	primary_server = NOSERV;

/*
 * The "from_server" represents the server on whose behalf we are 
 * currently working.  If you think of the client as always doing 
 * one of three things:
 *   1. Processing a keystroke
 *   2. Processing something from the server
 *   3. Resting
 * Context is commutative.  If you do an /exec in an /on msg, then 
 * that /exec runs in the context of the /on msg.  If you do a socket
 * thing in context of a keypress, then that socket thing runs in 
 * the context of the server of the window in which you pressed that key.
 *
 * In all cases where the language is active, there is a "from_server"
 * that represents the server from which the action initiated.
 */
	int	from_server = NOSERV;

/* 
 * The "parsing_server_index" is set to the refnum of a server while
 * we are processing data from it.  It is NOSERV at all other times.
 * The value is _NOT_ commutative.  If you do an /exec in an /on msg, 
 * then it will not be set during the /exec (as contrasted to from_server
 * which will be set during the /exec)
 *
 * This value is only used for:
 *	$S	(if it is set, $S is this value; otherwise it is the
 *		 server of your current window.  $S does not follow
 *		 from_server.  Use $serverctl(FROM_SERVER)
 *	By /TIMER to decide whether this is a server timer or a 
 *		window timer.
 */
	int	parsing_server_index = NOSERV;

/* 
 * The "last_server" is the last server we processed.  That is, it is
 * the most recent value of "parsing_server_index".  It never gets 
 * unset.  But it can be reset at any time to any value.
 *
 * It is only used for $serverctl(LAST_SERVER)
 */
	int	last_server = NOSERV;



	/* Convert user string to json data */
static	int		load_cjson_from_json		(const char *configdoc, cJSON **rootptr);
static	int		load_cjson_from_kwarg		(const char *configdoc, cJSON **rootptr);
static	int		load_cjson			(const char *str, cJSON **rootptr);

	/* CRUD a serverinfo from a string */
	int		serverinfo_clear 		(ServerInfo *s);
	int		serverinfo_load 		(ServerInfo *s, const char *str);
static  void		serverinfo_free 		(ServerInfo *si);
static	const char *	serverinfo_get			(const SI *si, const char *key);
static	bool		serverinfo_set	 		(const SI *si, const char *key, const char *value);

	/* CRUD a serverlist */
	void		server_list_remove_all 		(void);
static  void		server_list_remove	 	(int i);
	void		server_list_display 		(void);
	char *		server_list_to_string 		(void);
	int		server_list_size 		(void);
	Server *	get_server 			(int server);
static	int		next_server_in_group 		(int oldserv, int direction);
static	char *		get_all_server_groups 		(void);

	/* Serverdesc X ServerList */
	int		serverdesc_lookup 		(const char *desc);
	int		serverdesc_insert	 	(const char *desc);
	int		serverdesc_insert_with_group 	(const char *servers_, const char *group);
static  int		serverdesc_update		(const char *str);
static  int		serverdesc_update_aserver	(const char *str, int refnum);
	int		serverdesc_upsert 		(const char *servdesc, int quiet);
static	int		serverdesc_import_file 		(const char *file_path);
	int		serverdesc_import_default_file 	(void);

	/* Serverinfo X ServerList */
	int		serverinfo_matches_servref 	(ServerInfo *si, int servref);
static  int		serverinfo_lookup 		(ServerInfo *si);
static  int		serverinfo_insert 		(ServerInfo *si);
static  void		serverinfo_update_aserver	(const ServerInfo *new_si, int refnum);

	/* CRUD a server */
	const char *	get_si 				(int refnum, const char *field);
static	bool		set_si 				(int refnum, const char *field, const char *value);

	/* User facing API */
	BUILT_IN_COMMAND(servercmd);
	BUILT_IN_COMMAND(disconnectcmd);
	BUILT_IN_COMMAND(reconnectcmd);
	char *		serverctl      			(char *input);

	/* Internal stuff */
static  void		server_io			(int fd);
	void		send_to_server 			(const char *format, ...);
	void		send_to_server_with_payload 	(const char *payload, const char *format, ...);
	void		send_to_aserver 		(int refnum, const char *format, ...);
	void		send_to_aserver_raw 		(int refnum, size_t len, const char *buffer);
static	void		vsend_to_aserver_with_payload 	(int refnum, const char *payload, const char *format, va_list args);

	int		server_bootstrap_connection 	(int server);
static  int		server_grab_address 		(int server);
static	int		server_connect_next_address_internal (int server);
	int		server_connect_next_addr 	(int new_server);
	int		server_more_addrs 		(int refnum);
static	int		server_addrs_left 		(int refnum);
static	void		server_discard_dns 		(int refnum);
	void		server_register 		(int refnum, const char *nick);
	void		server_is_registered 		(int refnum, const char *itsname, const char *ourname);
static	void		server_is_unregistered 		(int refnum);
	int		is_server_open 			(int refnum);
	int		is_server_registered 		(int refnum);
	int		is_server_active 		(int refnum);
	int		is_server_valid 		(int refnum);
	int		servers_close_all 		(const char *message);
static	void		server_close_internal 		(int refnum, const char *message, int);
static	void		server_close_soft 		(int refnum);
	void		server_close	 		(int refnum, const char *message);

	/* CRUD a server  */
	void		set_server_away_message 	(int refnum, const char *message);
	const char *	get_server_away_message 	(int refnum);
	void		set_server_away_status 		(int refnum, int status);
	int		get_server_away_status 		(int refnum);

	const char *	get_server_umode 		(int refnum);
static	void		set_user_mode 			(int refnum, int mode);
static	void		unset_user_mode 		(int refnum, int mode);
static	void		clear_user_modes 		(int refnum);
	void		update_server_umode 		(int refnum, const char *modes);
static	void		reinstate_user_modes 		(void);

	int		get_server_operator 		(int refnum);
	int		get_server_ssl_enabled 		(int refnum);
	const char *	get_server_ssl_cipher 		(int refnum);

static	const char *	get_server_password 		(int refnum);
static	void		set_server_password 		(int refnum, const char *password);
	void		password_sendline 		(void *data, const char *line);
	int		is_me 				(int refnum, const char *nick);

static	void		set_server_port 		(int refnum, int port);
	int		get_server_port 		(int refnum);
	int		get_server_local_port 		(int refnum);
static	const char *	get_server_remote_paddr 	(int refnum);
static	SSu		get_server_remote_addr 		(int refnum);
	SSu		get_server_local_addr 		(int refnum);
static	void		set_server_userhost 		(int refnum, const char *uh);
	const char *	get_server_userhost 		(int refnum);
	void		use_server_cookie 		(int refnum);

	const char *	get_server_nickname 		(int refnum);
	void		change_server_nickname 		(int refnum, const char *nick);
	const char *	get_pending_nickname 		(int refnum);
	void		accept_server_nickname 		(int refnum, const char *nick);
	void		nickname_change_rejected 	(int refnum, const char *mynick);
static	void		reset_nickname 			(int refnum);
	void		set_server_unique_id 		(int servref, const char * id);

static	void		set_server_state 		(int refnum, int new_state);
	const char *	get_server_state_str 		(int refnum);

static	void		set_server_name 		(int servref, const char * param );
	const char *	get_server_name 		(int servref);
static	const char *	get_server_host 		(int servref);
	void		set_server_group 		(int servref, const char * param );
	const char *	get_server_group 		(int servref);
static	void		set_server_server_type 		(int servref, const char * param );
static	const char *	get_server_type 		(int servref);
static	void		set_server_vhost 		(int servref, const char * param );
	const char *	get_server_vhost 		(int servref);
static	void		set_server_cert 		(int refnum, const char *cert);
	const char *	get_server_cert 		(int servref);
	void		set_server_cap_hold 		(int refnum, int value);
	int		get_server_cap_hold 		(int servref);
	const char *	get_server_itsname 		(int refnum);
	void		set_server_doing_privmsg 	(int servref, int value);
	int		get_server_doing_privmsg 	(int servref);
	void		set_server_doing_notice 	(int servref, int value);
	int		get_server_doing_notice 	(int servref);
	void		set_server_doing_ctcp 		(int servref, int value);
	int		get_server_doing_ctcp 		(int servref);
	int		get_server_protocol_state 	(int refnum);
	void		set_server_protocol_state 	(int refnum, int state);

	void		server_hard_wait 		(int i);
	void		server_passive_wait 		(int i, const char *stuff);
	int		server_check_wait 		(int refnum, const char *nick);

static	void		add_server_altname 		(int refnum, char *altname);
static	void		reset_server_altnames 		(int refnum, char *new_altnames);
static	char *		get_server_altnames 		(int refnum);
	const char *	get_server_altname 		(int refnum, int which);
static	char *		shortname 			(const char *oname);

static	void		make_options 			(int refnum);
static	void		destroy_an_option 		(OPTION_item *item);
static	void		destroy_options 		(int refnum);
static	char *		get_server_005s 		(int refnum, const char *str);
	const char *	get_server_005 			(int refnum, const char *setting);
static	OPTION_item *	new_005_item 			(int refnum, const char *setting);
	void		set_server_005 			(int refnum, char *setting, const char *value);

static	char *		get_my_fallback_userhost	(void);
static	void		got_my_userhost 		(int refnum, UserhostItem *__U(item), const char *__U(nick), const char *__U(stuff));

static	void	set_server_accept_cert 	(int, int);
static	void	set_server_vhost	(int, const char *);
static	void	set_server_itsname	(int, const char *);
static	void   	set_server_port 	(int, int);
static	void	set_server_state	(int, int);



/************************************************************************/
/*
 *	A "server configuration document" or a "server description",
 *	is a string (textual) representation of how to identify a server.
 *	This server description can either be a kwarg string or a json string.
 *
 *	A "Serverinfo" is a data structure and API that lets you work with 
 *	a server description in C code.  Serverinfo should be seen as an
 *	operational representation of a server description.
 *
 *	A "Server" is a data structure and API that lets you work with an 
 *	IRC server, whose foundation is a Serverinfo, and all of the state
 *	information about your interaction with that server.  
 *	Each Server "has a" Serverinfo
 *
 *	There are APIs that let you create a serverinfo, and use it to search
 *	for Servers, or create a new Server from it.
 */

/*
 * SERVER CONFIGURATION DOCUMENTS -- KEY=VALUE DATA STORED IN cJSON
 */
/*
 * ABOUT KWARG STRINGS
 * --------------------
 * A "kwarg string" is a colon-seperated-string representing an object having
 * a bunch of key-value pairs.
 * Leading fields are "positional arguments" and the keys are implicit
 * For example:
 *	irc.foo.com:6697
 * is the same as:
 *	HOST=irc.foo.com:PORT=6697
 *
 * Any positional argument can be omitted by leaving it blank:
 *	irc.foo.com:::yournick:group:irc-ssl
 * This is called "colon counting" and is confusing.  Not recommended.
 *
 * You can specify the key at any time, but you must continue once you start
 *	irc.foo.com:PORT=6697:TYPE=irc-ssl:NICK=yournick
 *
 * Key names are case insensitive and automatically fold to UPPERCASE.
 *
 * A "kwarg string" parses itself into a "configuration document".
 */
/*
 * ABOUT JSON STRINGS
 * -------------------
 * You can also specify a server configuration document using ordinary JSON.
 * This is probably clearer, more verbose, and less backwards compatible.
 *
 * A json string also parses itself into a "configuration document".
 */
/*
 * SERVER CONFIGURATION DOCUMENTS ARE A SET OF FIELDS
 * ---------------------------------------------------
 * You can specify any field you want in a configuration document,
 * but only certain fields are used by the base client:
 *  'HOST'     is a hostname, or an ipv4 p-addr ("192.168.0.1")
 *             or an ipv6 p-addr in square brackets ("[01::01]")
 *  'PORT'     is the port number the server is listening on (usually 6667)
 *  'PASS'     is the protocol passwd to log onto the server (usually blank)
 *  'NICK'     is the nickname you want to use on this server
 *  'GROUP'    is the server group this server belongs to
 *  'TYPE'     is the type (ugh) -- IRC or IRC-SSL
 *  'PROTO'    is the socket family, either 'tcp4' or 'tcp6' or neither.
 *  'VHOST'    is the virtual hostname to use for this connection.
 *  'CERT'     is the local certificate file (PEM) to use for SSL
 */
static	const char *fields[] = {
		"HOST",
		"PORT",
		"PASS",
		"NICK",
		"GROUP",
		"TYPE",
		"PROTO",
		"VHOST",
		"CERT",
		"LASTFIELD",
		NULL
	};

/*
 * load_cjson_from_json
 */
static int	load_cjson_from_json (const char *configdoc, cJSON **rootptr)
{
	cJSON *	x;
	size_t	consumed = 0;
	size_t	sanity;

	if (!configdoc || !rootptr)
		return -1;

	sanity = strlen(configdoc);
	if ((x = cJSON_Parse(configdoc, 0, &consumed)))
	{
		*rootptr = x;
		return 0;
	}

	if (consumed >= sanity)
		yell("server_configdoc_json_to_json: Error parsing %s at end of string", configdoc);
	else
		yell("server_configdoc_json_to_json: Error parsing %s at byte %ld [%12s]", 
				configdoc, (long)consumed, configdoc + consumed);

	return -1;
}

/* 
 * load_cjson_from_kwarg
 */
static int	load_cjson_from_kwarg (const char *configdoc, cJSON **rootptr)
{
	cJSON *	root;

	if ((root = kwarg_string_to_json(configdoc, fields)))
	{
		*rootptr = root;
		return 0;
	}

	yell("server_configdoc_kwarg_to_json: Error parsing %s", configdoc);
	return -1;
}

/*
 * load_cjson:  Create or Modify a temporary ServerInfo based on string.
 *
 * Arguments:
 *	str	A server description (configuration document).  It may be either a kwarg 
 *		string or a json object.  Other formats may be supported in the future.
 *	rootptr	A pointer to a cJSON pointer.  It may already be filled in.
 *
 * Return value:	
 *	 0	The operation completed successfully
 *	!= 0	An error of some sort occurred
 *			str was NULL
 *			rootptr was NULL
 *			The string within 'str' could not be parsed
 *
 */
static int	load_cjson (const char *str, cJSON **rootptr)
{
	char *	json_string;
	int	r;
	cJSON *	root = NULL;

	if (!str)
		panic(1, "load_cjson: str == NULL");
	if (!rootptr)
		panic(1, "load_cjson: rootptr == NULL");

	if (*str == '{')
		r = load_cjson_from_json(str, &root);
	else
		r = load_cjson_from_kwarg(str, &root);
	if (r < 0)
	{
		debug(DEBUG_KWARG_PARSE, "SERVER DESC: String %s could not be parsed", str);
		return r;
	}

	/* XXX TODO - Here is where we would "normalize" the data structure */

	json_string = cJSON_Generate(*rootptr, true_);
	debug(DEBUG_KWARG_PARSE, ">>> %s", json_string);

	new_free(&json_string);
	*rootptr = root;
	return 0;
}




/*************************************************************************/
/*
 * SERVERINFO -- A WRAPPER (and API) AROUND SERVER CONFIGURATION DOCUMENTS
 *
 * Each server has a serverinfo, which contains a server configuration document.
 *
 * You can also create your own serverinfos, which contain either a SCD or 
 * contains a bare refnum.  You can use these for lookups.
 *
 * Serverinfos are the user-facing representation of "a server".
 * Any time the user refers to a server it becomes a serverinfo,
 * and then you work on it from there.
 */

/*
 * serverinfo_clear: Initialize/Reset a ServerInfo object
 *
 * Parameters:
 *	s	- A ServerInfo that is brand new or has already
 *		  been cleansed (see below)
 *
 * Return value:
 *	0	- This function always returns 0.
 *
 * Notes:
 *	If you are resetting an existing serverinfo (ie, one that has
 *	already been in use), you must call serverinfo_free() instead.
 *	Otherwise, this will leak memory.
 */
int	serverinfo_clear (ServerInfo *s)
{
	/* DONE */
	memset(s, 0, sizeof(ServerInfo));
        s->refnum = NOSERV;

	return 0;

}

/*
 * serverinfo_load:  Create or Modify a temporary ServerInfo based on string.
 *
 * Arguments:
 *	s	A ServerInfo object.  
 *	str	A server description.  
 *
 * Notes:
 *	This function should be used for converting user input into "a server".
 *	Everything else is an internal API.
 *
 * Return value:	
 *	This function returns 0
 */
int	serverinfo_load (ServerInfo *s, const char *str)
{
	char *		after;
	cJSON *		root = NULL;

	if (!str)
		panic(1, "server_configdoc_to_serverinfo: str == NULL");

	/*
	 * As a shortcut, we allow the string to be a number,
	 * which refers to an existing server.
	 */
	if (str && is_number(str))
	{
		int	i;

		i = strtol(str, &after, 10);
		if ((!after || !*after) && get_server(i))
		{
			s->refnum = i;
			return 0;
		}
	}

	/* First the JSON part */
	load_cjson(str, &root);
	s->root = root;

	/* Here we would "Normalize" the server document */
	/* XXX TODO */

	return 0;
}

/*
 * serverinfo_free - Destroy a permanent ServerInfo when you're done with it.
 *
 * Arguments:
 *	si	A permanent Serverinfo 
 */
static	void	serverinfo_free (ServerInfo *si)
{
	/* First the JSON part */
	cJSON_DeleteItem(&si->root);
	serverinfo_clear(si);
}



/* * * */
const char *	serverinfo_get (const SI *si, const char *key)
{
	cJSON *		x;

	if (!si)
		return NULL;

	if (!(x = cJSON_GetObjectItem(si->root, key)))
		return NULL;

	return cJSON_GetValueAsString(x);
}

bool	serverinfo_set (const SI *si, const char *key, const char *value)
{
	cJSON *		x;

	if (!si)
		return NULL;


	if (!(x = cJSON_GetObjectItem(si->root, key)))
	{
		if ((cJSON_AddStringToObject(si->root, key, value)))
			return true;
		else
			return false;
	}
	else
	{
		if (cJSON_IsObject(x) || cJSON_IsArray(x))
			return false;

		if (cJSON_ResetValueAsString(x, value) == true_)
			return true;
		else
			return false;
	}
}

const char *	get_si (int refnum, const char *field)
{
	Server *	s;
	cJSON *		x;

	if (!(s = get_server(refnum)))
		return NULL;

	if ((x = cJSON_GetObjectItem(s->info->root, field)))
		return cJSON_GetValueAsString(x);
	return NULL;
}

bool	set_si (int refnum, const char *field, const char *value)
{
	Server *	s;
	cJSON *		x;

	if (!(s = get_server(refnum)))
		return false;

	if (!(x = cJSON_GetObjectItem(s->info->root, field)))
	{
		if ((cJSON_AddStringToObject(s->info->root, field, value)))
			return true;
		else
			return false;
	}

	if (cJSON_IsObject(x) || cJSON_IsArray(x)) return false;

	cJSON_ResetValueAsString(x, value);

	return true;
}

/******************************************************************************/
/*
 * serverinfo_update_aserver - Merge/update a serverinfo into an existing server.
 *
 * Arguments:
 *	refnum	- An existing server refnum.  Obviously it should exist.
 *	new_si	- A ServerInfo that contains fields that should be updated
 *		  for 'refnum'.  It does not need to be a proper ServerInfo.
 *		  New_si will not be changed.
 */
static	void	serverinfo_update_aserver (const ServerInfo *new_si, int refnum)
{
	cJSON *		x;

	if (!get_server(refnum))
		return;

	cJSON_ForEach(x, new_si->root)
	{
		set_si(refnum, x->name, x->valuestring);
	}

	return;
}

/*
 * serverinfo_matches_servref - See if a query serverinfo could describe a server
 *
 * Arguments:
 *	si	A query ServerInfo previously passed to server_configdoc_to_serverinfo.
 *	servref	A server that might be correctly described by 'si'.
 *
 * Return value:
 *	The server refnum of the server that matches 'si',
 *	or NOSERV if there is no server that matches.
 */
int	serverinfo_matches_servref (ServerInfo *si, int servref)
{
	int		j;
	const char *	host_ = NULL;
	int		refnum_ = NOSERV;
	Server *	s;

	/* Our candidate server must exist */
	if (!(s = get_server(servref)))
		return 0;

	/*
	 * NOTE -- You may be asking "don't we check for the refnum
	 * in serverinfo_lookup()?  Yes we do.  But this function is
	 * called by the recoder to see if the recode rule matches
	 * this server.  So it doesn't need to go through a whole
	 * serverinfo_lookup().   Six of one, half dozen of the other...
	 */
#if 1
	/* 
	 * Trap these three cases:
	 * refnum != NOSERV, HOST == NULL	- Look for 'refnum'
	 * refnum != NOSERV, HOST is a number	- Look for 'refnum'
	 * refnum == NOSERV, HOST is a number	- Look for 'HOST'
	 * ... Otherwise, continue on below! 
	 */
	if (si->refnum != NOSERV)
		refnum_ = si->refnum;
	else
	{
		host_ = serverinfo_get(si, "HOST");
		if (host_ && is_number(host_))
		{
			refnum_ = atol(host_);
			host_ = NULL;
		}
	}

	/* Are we looking for this server specifically? */
	if (refnum_ != NOSERV)
	{
		if (refnum_ == servref)
			return 1;
		else
			return 0;
	}
#endif

	/*
	 * We are no longer matching against a refnum.
	 * Let's see if our host matches the server's vitals
	 */
	if (empty(get_server_host(servref)))
		return 0;

	/* If you requested a specific port, IT MUST MATCH.  */
	/* If you don't specify a port, then any port is fine */
	const char *	port__ = serverinfo_get(si, "PORT");
	if (port__)
	{
		int	port_ = atol(nonull(port__));
		if (port_ != 0 && port_ != get_server_port(servref))
			return 0;
	}

	/* If you specified a password, IT MUST MATCH */
	/* If you don't specify a password, then any pass is fine */
	const char *password_ = serverinfo_get(si, "PASS");
	if (password_ && empty(get_server_password(servref)))
		return 0;
	if (password_ && !wild_match(password_, get_server_password(servref)))
		return 0;

	/*
	 * At this point, we're looking to match your provided
	 * host against something reasonable.
	 *  1. The "ourname"  (the internet hostname)
	 *  2. The "itsname"  (the server's logical hostname on
	 *		       irc, which may or may not have anything
	 *		       to do with its internet hostname)
	 *  3. The Server Group
	 *  4. Any "altname"
	 *
	 * IMPORTANT! All of the above do WILDCARD MATCHING, so 
	 * that means hostname like "*.undernet.org" will match
	 * on an undernet server, even if you don't know the 
	 * exact name!
	 *
	 * IMPORTANT -- Please remember -- the lowest numbered 
	 * refnum that matches ANY of the four will be our winner!
	 * That means if server refnum 0 has an altname of 
	 * "booya" and server refnum 1 has a group of "booya",
	 * then server 0 wins!
	 */

	if (host_ && get_server_host(servref) && wild_match(host_, get_server_host(servref)))
		return 1;

	if (host_ && get_server_itsname(servref) && wild_match(host_, get_server_itsname(servref)))
		return 1;

	if (host_ && !empty(get_server_group(servref)) && wild_match(host_, get_server_group(servref)))
		return 1;

	if (host_ && get_server_005(servref, "NETWORK") && wild_match(host_, get_server_005(servref, "NETWORK")))
		return 1;

	for (j = 0; j < s->altnames->numitems; j++)
	{
		if (!s->altnames->list[j].name)
			continue;

		if (host_ && wild_match(host_, s->altnames->list[j].name))
			return 1;
	}

	return 0;
}


/*
 * serverinfo_lookup - Convert a query serverinfo into a server refnum
 *			   Returns the FIRST server that seems a match.
 *
 * Arguments:
 *	si	A temporary ServerInfo previously passed to server_configdoc_to_serverinfo.
 * Return value:
 *	The first server refnum that matches 'si',
 *	or NOSERV if there is no server that matches.
 * Notes:
 *	If this function returns NOSERV, you can call serverinfo_to_newserv()
 *	to add the ServerInfo to the server list.
 */
static	int	serverinfo_lookup (ServerInfo *si)
{
	int	i, opened;

	if (si->refnum != NOSERV && get_server(si->refnum) != NULL)
		return si->refnum;

	if (!serverinfo_get(si, "HOST"))
		return NOSERV;

	for (opened = 1; opened >= 0; opened--)
	{
	    for (i = 0; i < number_of_servers; i++)
	    {
		if (is_server_open(i) != opened)
			continue;

		if (serverinfo_matches_servref(si, i))
			return i;
	    }
	}

	return NOSERV;
}

/*
 * Convert a serverdesc to a query serverinfo, 
 * Then look up the query serverinfo to a servref
 */
int	serverdesc_lookup (const char *desc)
{
	char *		ptr;
	ServerInfo 	si;
	int		retval;

	/* Then the older part */
	ptr = LOCAL_COPY(desc);
	serverinfo_clear(&si);
	if (serverinfo_load(&si, ptr))
		return NOSERV;
	retval = serverinfo_lookup(&si);

	serverinfo_free(&si);
	return retval;
}

/*
 * Convert a serverdesc to a query serverinfo, 
 * Then look up the query serverinfo to a servref
 */
int	serverdesc_upsert (const char *servdesc, int quiet)
{
	int		x;

	if ((x = serverdesc_lookup(servdesc)) != NOSERV)
	{
		serverdesc_update_aserver(servdesc, x);
		if (!quiet)
			say("Server [%d] updated with [%s]", x, servdesc);
	}
	else
	{
		x = serverdesc_insert(servdesc);
		if (!quiet)
			say("Server [%s] added as server %d", servdesc, x);
	}
	return x;
}


/*
 * Convert a serverdesc to a query serverinfo
 * Then Merge a server's serverinfo with the query serverinfo
 */
static	int	serverdesc_update_aserver (const char *str, int refnum)
{
	ServerInfo si;

	if (!get_server(refnum))
		return NOSERV;

	/* Then the older part */
	serverinfo_clear(&si);
	serverinfo_load(&si, str);
	serverinfo_update_aserver(&si, refnum);

	serverinfo_free(&si);
	return refnum;
}

/*
 * Convert a serverdesc to a query serverinfo
 * Then lookup the server for that query serverinfo
 * Then merge that query serverinfo into that server
 */
int	serverdesc_update (const char *desc)
{
	char *		ptr;
	ServerInfo 	si;
	int		retval;

	/* Then the older part */
	ptr = LOCAL_COPY(desc);
	serverinfo_clear(&si);
	if (serverinfo_load(&si, ptr))
		return NOSERV;

	if ((retval = serverinfo_lookup(&si)) != NOSERV)
		serverinfo_update_aserver(&si, retval);

	serverinfo_free(&si);
	return retval;
}


/*
 * serverinfo_insert - Add a server to the server list
 *
 * Arguments:
 *	si	A ServerInfo that should be added to the global server list.
 *		The new server makes a copy of this ServerInfo, so you are
 *		still responsible for cleaning up 'si' after calling.
 * Notes:
 *	You are responsible for ensuring that 'si' does not conflict with an
 *	already existing server.  You should not call this function unless
 *	serverinfo_lookup(si) == NOSERV.  Adding duplicate servers to the 
 *	server list results in undefined behavior.
 */
static	int	serverinfo_insert (ServerInfo *si)
{
	int		i;
	Server *	s;

	for (i = 0; i < number_of_servers; i++)
		if (server_list[i] == NULL)
			break;

	if (i == number_of_servers)
	{
		number_of_servers++;
		RESIZE(server_list, Server *, number_of_servers);
	}

	s = server_list[i] = new_malloc(sizeof(Server));

	s->info = (ServerInfo *)new_malloc(sizeof(ServerInfo));
	serverinfo_clear(s->info);
	/* 
	 * We must do a deep copy because we don't know the
	 * provenance of 'si' -- it might be a local variable
	 * on the stack of the caller; but we must ensure
	 * it is on the heap under our exclusive control.
	 */
	s->info->root = cJSON_Duplicate(si->root, true_);

	s->altnames = new_bucket();
	add_to_bucket(s->altnames, shortname(serverinfo_get(si, "HOST")), NULL);

	s->itsname = (char *) 0;
	s->away_message = (char *) 0;
	s->away_status = 0;
	s->version_string = (char *) 0;
	s->des = -1;
	s->state = SERVER_CREATED;
	s->nickname = (char *) 0;
	s->s_nickname = (char *) 0;
	s->d_nickname = (char *) 0;
	s->unique_id = (char *) 0;
	s->userhost = (char *) 0;
	s->line_length = IRCD_BUFFER_SIZE;
	s->max_cached_chan_size = -1;
	s->who_queue = NULL;
	s->ison_len = 500;
	s->ison_max = 1;
	s->ison_queue = NULL;
	s->ison_wait = NULL;
	s->userhost_max = 1;
	s->userhost_queue = NULL;
	s->userhost_wait = NULL;
	memset(&s->local_sockname.ss, 0, sizeof(s->local_sockname.ss));
	memset(&s->remote_sockname.ss, 0, sizeof(s->remote_sockname.ss));
	s->remote_paddr = NULL;
	s->cookie = NULL;
	s->quit_message = NULL;
	s->umode[0] = 0;
	s->addrs = NULL;
	s->autoclose = 1;
	s->default_realname = NULL;
	s->realname = NULL;
	s->any_data = 0;
	s->cap_hold = 0;

	s->protocol_metadata = 0;
	s->doing_privmsg = 0;
	s->doing_notice = 0;
	s->doing_ctcp = 0;
	s->waiting_in = 0;
	s->waiting_out = 0;
	s->start_wait_list = NULL;
	s->end_wait_list = NULL;

	s->invite_channel = NULL;
	s->joined_nick = NULL;
	s->public_nick = NULL;
	s->recv_nick = NULL;
	s->sent_nick = NULL;
	s->sent_body = NULL;

	s->stricmp_table = 1;		/* By default, use rfc1459 */

	const char *nick_ = get_si(i, "NICK");
	if (!empty(nick_))
		malloc_strcpy(&s->d_nickname, nick_);
	else
		malloc_strcpy(&s->d_nickname, nickname);

	make_options(i);

	set_server_state(i, SERVER_RECONNECT);
	return i;
}

/*
 * Convert a serverdesc into a canonical serverinfo
 * Then convert that canonical serverinfo into a servref
 */
int	serverdesc_insert (const char *desc)
{
	char *		ptr;
	ServerInfo 	si;
	int		retval;

	ptr = LOCAL_COPY(desc);
	serverinfo_clear(&si);
	if (serverinfo_load(&si, ptr))
		return NOSERV;

	retval = serverinfo_insert(&si);
	return retval;
}

/*
 * Treat each line in a file as a serverdesc; add them all.
 */
static int 	serverdesc_import_file (const char *file_path)
{
	Filename	expanded;
	FILE *		fp;
	char		buffer[BIG_BUFFER_SIZE + 1];
	char *		defaultgroup = NULL;

	if (normalize_filename(file_path, expanded))
		return -1;

	if (!(fp = fopen(expanded, "r")))
		return -1;

	while (fgets(buffer, BIG_BUFFER_SIZE, fp))
	{
		chop(buffer, 1);
		if (*buffer == '#')
			continue;
		else if (*buffer == '[')
		{
		    char *p;
		    if ((p = strrchr(buffer, ']')))
			*p++ = 0;
		    malloc_strcpy(&defaultgroup, buffer + 1);
		}
		else if (*buffer == 0)
			continue;
		else
			serverdesc_insert_with_group(buffer, defaultgroup);
	}

	fclose(fp);
	new_free(&defaultgroup);
	return 0;
}

/*
 * Determine the default server file, and load it
 */
int 	serverdesc_import_default_file (void)
{
	Filename	file_path;
	char *		clang_is_frustrating;

	if ((clang_is_frustrating = getenv("IRC_SERVERS_FILE")))
		strlcpy(file_path, clang_is_frustrating, sizeof file_path);
	else
	{
#ifdef SERVERS_FILE
		*file_path = 0;
		if (SERVERS_FILE[0] != '/' && SERVERS_FILE[0] != '~')
			strlcpy(file_path, irc_lib, sizeof file_path);
		strlcat(file_path, SERVERS_FILE, sizeof file_path);
#else
		return -1;
#endif
	}

	return serverdesc_import_file(file_path);
}

/*
 * serverdesc_insert_with_group: space-separated list of server descs to the server list.
 *	If the server description does not set a port, use the default port.
 *	If the server description does not set a group, use the provided group.
 *  This function modifies "servers".
 */
int	serverdesc_insert_with_group (const char *servers_, const char *group)
{
	ServerInfo 	si;
	int		refnum;
	char *		servers;
	int		retval;

	if (!servers_)
		return NOSERV;
	servers = LOCAL_COPY(servers_);

	serverinfo_clear(&si);
	serverinfo_load(&si, servers);

	/* Then the older part */
	const char *group_ = serverinfo_get(&si, "GROUP");
	if (group && group_ == NULL)
		serverinfo_set(&si, "GROUP", group);

	refnum = serverinfo_lookup(&si);
	if (refnum == NOSERV)
		retval = serverinfo_insert(&si);
	else
	{
		serverinfo_update_aserver(&si, refnum);
		retval = refnum;
	}

	serverinfo_free(&si);
	return retval;
}



/***************************************************************************/

/***************************************************************************/



void	server_list_remove_all (void)
{
	int	i;

	for (i = 0; i < number_of_servers; i++)
		server_list_remove(i);
	new_free((char **)&server_list);
}

static 	void 	server_list_remove (int i)
{
	Server  *s;
	int	count, j;

	if (!(s = get_server(i)))
		return;

	/* Count up how many servers are left. */
	for (count = 0, j = 0; j < number_of_servers; j++)
		if (get_server(j))
			count++;

	if (count == 1 && !dead)
	{
		say("You can't delete the last server!");
		return;
	}

	say("Deleting server [%d]", i);
	set_server_state(i, SERVER_DELETED);

	clean_server_queues(i);
	new_free(&s->itsname);
	new_free(&s->away_message);
	new_free(&s->version_string);
	new_free(&s->nickname);
	new_free(&s->s_nickname);
	new_free(&s->d_nickname);
	new_free(&s->unique_id);
	new_free(&s->userhost);
	new_free(&s->cookie);
	new_free(&s->quit_message);
	new_free(&s->invite_channel);
	new_free(&s->joined_nick);
	new_free(&s->public_nick);
	new_free(&s->recv_nick);
	new_free(&s->sent_nick);
	new_free(&s->sent_body);
	new_free(&s->default_realname);
	new_free(&s->remote_paddr);
	destroy_options(i);
	reset_server_altnames(i, NULL);
	free_bucket(&s->altnames);
	serverinfo_free(s->info);
	new_free(&s->info);

	new_free(&server_list[i]);
	s = NULL;
}


/*****************************************************************************/

/* server_list_display: just guess what this does */
void 	server_list_display (void)
{
	int	i;

	if (!server_list)
	{
		say("The server list is empty");
		return;
	}

	if (from_server != NOSERV && (get_server(from_server)))
		say("Current server: %s %d", get_server_host(from_server), get_server_port(from_server));
	else
		say("Current server: <None>");

	if (primary_server != NOSERV && (get_server(primary_server)))
		say("Primary server: %s %d", get_server_host(from_server), get_server_port(from_server));
	else
		say("Primary server: <None>");

	say("Server list:");
	for (i = 0; i < number_of_servers; i++)
	{
		Server *	s;

		if (!(s = get_server(i)))
			continue;

		/*
		 * XXX Ugh.  I should build this up bit by bit.
		 */
		if (!s->nickname)
			say("\t%d) %s %d [%s] %s [%s] (vhost: %s)",
				i, get_server_host(i), get_server_port(i),
				get_server_group(i), get_server_type(i),
				get_server_state_str(i),
				get_server_vhost(i));
		else if (is_server_open(i))
			say("\t%d) %s %d (%s) [%s] %s [%s] (vhost: %s)",
				i, get_server_host(i), get_server_port(i),
				s->nickname, get_server_group(i),
				get_server_type(i),
				get_server_state_str(i),
				get_server_vhost(i));
		else
			say("\t%d) %s %d (was %s) [%s] %s [%s] (vhost: %s)",
				i, get_server_host(i), get_server_port(i),
				s->nickname, get_server_group(i),
				get_server_type(i),
				get_server_state_str(i),
				get_server_vhost(i));
	}
}

char *	server_list_to_string (void)
{
	Server	*s;
	int	i;
	char	*buffer = NULL;

	for (i = 0; i < number_of_servers; i++)
	{
		if (!(s = get_server(i)))
			continue;

		if (s->des != -1)
		    malloc_strcat_wordlist(&buffer, space, get_server_itsname(i));
	}

	RETURN_MSTR(buffer);
}

/* server_list_size: returns the number of servers in the server list */
int 	server_list_size (void)
{
	return number_of_servers;
}

/* 
 * Look for another server in the same group as 'oldserv'
 * Direction is 1 to go forward, -1 to go backward. 
 * Other values will lose.
 */
static int	next_server_in_group (int oldserv, int direction)
{
	int	newserv;
	int	counter;

	for (counter = 1; counter <= number_of_servers; counter++)
	{
		/* Starting with 'oldserv', move in the given direction */
		newserv = oldserv + (counter * direction);

		/* Make sure the new server is always a valid servref */
		while (newserv < 0)
			newserv += number_of_servers;

		/* Make sure the new server is valid. */
		if (newserv >= number_of_servers)
			newserv %= number_of_servers;

		/* If there is no server at this refnum, skip it. */
		if (!get_server(newserv))
			continue;

		if (!my_stricmp(get_server_group(oldserv),
			        get_server_group(newserv)))
			return newserv;
	}
	return oldserv;		/* Couldn't find one. */
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
	int	connected_to_server = 0;	/* How many active server 
						   connections are open */
static	char    hard_wait_nick[] = "***LW***";
static	char    wait_nick[] = "***W***";

/*
 * Each server goes through various stages/states where it builds up its capabilities
 * until it is fully enabled as a conduit for IRC commands by the user.
 * Each state *generally* automatically transitions to the next state when it completes.
 * 
 * There are three quiescent states (ie, states that do not automatically advance)
 *	RECONNECT	A server in RECONNECT will stay there until a window is associated with the server
 *			Once a window is associated with a server, it advances to POLICY (was DNS)
 *	ACTIVE		A server in ACTIVE will stay there until the protocol session ends, because of
 *			1. The user requests disconnection,
 *			2. The server tells us we're being rejected,
 *			3. An EOF occurs on the underying network socket
 *	CLOSED		A server in CLOSED will stay there until the user moves it back to RECONNECT
 */
const char *server_states[14] = {
	"CREATED",		/* A server in CREATED is being built and can't be used yet. */
	"RECONNECT",		/* A server in RECONNECT can be connected to (but isn't).  Quiescent until a window points at it */
	"POLICY",		/* A server in POLICY invites scripts to modify the server description to be used */
	"DNS",			/* A server in DNS is fetching IP addresses (but doesn't have them). */
	"CONNECTING",		/* A server in CONNECTING is doing a non-blocking connect. */
	"SSL_CONNECTING",	/* A server in SSL_CONNECTING is establishing SSL on top of the socket */
	"REGISTERING",		/* A server in REGISTERING is establishing an RFC1459 session with irc server */
	"SYNCING",		/* A server in SYNCING has a RFC1459 protocol layer and is doing things like setting AWAY */
	"ACTIVE",		/* A server in ACTIVE is quiescent and can be used by the user */
	"EOF",			/* A server in EOF has received an EOF on the socket (needs to be server_close()d */
	"ERROR",		/* A server in ERROR has received an error incompatable with the connection and needs to be reset */
	"CLOSING",		/* A server in CLOSING has already failed, and we are just cleaning up state */
	"CLOSED",		/* A server in CLOSED is "cleaned" and quiescent - you need to reset to RECONNECT */
	"DELETED"		/* A server in DELETED is being torn down and can't be used any more */
};



/*************************** SERVER STUFF *************************/
/*
 * server: the /SERVER command. Read the SERVER help page about 
 *
 * /SERVER
 *	Show the server list.
 * /SERVER -ADD <desc>
 *	Add server <desc> to server list.
 *	Fails if you do not give it a <desc>
 * /SERVER -UPDATE <refnum> <desc>
 * /SERVER -UPSERT <refnum> <desc>
 * /SERVER -DELETE <refnum|desc>
 *	Remove server <refnum> (or <desc>) from server list.
 *	Fails if you do not give it a refnum or desc.
 *	Fails if server does not exist.
 * 	Fails if server is open.
 * /SERVER <refnum|desc>
 *	Switch windows from current server to another server.
 */
BUILT_IN_COMMAND(servercmd)
{
	char	*server = NULL;
	int	i;
	int	olds, news;
	char *	shadow;
	size_t	slen;

	/*
	 * This is a new trick I'm testing out to see if it works
	 * better than the hack I was using.
	 */
	shadow = LOCAL_COPY(args);
	if ((server = next_arg(shadow, &shadow)) == NULL)
	{
		server_list_display();
		return;
	}

	slen = strlen(server);
	if (slen > 1 && !my_strnicmp(server, "-HELP", slen))
	{
		say("Usage: /SERVER host:port:...");
		say("            Upsert a server (and connect to it)");
		say("            This will supplant your current server");
		say("       /SERVER -ADD host:port:...");
		say("            Upsert a server (but don't connect to it)");
		say("       /SERVER -UPDATE refnum host:port:...");
		say("            Update existing server by refnum");
		say("            You can't update a server you're connected to");
		say("       /SERVER -DELETE refnum");
		say("            Delete a server (so you can't use it)");
		say("            You can't delete a server you're connected to");
		return;
	}

	/*
	 * SERVER -ADD <host>                   Add a server to list
	 */
	if (slen > 1 && !my_strnicmp(server, "-ADD", slen))
	{
		char *	servdesc;

		next_arg(args, &args);		/* Skip -ADD */
		if (!(servdesc = new_next_arg(args, &args)))
			goto add_usage_error;
		from_server = serverdesc_upsert(servdesc, 0);
		return;

add_usage_error:
		say("Usage: /SERVER -ADD serverdesc");
		say(" This will create or update an existing server");
	}

	/*
	 * SERVER -UPDATE <refnum> <desc>		Change a server
	 */
	if (slen > 1 && !my_strnicmp(server, "-UPDATE", slen))
	{
		int	servref;
		char *	serverdesc;

		next_arg(args, &args);		/* Skip -UPDATE */
		if (!(server = new_next_arg(args, &args)))
			goto update_usage_error;
		if (!is_number(server))
			goto update_usage_error;
		servref = atol(server);
		if (!(serverdesc = new_next_arg(args, &args)))
			goto update_usage_error;

		if (is_server_open(servref))
		{
			say("Updating a server that is open would lead to configuration info being wrong");
			return;
		}

		serverdesc_update_aserver(serverdesc, servref);
		say("Server %d description updated", servref);
		return;

update_usage_error:
		say("Usage: /SERVER -UPDATE refnum serverdesc");
		say(" refnum must exist; serverdesc must make sense");
		return;
	}

	/*
	 * /SERVER -DELETE <refnum>             Delete a server from list
	 */
	if (slen > 1 && !my_strnicmp(server, "-DELETE", slen))
	{
		next_arg(args, &args);		/* Skip -DELETE */
		if (!(server = new_next_arg(args, &args)))
			goto delete_usage_error;
		if ((i = serverdesc_lookup(server)) == NOSERV)
			goto delete_usage_error;
		if (is_server_open(i))
			goto delete_usage_error;
		server_list_remove(i);
		return;

delete_usage_error:
		say("Usage: /SERVER -DELETE refnum");
		say(" refnum must exist; the server must be disconnected");
		return;
	}

	/* * * * The rest of these actually move windows around * * * */
	olds = from_server;

	if ((news = serverdesc_update(server)) == NOSERV)
	{
	    if ((news = serverdesc_insert(server)) == NOSERV)
	    {
		say("I can't parse server description [%s]", server);
		return;
	    }
	}

	/* Always unconditionally allow new server to auto-reconnect */
	if (!is_server_registered(news))
	{
		say("Reconnecting to server %d", news);
		set_server_state(news, SERVER_RECONNECT);
	}

	/* If the user is not actually changing server, just reassure them */
	if (olds == news)
	{
		say("This window is associated with server %d (%s:%d)",
			olds, get_server_name(olds), get_server_port(olds));
	}
	else
	{
		/* Do the switch! */
		set_server_quit_message(olds, "Changing servers");
		change_window_server(olds, news);
	}
}


/* SERVER INPUT STUFF */
/*
 * server_io: A callback suitable for use with new_open() to handle servers
 *
 * When new_open() determines that new data is ready for a file descriptor,
 * it calls the callback function registered with that file descriptor.
 * This is the function that should be used for every server fd.
 *
 * It will:
 *  1) Determine what state the file descriptor is in 
 *  2) De-queue the next chunk of data from the server (which is state
 *     dependent -- ie, in DNS, it's a serialized IP address.  In CONNECTED, 
 *     it's a line of text terminated by \r\n).
 *  3) Take the appropriate action based on the state and data received.
 *
 * It might have been reasonable to have several different callbacks -- one for
 * each server state, and it might have been reasonable to implement each 
 * server state as a separate function that gets called from here; but I chose 
 * to implement it as one monolithic function.  There is nothing special about 
 * doing it one way or the other.
 */
static	void	server_io (int fd)
{
	Server *s;
	char	buffer[IO_BUFFER_SIZE + 1];
	int	des,
		i, l;
	char *extra = NULL;
	int	found = 0;

	for (i = 0; i < number_of_servers; i++)
	{
		ssize_t	junk;
		char 	*bufptr = buffer;
		int	retval = 0;

		if (!(s = get_server(i)))
			continue;

		if ((des = s->des) < 0)
			continue;		/* Nothing to see here, */

		if (des != fd)
			continue;		/* Move along. */

		found = 1;			/* We found it */

		from_server = i;
		l = set_context(from_server, -1, NULL, NULL, LEVEL_OTHER);

		/* - - - - */
		/*
		 * Is the dns lookup finished?
		 * Handle DNS lookup responses from the dns helper.
		 * Remember that when we start up a server connection,
		 * s->des points to a socket connected to the dns helper
		 * which feeds us Getaddrinfo() responses.  We then use
		 * those reponses, to establish nonblocking connect()s
		 * [the call to server_connect_next_addr() below], which replaces
		 * s->des with a new socket connecting to the server.
		 */
		if (s->state == SERVER_DNS)
		{
			char	result_json[10240];
			ssize_t	len;
			int	failure_code = 0;

			*result_json = 0;
			len = dgets(s->des, result_json, sizeof(result_json), 0);
			if (len < 0)
			{
				debug(DEBUG_SERVER_CONNECT, "server_io: Something went very wrong with the dns response on %d - %ld", s->des, len);
				say("An unexpected DNS lookup error occurred.");
				s->des = new_close(s->des);
				set_server_state(i, SERVER_ERROR);
				server_close(i, NULL);
			}

			s->addrs_total = json_to_sockaddr_array(result_json, &failure_code, &s->addrs);
			s->addr_counter = 0;
			if (failure_code != 0)
			{
				say("DNS lookup for server %d [%s] failed with error: %d (%s)", 
					i, get_server_host(i), failure_code, ares_strerror(failure_code));
				s->des = new_close(s->des);
				set_server_state(i, SERVER_ERROR);
				server_close(i, NULL);
			}
			else if (s->addrs_total < 0)
			{
				debug(DEBUG_SERVER_CONNECT, "server_io(%d): Something went very wrong with the json - %s", i, result_json);
				s->des = new_close(s->des);
				set_server_state(i, SERVER_ERROR);
				server_close(i, NULL);
			}
			else
			{
				debug(DEBUG_SERVER_CONNECT, "server_io(%d): I got a dns response: %s", i, result_json);
				say("DNS lookup for server %d [%s] "
					"returned (%d) addresses", 
					i, get_server_host(i), s->addrs_total);

				s->des = new_close(s->des);
				s->addr_counter = 0;
				server_connect_next_addr(i);	/* This function advances us to SERVER_CONNECTING */
			}
		}

		/* - - - - */
		/*
		 * First look for nonblocking connects that are finished.
		 */
		else if (s->state == SERVER_CONNECTING)
		{
			ssize_t c;
			SSu	 name;

			memset(&name.ss, 0, sizeof(name.ss));

			debug(DEBUG_SERVER_CONNECT, "server_io: server [%d] is now ready to write", i);

#define DGETS(x, y) dgets( x , (char *) & y , sizeof y , -1);

			/* * */
			/* This is the errno value from getsockopt() */
			c = DGETS(des, retval)
			if (c < (ssize_t)sizeof(retval) || retval)
				goto something_broke;

			/* This is the socket error returned by getsockopt() */
			c = DGETS(des, retval)
			if (c < (ssize_t)sizeof(retval) || retval)
				goto something_broke;

			/* * */
			/* This is the errno value from getsockname() */
			c = DGETS(des, retval)
			if (c < (ssize_t)sizeof(retval) || retval)
				goto something_broke;

			/* This is the address returned by getsockname() */
			c = DGETS(des, name.ss)
			if (c < (ssize_t)sizeof(name.ss))
				goto something_broke;

			/* * */
			/* This is the errno value from getpeername() */
			c = DGETS(des, retval)
			if (c < (ssize_t)sizeof(retval) || retval)
				goto something_broke;

			/* This is the address returned by getpeername() */
			c = DGETS(des, name.ss)
			if (c < (ssize_t)sizeof(name.ss))
				goto something_broke;

			/* XXX - I don't care if this is abusive.  */
			if (0)
			{
something_broke:
				if (retval)
				{
					syserr(i, "Could not connect to server [%d] "
						"address [%d] because of error: %s", 
						i, s->addr_counter, strerror(retval));
				}
				else
					syserr(i, "Could not connect to server [%d] "
						"address [%d]: (Internal error)", 
						i, s->addr_counter);

				/* 
				 * This results in trying the next IP addr 
				 * Servers that have windows pointing to them
				 * that land in CLOSED that still have IP addrs
				 * left to try, will auto-resurrect in 
				 * window_check_servers().
				 * 
				 * It is not necessary for us to take any 
				 * action here
				 */
				set_server_state(i, SERVER_ERROR);
				server_close(i, NULL);
				pop_context(l);
				continue;
			}

			memcpy(&s->remote_sockname.ss, &name.ss, sizeof(name.ss));
			s->remote_paddr = ssu_to_paddr_quick((SSu *)&name);
			say("Connected to IP address %s", s->remote_paddr);

			/*
			 * For SSL server connections, we have to take a little
			 * detour.  First we start up the ssl connection, which
			 * always returns before it completes.  Then we tell 
			 * newio to call the ssl connector when the fd is 
			 * ready, and change our state to tell us what we're 
			 * doing.
			 */
			const char *x = get_server_type(i);
			if (!my_stricmp(x, "IRC-SSL"))
			{
				/* XXX 'des' might not be both the fd and channel! */
				/* (ie, on systems where fd != channel) */
				int	ssl_err;

				ssl_err =  ssl_startup(des, des, get_server_name(i), get_server_cert(i));

				/* SSL connection failed */
				if (ssl_err == -1)
				{
					/* XXX I don't care if this is abusive. */
					syserr(i, "Could not start SSL connection to server "
						"[%d] address [%d]", 
						i, s->addr_counter);
					goto something_broke;
				}

				/* 
				 * For us, this is asynchronous.  For nonblocking
				 * SSL connections, we have to wait until later.
				 * For blocking connections, we choose to wait until
				 * later, since the return code is posted to us via
				 * dgets().
				 */
				set_server_state(i, SERVER_SSL_CONNECTING);
				new_open(des, server_io, NEWIO_SSL_CONNECT, POLLIN, 0, i);
				pop_context(l);
				break;
			}

return_from_ssl_detour:
			/*
			 * Our IO callback depends on our medium
			 */
			if (is_fd_ssl_enabled(des))
				new_open(des, server_io, NEWIO_SSL_READ, POLLIN, 0, i);
			else
				new_open(des, server_io, NEWIO_RECV, POLLIN, 0, i);

			/* Always try to fall back to the nick from the server description */
			/* This was discussed and agreed to in April 2016 */
			if (!empty(get_si(i, "NICK")))
				server_register(i, get_si(i, "NICK"));
			else
				server_register(i, s->d_nickname);
		}

		/* - - - - */
		/*
		 * Above, we did new_open(..., NEWIO_SSL_CONNECT, ...)
		 * which leads us here when the ssl stuff posts a result code.
		 * If it failed, we punt on this address and go to the next.
		 * If it succeeded, we "return" from out detour and go back
		 * to the place in SERVER_CONNECTING we left off.
		 */
		else if (s->state == SERVER_SSL_CONNECTING)
		{
			ssize_t c;

			debug(DEBUG_SERVER_CONNECT, "server_io: server [%d] finished ssl setup", i);

			c = DGETS(des, retval)
			if (c < (ssize_t)sizeof(retval) || retval)
			{
				syserr(i, "SSL_connect returned [%d]", retval);
				goto something_broke;
			}

			/* By default, we don't accept SSL Cert */
			s->accept_cert = -1;

			/* 
			 * This throws the /ON SSL_SERVER_CERT_LIST and makes
			 * the socket blocking again.
			 */
			if (ssl_connected(des) < 0)
			{
				syserr(i, "ssl_connected() failed");
				goto something_broke;
			}

			/* 
			 * For backwards compatability, if the user has already set accept_cert
			 * by here (they hooked /on server_ssl_cert) we just accept that.
			 * Otherwise, we decide what WE think.
			 */
			if (s->accept_cert == -1)
			{
				int 	verify_error,
					checkhost_error,
					self_signed_error,
					other_error;

				verify_error = get_ssl_verify_error(des);
				checkhost_error = get_ssl_checkhost_error(des);
				self_signed_error = get_ssl_self_signed_error(des);
				other_error = get_ssl_other_error(des);

				/*
				 * Policy checks for invalid ssl certs.
				 * If an SSL cert is invalid (verify_error != 0)
				 * It is for one of the three reasons
				 *   (self-signed, bad host, or other)
				 */
				if (verify_error)
				{

					/*
					 * If "other_error" is 0, then all of the errors
					 * were either self-signed or checkhost errors.
					 * We forgive those if the user said to
					 */
					if (!other_error && get_int_var(ACCEPT_INVALID_SSL_CERT_VAR))
					{
						syserr(i, "The SSL certificate for server %d has problems, "
							  "but /SET ACCEPT_INVALID_SSL_CERT is ON", i);
						s->accept_cert = 1;
					}
					else
					{
						syserr(i, "The SSL certificate for server %d is not "
							  "acceptable", i);
						s->accept_cert = 0;
					}
				}
				else
					s->accept_cert = 1;

				/*
				 * Let a script have a chance to overrule us
				 */
				do_hook(SERVER_SSL_EVAL_LIST, "%d %s %d %d %d %d %d",
								i, get_server_name(i),
								verify_error,
								checkhost_error,
								self_signed_error,
								other_error,
								s->accept_cert);

				if (s->accept_cert == 0)
				{
					syserr(i, "SSL Certificate Verification for server %d failed: (verify error: %d, checkhost error: %d, self_signed error: %d, other error: %d)", i, verify_error, checkhost_error, self_signed_error, other_error);
					goto something_broke;
				}
			}

			goto return_from_ssl_detour;	/* All is well! */
		}

		/* - - - - */
		/* Everything else is a normal read. */
		else
		{
			last_server = i;
			junk = dgets(des, bufptr, get_server_line_length(i), 1);

			/* 
			 * If we were to support encapsulating protocols, 
			 * we would do the extraction here.  In the end, 
			 * we want 'bufptr' to contain the rfc1459 message,
			 * and whatever metadata would go into other vars.
			 *
			 * XXX TODO - We need to de-couple the protocol
			 * state (to the server) from the state of the
			 * socket we use to talk to it.
			 */

			switch (junk)
			{
				case 0:		/* Sit on incomplete lines */
					break;

				case -1:	/* EOF or other error */
				{
					int	server_was_registered = is_server_registered(i);

					/* XXX Ugh. i'm going to regret this */
					if (s->any_data == 0)
					{
						int	p;
						p = get_server_port(i);
						if (p > 6690 && my_stricmp(get_server_type(i), "IRC-SSL"))
						{
							server_close(i, NULL);
							set_server_server_type(i, "IRC-SSL");
							set_server_state(i, SERVER_RECONNECT);
							say("Connection closed from %s - Trying SSL next", get_server_host(i));
							break;
						}
						else if (p <= 6690 && my_stricmp(get_server_type(i), "IRC"))
						{
							server_close(i, NULL);
							set_server_server_type(i, "IRC");
							set_server_state(i, SERVER_RECONNECT);
							say("Connection closed from %s - Trying no-SSL next", get_server_host(i));
							break;
						}
						else
						{
							say("Something went wrong with your connection to %s -- you might need to help me!", get_server_host(i));
							/* No "break" here -- fallthrough */
						}
					}

					parsing_server_index = i;
					server_is_unregistered(i);
					if (server_was_registered)
						do_hook(RECONNECT_REQUIRED_LIST, "%d", i);
					server_close(i, NULL);
					say("Connection closed from %s", get_server_host(i));
					parsing_server_index = NOSERV;

					i++;		/* NEVER DELETE THIS! */
					break;
				}

				default:	/* New inbound data */
				{
					char *end;

					end = strlen(buffer) + buffer;
					if (*--end == '\n')
						*end-- = '\0';
					if (*end == '\r')
						*end-- = '\0';

					rfc1459_any_to_utf8(bufptr, sizeof(buffer), &extra);
					if (extra)
						bufptr = extra;

					debug(DEBUG_RFC1459, "[%d] <- [%s]", s->des, bufptr);
					debug(DEBUG_INBOUND, "[%d] <- [%s]", s->des, bufptr);

					parsing_server_index = i;
					s->any_data = 1;
					/* I added this for caf. :) */
					if (do_hook(RAW_IRC_BYTES_LIST, "%s", buffer))
					{
					    /* XXX What should 2nd arg be? */
					    parse_server(bufptr, sizeof buffer);
					}
					parsing_server_index = NOSERV;

					new_free(&extra);
					break;
				}
			}
		}

		pop_context(l);
		from_server = primary_server;
	}

	if (!found)
	{
		syserr(i, "FD [%d] says it is a server but no server claims it.  Closing it", fd);
		new_close(fd);
	}
}


/* SERVER OUTPUT STUFF */
/*
 * send_to_aserver - Send a message to a specific irc server
 *
 * Arguments:
 *	refnum	- The server to send message to
 *	format 	- The message to send. (in UTF8)
 *
 * Note: Message will be translated to server's encoding before sending.
 */
void	send_to_aserver (int refnum, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsend_to_aserver_with_payload(refnum, NULL, format, args);
	va_end(args);
}

#if 0
/*
 * send_to_aserver_with_payload
 */
static void	send_to_aserver_with_payload (int refnum, const char *payload, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsend_to_aserver_with_payload(refnum, payload, format, args);
	va_end(args);
}
#endif

/*
 * send_to_server - Send a message to current irc server
 *
 * Arguments:
 *	format 	- The message to send. (in UTF8)
 *
 * Note: Message will be translated to server's encoding before sending.
 */
void	send_to_server (const char *format, ...)
{
	va_list args;
	int	server;

	if ((server = from_server) == NOSERV)
		server = primary_server;

	va_start(args, format);
	vsend_to_aserver_with_payload(server, NULL, format, args);
	va_end(args);
}

/*
 * send_to_server_with_payload
 */
void	send_to_server_with_payload (const char *payload, const char *format, ...)
{
	va_list args;
	int	server;

	if ((server = from_server) == NOSERV)
		server = primary_server;

	va_start(args, format);
	vsend_to_aserver_with_payload(server, payload, format, args);
	va_end(args);
}


/* send_to_server: sends the given info the the server */
/*
 * vsend_to_aserver - Generalized message sending to irc.
 *
 * Arguments:
 *	refnum	- The server to send message to
 *	payload	- An *ALREADY RECODED* "payload" message to append
 *	format	- The base part of the message (in UTF8)
 *	args	- Args to 'format'.
 *
 * Note: "format" + "args" will be converted to the server's encoding.
 * The resulting message will be:
 *	[format+args after conversion] :[payload as-is]
 * This allows you to send a message to someone encoded in one encoding,
 * which refering to their channel or nick as the server wants.
 */
static void 	vsend_to_aserver_with_payload (int refnum, const char *payload, const char *format, va_list args)
{
	Server *s;
	char	buffer[BIG_BUFFER_SIZE * 11 + 1]; /* make this buffer *much*
						  * bigger than needed */
	int	server_part_len;
	int	len,
		des;
	int	ofs;
	char *	extra = NULL;

	if (!(s = get_server(refnum)))
		return;

	/* No server or server is closed */
	if (refnum == NOSERV || ((des = s->des) == -1))
        {
	    if (do_hook(DISCONNECT_LIST,"No Connection to %d", refnum))
		say("You are not connected to a server, "
			"use /SERVER to connect.");
	    return;
        }

	/* No message to send */
	if (!format)
		return;


	/****************************************/
	/*
	 * 1. Press and translate the server part
	 */
	/* Keep the results short, and within reason. */
	server_part_len = vsnprintf(buffer, BIG_BUFFER_SIZE, format, args);

	/* XXX To be honest, this is so unlikely i'm not sure what to do here */
	if (server_part_len == -1)
		buffer[IRCD_BUFFER_SIZE - 200] = 0;

	if (outbound_line_mangler)
	{
	    char *s2;
	    s2 = new_normalize_string(buffer, 1, outbound_line_mangler);
	    strlcpy(buffer, s2, sizeof(buffer));
	    new_free(&s2);
	}

	outbound_recode(zero, refnum, buffer, &extra);
	if (extra)
	{
		strlcpy(buffer, extra, sizeof(buffer));
		new_free(&extra);
	}

	/****************************************/
	/*
	 * 2. Append the (already translated) payload part if necessary
	 */
	if (payload)
	{
		strlcat(buffer, " :", sizeof(buffer));
		if (outbound_line_mangler)
		{
		    char *s2;
		    s2 = new_normalize_string(payload, 1, outbound_line_mangler);
		    strlcat(buffer, s2, sizeof(buffer));
		    new_free(&s2);
		}
		else
		    strlcat(buffer, payload, sizeof(buffer));
	}

	/****************************************/
	/*
	 * Send the resulting message out
	 */
	len = strlen(buffer);
	s->sent = 1;
	if (len > (IRCD_BUFFER_SIZE - 2))
		buffer[IRCD_BUFFER_SIZE - 2] = 0;
	debug(DEBUG_RFC1459, "[%d] -> [%s]", des, buffer);
	debug(DEBUG_OUTBOUND, "[%d] -> [%s]", des, buffer);
	strlcat(buffer, "\r\n", sizeof buffer);

	/* This "from_server" hack is for the benefit of do_hook. */
	ofs = from_server;
	from_server = refnum;

	/* XXX TODO - I don't like that this is ``encoded'' rather than utf8. */
	if (do_hook(SEND_TO_SERVER_LIST, "%d %d %s", from_server, des, buffer))
		send_to_aserver_raw(refnum, strlen(buffer), buffer);
	from_server = ofs;
}

void	send_to_aserver_raw (int refnum, size_t len, const char *buffer)
{
	Server *s;
	int des;
	int err = 0;

	if (!(s = get_server(refnum)))
		return;

	if ((des = s->des) != -1 && buffer)
	{
	    if (is_fd_ssl_enabled(des) == 1)
		err = ssl_write(des, buffer, len);
	    else
		err = write(des, buffer, len);

	    if (err == -1 && (!get_int_var(NO_FAIL_DISCONNECT_VAR)))
	    {
		if (is_server_registered(refnum))
		{
			say("Write to server failed.  Resetting connection.");
			set_server_state(refnum, SERVER_ERROR);
			do_hook(RECONNECT_REQUIRED_LIST, "%d", refnum);
			server_close(refnum, NULL);
		}
	    }
	}
}


/* CONNECTION/RECONNECTION STRATEGIES */
/*
 * server_bootstrap_connection - Bring a server connected to a window out of RECONNECT
 *
 * Arguments:
 *	server - A server in RECONNECT state
 *		 In the future, a server not being in RECONNECT may become an error
 *
 * Notes:
 * 	This function technically is the _only_ entry point for a server 
 * 	connection "from scratch".  It is only called by window_check_servers()
 * 	when a window observes it is associated with a server in the RECONNECT state.
 *
 *	Thus, to reconnect to a server, you put a server in a RECONNECT state.
 *	When a window notices this, it will kick off the reconnect (here)
 *
 * Return Value:
 *	-1	An error of some kind occured.
 */

int	server_bootstrap_connection (int server)
{
	debug(DEBUG_SERVER_CONNECT, "Bootstrapping server connection for server [%d]", server);
	debug(DEBUG_SERVER_CONNECT, "Inviting scripts to implement policy for server [%d]", server);
	set_server_state(server, SERVER_POLICY);

	/* Let's go ahead and get this party started! */
	return server_grab_address(server);
}

/*
 * server_grab_address -- look up all of the addresses for a hostname and
 *	save them in the Server data for later use.  Someone must free
 * 	the results when they're done using the data (usually when we 
 *	successfully connect, or when we run out of addrs)
 *
 *	After the server addresses are grabbed, server_io() will automatically
 * 	kick off server_connect_next_addr(), the next function in the chain.
 */
static	int	server_grab_address (int server)
{
	Server *s;
	int	family;
	int	xfd[2];

	debug(DEBUG_SERVER_CONNECT, "Grabbing server addresses for server [%d]", server);

	if (!(s = get_server(server)))
	{
		say("Server [%d] does not exist -- "
			"cannot do hostname lookup", server);
		return -1;		/* XXXX */
	}

	if (s->addrs)
	{
	        debug(DEBUG_SERVER_CONNECT, "This server still has addresses left over from "
			 "last time.  Starting over anyways...");
		server_discard_dns(server);
	}

	set_server_state(server, SERVER_DNS);

	say("Performing DNS lookup for [%s] (server %d)", get_server_host(server), server);
	xfd[0] = xfd[1] = -1;
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, xfd))
		yell("socketpair: %s", strerror(errno));
	new_open(xfd[1], server_io, NEWIO_READ, POLLIN, 1, server);

	const char *	pr = get_si(server, "PROTO");

	if (empty(pr))
		family = AF_UNSPEC;
	else if (!my_stricmp(pr, "0")
	      || !my_stricmp(pr, "any") 
	      || !my_stricmp(pr, "ip") 
	      || !my_stricmp(pr, "tcp") )
		family = AF_UNSPEC;
	else if (!my_stricmp(pr, "4")
	      || !my_stricmp(pr, "tcp4") 
	      || !my_stricmp(pr, "ipv4") 
	      || !my_stricmp(pr, "v4") 
	      || !my_stricmp(pr, "ip4") )
		family = AF_INET;
	else if (!my_stricmp(pr, "6")
	      || !my_stricmp(pr, "tcp6") 
	      || !my_stricmp(pr, "ipv6") 
	      || !my_stricmp(pr, "v6") 
	      || !my_stricmp(pr, "ip6") )
		family = AF_INET6;
	else
		family = AF_UNSPEC;

	hostname_to_json(xfd[0], family, get_server_host(server), ltoa(get_server_port(server)), 0);
	s->des = xfd[1];
	return 0;
}

/*
 * Make an attempt to connect to the next server address that will 
 * receive us.  This is the guts of "connectory", but "connectory" is
 * completely self-contained, and we have to be able to support looping
 * through getaddrinfo() results, possibly on multiple asynchronous
 * occasions.  So what we do is restart from where we left off before.
 */
static int	server_connect_next_address_internal (int server)
{
	Server *s;
	int	fd = -1;
	SSu	localaddr;
	socklen_t locallen = 0;
	char	p_addr[256];
	char	p_port[24];

	if (!(s = get_server(server)))
	{
		syserr(-1, "server_connect_next_address_internal: "
				"Server %d doesn't exist", server);
		return -1;
	}

	if (!s->addrs)
	{
		syserr(server, "server_connect_next_address_internal: "
				"There are no more addresses available "
				"for server %d", server);
		return -1;
	}

	for (; s->addr_counter < s->addrs_total; s->addr_counter++)
	{
		SSu 		addr;
		int 		family_;
		socklen_t	socklen_;
		int		port_;

		family_ = family(&s->addrs[s->addr_counter]);
		socklen_ = socklen(&s->addrs[s->addr_counter]);

		memset(&addr.ss, 0, sizeof(addr.ss));

		debug(DEBUG_SERVER_CONNECT, "Trying to connect to server %d using address [%d] and family [%d]",
				server, s->addr_counter, family_);

		memset(&localaddr, 0, sizeof(localaddr));
		if (get_default_vhost(family_, get_server_vhost(server), &localaddr, &locallen))
		{
			syserr(server, "server_connect_next_address_internal: "
				"Can't use address [%d]  because I can't get "
				"vhost for family [%d]",
				 s->addr_counter, family_);
			continue;
		}

		/* 
		 * NOW, HERE....
		 *
		 * DNS lookups may have given us a port number but that is not
		 * necessarily the port we want to use.  We must sanity check
		 * things here to prevent the user from futility.
		 */
		setssuport(&s->addrs[s->addr_counter], get_server_port(server));
		port_ = ssuport(&s->addrs[s->addr_counter]);

		const char *x = coalesce(get_server_type(server), "IRC");
		if (!my_stricmp(x, "IRC-SSL"))
		{
			if (port_ == 6667)
			{
				yell("Server %d is set to use SSL but is not using an SSL port. Fixing.", server);
				set_server_port(server, 6697);
				setssuport(&s->addrs[s->addr_counter], 6697);
			}
		}
		if (!my_stricmp(x, "IRC"))
		{
			if (port_ == 6697)
			{
				yell("Server %d is not set to use SSL but is using an SSL port. Fixing.", server);
				set_server_port(server, 6667);
				setssuport(&s->addrs[s->addr_counter], 6667);
			}
		}


		if ((fd = network_client(&localaddr, locallen, &s->addrs[s->addr_counter], socklen_)) < 0)
		{
			syserr(server, "server_connect_next_address_internal: "
				"network_client() failed for server %d address [%d].",
			server, s->addr_counter);
			continue;
		}

		if (ssu_to_paddr(&s->addrs[s->addr_counter], p_addr, 256, p_port, 24, NI_NUMERICHOST))
			say("Connecting to server refnum %d (%s), using address %d",
				server, get_server_host(server), s->addr_counter);
		else
			say("Connecting to server refnum %d (%s), "
				"using address %d (%s:%s)",
				server, get_server_host(server),
				s->addr_counter, p_addr, p_port);

		s->addr_counter++;
		return fd;
	}

	say("I'm out of addresses for server %d so I have to stop.", server);
	server_discard_dns(server);
	return -1;
}

/*
 * server_connect_next_adrr:  Supervision of a new network connection to server
 * 
 * Arguments:
 * 	new_server - A server refnum which has previously successfully completed a DNS lookup
 *			(ie, server_grab_addresses())
 *
 * This function is used by exactly two places:
 *	1) server_io() after a DNS lookup has completed
 *	2) /RECONNECT (disconnectcmd()) when a user wants to give up on a connection
 * The results of those two cases is "please try the next IP address"
 *
 * This function catches these errant initial conditions:
 *	1) You are trying to connect to an invalid server refnum
 *	2) The server is already open [this should be impossible, so we tell the user to /RECONNECT]
 * 
 * The function sets the server to CONNECTING and kicks off a nonblocking connect
 * (with server_connect_next_address_internal(), which should only be called here!
 *  (and so maybe should actually be included in this function))
 * Then it handles some administrative book-keeping and returns.
 *
 * If for any reason this function cannot succeed to produce a nonblocking connection,
 * we recommend the user do a /RECONNECT,
 */
int 	server_connect_next_addr (int new_server)
{
	int 		des;
	socklen_t	len;
	Server *	s;

	/*
	 * Can't connect to refnum -1, this is definitely an error.
	 */
	if (!(s = get_server(new_server)))
	{
		say("Connecting to server %d.  That makes no sense.", 
			new_server);
		return -1;		/* XXXX */
	}

	/*
	 * If we are already connected to the new server, go with that.
	 */
	if (s->des != -1)
	{
		say("Network connection to server %d at %s:%d is already open (state [%s])",
				new_server, get_server_host(new_server), get_server_port(new_server),
				server_states[s->state]);
		say("Use /RECONNECT -FORCE if this connection is stuck");
		from_server = new_server;
		return -1;		/* Server is already connected */
	}

	/*
	 * Make an attempt to connect to the new server.
	 * XXX I am not sure all these should be done _here_.
	 */
	set_server_state(new_server, SERVER_CONNECTING);
	errno = 0;				/* XXX And why do we need to reset errno? */
	memset(&s->local_sockname.ss, 0, sizeof(s->local_sockname.ss));
	memset(&s->remote_sockname.ss, 0, sizeof(s->remote_sockname.ss));

	/*
	 * Get a nonblocking connect going
	 */
	if ((des = server_connect_next_address_internal(new_server)) < 0)
	{
		debug(DEBUG_SERVER_CONNECT, "new_des is %d", des);

		if ((get_server(new_server)))
		{
		    say("Unable to connect to server %d at %s:%d",
				new_server, get_server_host(new_server), get_server_port(new_server));
		}
		else
			say("Unable to connect to server %d: not a valid server refnum", new_server);

		say("Use /RECONNECT to reconnect to this server");
		set_server_state(new_server, SERVER_CLOSED);
		return -1;		/* Connect failed */
	}

	/*
	 * Now we have a valid nonblocking connect going
	 * Register the nonblocking connect with newio.
	 */
	debug(DEBUG_SERVER_CONNECT, "server_connect_next_address_internal returned [%d]", des);
	from_server = new_server;	/* XXX sigh */
	new_open(des, server_io, NEWIO_CONNECT, POLLOUT, 0, from_server);

	/* 
	 * In the past, you could not do getsockname(2) on UDSs portably.
	 * See https://pubs.opengroup.org/onlinepubs/9799919799/functions/getsockname.html
	 * Issue 8 talks about AF_UNIX and getsockname() which implies it should be supported.
	 * Per the suggestion in the above page, we memset the sockname to 0 first.
	 */
	memset(&s->local_sockname.ss, 0, sizeof(s->local_sockname.ss));
	len = sizeof(s->local_sockname.ss);
	getsockname(des, &s->local_sockname.sa, &len);

	/*
	 * Initialize all of the server_list data items
	 * XXX I am not sure all these should be done _here_.
	 */
	s->des = des;


	clean_server_queues(new_server);	/* XXX Protocol level - should be somewhere else */

	/* So we set the default nickname for a server only when we use it */
	if (!s->d_nickname)			/* XXX Protocol level - should be somewhere else */
		malloc_strcpy(&s->d_nickname, nickname);

	/*
	 * Reset everything and go on our way.
	 */
	update_all_status();
	return 0;			/* New nonblocking connection established */
}

int 	servers_close_all (const char *message)
{
	int i;

	for (i = 0; i < number_of_servers; i++)
	{
		if (!get_server(i))
			continue;
		if (message)
			set_server_quit_message(i, message);
		server_close(i, NULL);
	}

	return 0;
}

/*
 * server_discard_dns:  Connecting to a server results in a DNS lookup,
 * which returns a set of IP addresses to try.  When we successfully connect
 * to a server, then we do not need the rest of the IP addresses we have.
 * That is to say, a reconnect should prompt a new DNS lookup.
 *
 * This is only to be done when the server state switches to ACTIVE.
 * If a connection fails at any previous state, we would want to try the
 * next IP address.
 */
static void	server_discard_dns (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	new_free(&s->addrs);
	s->addrs_total = 0;
}


void	server_close (int refnum, const char *message)
{
	if (!(get_server(refnum)))
		return;

	server_close_internal(refnum, message, 0);
}

void	server_close_soft (int refnum)
{
	if (!(get_server(refnum)))
		return;

	server_close_internal(refnum, NULL, 1);
}

/*
 * server_close: Given an index into the server list, this closes the
 * connection to the corresponding server.  If 'message' is anything other
 * than the NULL or the empty_string, it will send a protocol QUIT message
 * to the server before closing the connection.
 */
void	server_close_internal (int refnum, const char *message, int soft_reset)
{
	Server *s;
	int	was_registered;
	char *  sub_format;
	char 	final_message[IRCD_BUFFER_SIZE];

	/* Make sure server refnum is valid */
	if (!(s = get_server(refnum)))
	{
		yell("Closing server [%d] makes no sense!", refnum);
		return;
	}

	*final_message = 0;
	was_registered = is_server_registered(refnum);
	set_server_state(refnum, SERVER_CLOSING);
	if (s->waiting_out > s->waiting_in)		/* XXX - hack! */
		s->waiting_out = s->waiting_in = 0;

	destroy_waiting_channels(refnum);
	destroy_server_channels(refnum);

	new_free(&s->nickname);
	new_free(&s->s_nickname);
	new_free(&s->realname);
	s->any_data = 0;
	s->cap_hold = 0;

	if (s->des == -1)
		return;		/* Nothing to do here */

	/* Which do you choose, the hard or soft option? */
	if (soft_reset)
	{
		say("Performing a soft reset on server %d", refnum);
		set_server_state(refnum, SERVER_REGISTERING);
	}
	else
	{
		if (was_registered)
		{
			if (!message)
			    if (!(message = get_server_quit_message(refnum)))
				message = "Leaving";
			sub_format = convert_sub_format(message, 's');
			snprintf(final_message, sizeof(final_message), sub_format, irc_version);
			new_free(&sub_format);

			debug(DEBUG_RFC1459, "Closing server %d because [%s]", refnum, final_message);
			debug(DEBUG_OUTBOUND, "Closing server %d because [%s]", refnum, final_message);
			if (*final_message)
				send_to_aserver(refnum, "QUIT :%s\n", final_message);

			server_is_unregistered(refnum);
		}

		do_hook(SERVER_LOST_LIST, "%d %s %s", 
				refnum, get_server_host(refnum), final_message);
		s->des = new_close(s->des);
		set_server_state(refnum, SERVER_CLOSED);
	}
}

/********************* OTHER STUFF ************************************/

/* AWAY STATUS */
/*
 * Encapsulates everything we need to change our AWAY status.
 * This improves greatly on having everyone peek into that member.
 * Also, we can deal centrally with someone changing their AWAY
 * message for a server when we're not registered to that server
 * (when we do connect, then we send out the AWAY command.)
 * All this saves a lot of headaches and crashes.
 */
void	set_server_away_message (int refnum, const char *message)
{
	Server *s;

	if (!(s = get_server(refnum)))
	{
		say("You are not connected to a server.");
		return;
	}

	if (message && *message)
	{
		if (!s->away_message || strcmp(s->away_message, message))
			malloc_strcpy(&s->away_message, message);
		if (is_server_registered(refnum))
			send_to_aserver(refnum, "AWAY :%s", message);
	}
	else
	{
		new_free(&s->away_message);
		if (is_server_registered(refnum))
			send_to_aserver(refnum, "AWAY :");
	}
}

const char *	get_server_away_message (int refnum)
{
	Server *s;

	if (refnum == NOSERV)
	{
		int	i;

		for (i = 0; i < number_of_servers; i++)
		{
			if (!(s = get_server(i)))
				continue;

			if (is_server_registered(i) && s->away_message)
				return s->away_message;
		}

		return NULL;
	}

	if (!(s = get_server(refnum)))
		return NULL;
	
	return s->away_message;
}

void	set_server_away_status (int refnum, int status)
{
	Server *s;

	if (!(s = get_server(refnum)))
	{
		say("You are not connected to a server.");
		return;
	}

	/* Prevent shenanigans with 'status' */
	if (status)
		s->away_status = 1;
	else
		s->away_status = 0;
}

int	get_server_away_status (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	return s->away_status;
}


/* USER MODES */
const char *	get_server_umode (int refnum)
{
	Server *s;
	char *	retval;

	if (!(s = get_server(refnum)))
		return empty_string;

	retval = s->umode;
	return retval;		/* Eliminates a specious warning from gcc. */
}

static void	set_user_mode (int refnum, int mode)
{
	Server *s;
	char c, *p, *o;
	char new_umodes[1024];		/* Too huge for words */
	int	i;

	if (!(s = get_server(refnum)))
		return;

	/* 
	 * 'c' is the mode that is being set
	 * 'o' is the umodes that are already set
	 * 'p' is the string that we are building that adds 'c' to 'o'.
	 */
	c = (char)mode;
	o = s->umode;
	p = new_umodes;

	/* Copy the modes in 'o' that are alphabetically less than 'c' */
	for (i = 0; o && o[i]; i++)
	{
		if (o[i] >= c)
			break;
		*p++ = o[i];
	}

	/* If 'c' is already set, copy it, otherwise add it. */
	if (o && o[i] == c)
		*p++ = o[i++];
	else
		*p++ = c;

	/* Copy all the rest of the modes */
	for (; o && o[i]; i++)
		*p++ = o[i];

	/* Nul terminate the new string and reset the server's info */
	*p++ = 0;
	strlcpy(s->umode, new_umodes, 54);
}

static void	unset_user_mode (int refnum, int mode)
{
	Server *s;
	char c, *o, *p;
	char new_umodes[1024];		/* Too huge for words */
	int	i;

	if (!(s = get_server(refnum)))
		return;

	/* 
	 * 'c' is the mode that is being deleted
	 * 'o' is the umodes that are already set
	 * 'p' is the string that we are building that adds 'c' to 'o'.
	 */
	c = (char)mode;
	o = s->umode;
	p = new_umodes;

	/*
	 * Copy the whole of 'o' to 'p', except for any instances of 'c'.
	 */
	for (i = 0; o && o[i]; i++)
	{
		if (o[i] != c)
			*p++ = o[i];
	}

	/* Nul terminate the new string and reset the server's info */
	*p++ = 0;
	strlcpy(s->umode, new_umodes, 54);
}

static void 	clear_user_modes (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	*s->umode = 0;
}

void	update_server_umode (int refnum, const char *modes)
{
	int		onoff = 1;

	for (; *modes; modes++)
	{
		if (*modes == '-')
			onoff = 0;
		else if (*modes == '+')
			onoff = 1;
		else if (onoff == 1)
			set_user_mode(refnum, *modes);
		else if (onoff == 0)
			unset_user_mode(refnum, *modes);
	}
	update_all_status();
}

static void	reinstate_user_modes (void)
{
	const char *modes = get_server_umode(from_server);

	if (!modes || !*modes)
		modes = send_umode;

	if (modes && *modes)
	{
		debug(DEBUG_RFC1459, "Reinstating your user modes on server [%d] to [%s]", from_server, modes);
		debug(DEBUG_OUTBOUND, "Reinstating your user modes on server [%d] to [%s]", from_server, modes);
		send_to_server("MODE %s +%s", get_server_nickname(from_server), modes);
		clear_user_modes(from_server);
	}
}

int	get_server_operator (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	if (strchr(s->umode, 'O') || strchr(s->umode, 'o'))
		return 1;
	else
		return 0;
}


/* get_server_ssl_enabled: returns 1 if the server is using SSL connection */
int	get_server_ssl_enabled (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	return is_fd_ssl_enabled(s->des);
}

const char	*get_server_ssl_cipher (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return empty_string;
	if (!is_fd_ssl_enabled(s->des))
		return empty_string;
	return get_ssl_cipher(s->des);
}


/* CONNECTION/REGISTRATION STATUS */
void	server_register (int refnum, const char *nick)
{
	Server *	s;
	int		ofs = from_server;

	if (!(s = get_server(refnum)))
		return;

	if (get_server_state(refnum) != SERVER_CONNECTING &&
	    get_server_state(refnum) != SERVER_SSL_CONNECTING)
	{
		debug(DEBUG_SERVER_CONNECT, "Server [%d] state should be [%d] but it is [%d]", 
				refnum, SERVER_CONNECTING, 
				get_server_state(refnum));
		return;		/* Whatever */
	}

	if (is_server_registered(refnum))
	{
		debug(DEBUG_SERVER_CONNECT, "Server [%d] is already registered", refnum);
		return;		/* Whatever */
	}

	set_server_state(refnum, SERVER_REGISTERING);

	from_server = refnum;
	do_hook(SERVER_ESTABLISHED_LIST, "%s %d",
		get_server_name(refnum), get_server_port(refnum));
	from_server = ofs;

	if (!empty(get_server_password(refnum)))
	{
		char *dequoted = NULL;
		malloc_strcat_ues(&dequoted, get_server_password(refnum), "\\:");
		send_to_aserver(refnum, "PASS %s", dequoted);
		new_free(&dequoted);
	}

	malloc_strcpy(&s->realname, coalesce(s->default_realname, get_string_var(DEFAULT_REALNAME_VAR)));

	/*
	 * History note -- In the beginning RFC1459 defined USER as:
	 *	arg[0]	- Your username
	 *	arg[1]	- Your hostname
	 *	arg[2]	- Your usermode
	 *	arg[3]	- Your REALNAME
	 * But because we can't have nice things, 
	 *	arg[0]	- Replaced with your identd (if it's working)
 	 *	arg[1]	- Replaced with the reverse dns of your IP address
	 *	arg[2]	- Some servers honor it, some don't
	 *	arg[3]	- This is the only thing that matters anyways
	 *
	 * Servers do not object to providing a placeholder dot for
	 * the two fields they don't care about.
	 */
	send_to_aserver(refnum, "CAP LS 302");
	send_to_aserver(refnum, "USER %s . . :%s", 
			get_string_var(DEFAULT_USERNAME_VAR),
			s->realname);
	change_server_nickname(refnum, nick);
	debug(DEBUG_SERVER_CONNECT, "Registered with server [%d]", refnum);
}

static const char *	get_server_password (int refnum)
{
	if (!(get_server(refnum)))
		return NULL;

	return get_si(refnum, "PASS");
}

/*
 * set_server_password: this sets the password for the server with the given
 * index. If 'password' is NULL, the password is cleared
 */
static void	set_server_password (int refnum, const char *password)
{
	if (!(get_server(refnum)))
		return;

	set_si(refnum, "PASS", coalesce(password, empty_string));
}

/*
 * password_sendline: called by send_line() in get_password() to handle
 * hitting of the return key, etc 
 * -- Callback function
 */
void 	password_sendline (void *data_, const char *line)
{
	int	new_server;
	char *	data;

	if (!line || !*line)
		return;

	data = (char *)data_;
	new_server = serverdesc_lookup(data);
	new_free(&data_);

	set_server_password(new_server, line);
	server_close(new_server, NULL);
	set_server_state(new_server, SERVER_RECONNECT);
}


/*
 * is_server_open: Returns true if the given server index represents a server
 * with a live connection, returns false otherwise 
 */
int	is_server_open (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	return (s->des != -1);
}

int	is_server_registered (int refnum)
{
	int	state;

	if (!get_server(refnum))
		return 0;

	state = get_server_state(refnum);
	if (state == SERVER_SYNCING  || state == SERVER_ACTIVE)
		return 1;
	else
		return 0;
}


/*
 * Informs the client that the user is now officially registered or not
 * registered on the specified server.
 */
void  server_is_registered (int refnum, const char *itsname, const char *ourname)
{
	int	window;

	if (!get_server(refnum))
		return;

	/* 
	 * When we get an 001 (hit ACTIVE), we discard other DNS results 
	 * so that any reconnect results in a new DNS lookup.  If a failure 
	 * occurs before we hit this point, we will move to the next saved DNS 
	 * address on failure.
	 * (This is especially relevant for failed nonblocking connects)
	 */
	debug(DEBUG_SERVER_CONNECT, "We're connected! Throwing away the rest of the addrs");
	server_discard_dns(refnum);

	set_server_state(refnum, SERVER_SYNCING);

	set_server_cap_hold(refnum, 0);
	set_server_away_status(refnum, 0);
	accept_server_nickname(refnum, ourname);
	set_server_itsname(refnum, itsname);

	if ((window = get_server_current_window(refnum)) != -1)
	    if (new_server_lastlog_mask)
		renormalize_window_levels(window, *new_server_lastlog_mask);

	/*
	 * This hack is required by a race condition with freebsd that 
	 * I'm seeing on epicsol.  For reasons that I have never been able
	 * to adequately explain, if I write out data to the socket (ie,
	 * from reinstate_user_modes) at the same time as the kernel is
	 * reassembling a fractured packet (ie, the initial packet from 
	 * the server with the 001 and stuff), the kernel will refuse to 
	 * flag the socket as read()able ever again.  I've confirmed this
	 * with poll(), and everything else.
	 * Wireshark shows that the packet(s) do come in, but the kernel
	 * refuses to give them to me.  This tiny sleep eliminates the 
	 * race condition that consistently causes this problem.
	 *
	 * P.S. -- Yes, I tried different nic cards using different drivers.
	 * Yes, I've tried multiple versions of freebsd.
	 */
	my_sleep(0.005);

	reinstate_user_modes();
	userhostbase(from_server, NULL, NULL, got_my_userhost, 1);

	if (default_channel)
	{
		e_channel("JOIN", default_channel, empty_string);
		new_free(&default_channel);
	}

	if (get_server_away_message(refnum))
		set_server_away_message(from_server, get_server_away_message(from_server));

	update_all_status();
	do_hook(CONNECT_LIST, "%s %d %s", get_server_name(refnum), 
					get_server_port(refnum), 
					get_server_itsname(from_server));
	window_check_channels();
	set_server_state(refnum, SERVER_ACTIVE);
	isonbase(from_server, NULL, NULL);
}

static void	server_is_unregistered (int refnum)
{
	if (!get_server(refnum))
		return;

	destroy_options(refnum);
	set_server_away_status(refnum, 0);
	set_server_state(refnum, SERVER_EOF);
}

int	is_server_active (int refnum)
{
	if (get_server(refnum))
		return 0;

	if (get_server_state(refnum) == SERVER_ACTIVE)
		return 1;
	return 0;
}

int	is_server_valid (int refnum)
{
	if (get_server(refnum))
		return 1;
	return 0;
}


/*
 * DISCONNECT/RECONNECT - Change a server's state
 *
 * Arguments:
 *    By default, the current server will be used.
 *	<server refnum>		Disconnect or reconnect from a specific server
 *	-FORCE			Don't argue with me, just do it.
 *	-SAFE			For services: "Don't do anything that makes no sense"
 *
 * First, the server will be put into the CLOSED state
 * Then, if you did /RECONNECT the server will be put into the RECONNECT state
 */
BUILT_IN_COMMAND(disconnectcmd)
{
	char *	arg;
	const char *message;
	int	i = get_window_server(0);
	int	reconnect_ = strcmp(command, "DISCONNECT");
	int	force = 1;

	while (args && *args) 
	{
		arg = next_arg(args, &args);
		if (arg == NULL)
		{
			if ((i = from_server) == NOSERV)
				i = get_window_server(0);
			break;
		}
		else if (my_strnicmp(arg, "-FORCE", 1))
			force = 1;
		else if (my_strnicmp(arg, "-SAFE", 1))
			force = 0;
		else
		{
			i = serverdesc_lookup(arg);
			if (i == NOSERV)
			{
				say("No such server!");
				return;
			}
			break;
		}
	}

	if (!get_server(i))
	{
		if (!connected_to_server)
			goto throw_on_disconnect;

		say("%s: Unknown server %d", command, i);
		return;
	}

	if (force == 0 && reconnect_ && is_server_registered(i))
	{
		say("You cannot /RECONNECT -SAFE to a server you are actively on (%s)", get_server_itsname(i));
		say("Use /DISCONNECT first.  This is a safety valve");
		return;
	}

	if (args && *args)
		message = args;
	else if (reconnect_)
		message = "Reconnecting";
	else
		message = "Disconnecting";

	say("Disconnecting from server %s", get_server_itsname(i));
	server_close(i, message);
	update_all_status();

	if (reconnect_)
	{
		say("Reconnecting to server %s", get_server_itsname(i));

		/* 
		 * Now, why do we call "server_connect_next_addr()" 
		 * instead of setting the server state to SERVER_RECONNECT?
		 * server_connect() is the entry point for after we have
		 * done DNS lookups.  This shortcut here is how we iterate
		 * through all the DNS lookups.  However, if that fails
		 * (maybe becuase we ran out of address), then we do go
		 * ahead and reset to SERVER_RECONNECT which does a dns 
		 * lookup and restarts the process.
		 */
		if (server_connect_next_addr(i) < 0)
			set_server_state(i, SERVER_RECONNECT);
	}
	else
	{
throw_on_disconnect:
		if (!connected_to_server)
			if (do_hook(DISCONNECT_LIST, "Disconnected by user request"))
				say("You are not connected to a server, use /SERVER to connect.");
	}
} 

BUILT_IN_COMMAND(reconnectcmd)
{
	disconnectcmd(command, args, subargs);
}

/**************************************************************************/
/* Getters and setters and stuff, oh my! */

/* PORTS */
static void    set_server_port (int refnum, int port)
{
	if (!(get_server(refnum)))
		return;

	set_si(refnum, "PORT", ltoa(port));
}

/* get_server_port: Returns the connection port for the given server index */
int	get_server_port (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	if (is_server_open(refnum))
		return ssu_to_port_quick(&s->remote_sockname);

	return atol(coalesce_empty(get_si(refnum, "PORT"), ltoa(irc_port)));
}

int	get_server_local_port (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	if (is_server_open(refnum))
		return ssu_to_port_quick(&s->local_sockname);

	return 0;
}

static const char *	get_server_remote_paddr (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	if (is_server_open(refnum) && s->remote_paddr)
		return s->remote_paddr;

	return empty_string;
}


static SSu	get_server_remote_addr (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
	    panic(1, "Refnum %d isn't valid in get_server_remote_addr", refnum);

	return s->remote_sockname;
}

SSu	get_server_local_addr (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		panic(1, "Refnum %d isn't valid in get_server_local_addr", refnum);

	return s->local_sockname;
}

/* USERHOST */
static void	set_server_userhost (int refnum, const char *uh)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	malloc_strcpy(&s->userhost, uh);
}

/*
 * get_server_userhost: return the userhost for this connection to server
 */
const char	*get_server_userhost (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)) || !s->userhost)
		return get_my_fallback_userhost();

	return s->userhost;
}


/* COOKIES */
void	use_server_cookie (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	if (s->cookie)
		send_to_aserver(refnum, "COOKIE %s", s->cookie);
}


/* NICKNAMES */
/*
 * get_server_nickname: returns the current nickname for the given server
 * index 
 */
const char	*get_server_nickname (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return "<invalid server>";

	if (s->nickname)
		return s->nickname;

	return "<not registered yet>";
}

int	is_me (int refnum, const char *nick)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	if (s->nickname && nick)
		return !my_stricmp(nick, s->nickname);

	return 0;
}

/*
 * This is the function to attempt to make a nickname change.  You
 * cannot send the NICK command directly to the server: you must call
 * this function.  This function makes sure that the neccesary variables
 * are set so that if the NICK command fails, a sane action can be taken.
 *
 * If ``nick'' is NULL, then this function just tells the server what
 * we're trying to change our nickname to.  If we're not trying to change
 * our nickname, then this function does nothing.
 */
void	change_server_nickname (int refnum, const char *nick)
{
	Server *s;
	const char *id;

	if (!(s = get_server(refnum)))
		return;			/* Uh, no. */

	if (nick)
	{
		/* If changing to our Unique ID, the default nickname is 0 */
		id = get_server_unique_id(refnum);
		if (id && !my_stricmp(nick, id))
			malloc_strcpy(&s->d_nickname, zero);
		else
			malloc_strcpy(&s->d_nickname, nick);

		/* Make a note that we are changing our nickname */
		malloc_strcpy(&s->s_nickname, nick);
	}

	if (s->s_nickname && is_server_open(refnum))
		send_to_aserver(refnum, "NICK %s", s->s_nickname);
}

const char *	get_pending_nickname (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return NULL;

	return s->s_nickname;
}

void	accept_server_nickname (int refnum, const char *nick)
{
	Server *s;
	const char *id;

	if (!(s = get_server(refnum)))
		return;

	/* We always accept whatever the server says our new nick is. */
	malloc_strcpy(&s->nickname, nick);
	new_free(&s->s_nickname);

	/* Change our default nickname to our new nick, or 0 for unique id's */
	id = get_server_unique_id(refnum);
	if (id && !my_stricmp(nick, id))
		malloc_strcpy(&s->d_nickname, zero);
	else
		malloc_strcpy(&s->d_nickname, nick);

	/* Change the global nickname for primary server (die, die!) */
	if (refnum == primary_server)
		strlcpy(nickname, nick, sizeof nickname);

	update_all_status();
}

void	nickname_change_rejected (int refnum, const char *mynick)
{
	if (is_server_registered(refnum))
	{
		accept_server_nickname(refnum, mynick);
		return;
	}

	reset_nickname(refnum);
}

/*
 * reset_nickname: when the server reports that the selected nickname is not
 * a good one, it gets reset here. 
 * -- Called by more than one place
 */
static void 	reset_nickname (int refnum)
{
	Server *s;
	char *	old_pending = NULL;

	if (!(s = get_server(refnum)))
		return; 		/* Don't repeat the reset */

	if (s->s_nickname)
		old_pending = LOCAL_COPY(s->s_nickname);

	do_hook(NEW_NICKNAME_LIST, "%d %s %s", refnum, 
			coalesce(s->nickname, "*"), 
			coalesce(s->s_nickname, "*"));

	if (!(s = get_server(refnum)))
		return;			/* Just in case the user punted */

	/* Did the user do a /NICK in the /ON NEW_NICKNAME ? */
	if (s->s_nickname == NULL || 
		(old_pending && !strcmp(old_pending, s->s_nickname)))
	{
	    say("Use the /NICK command to set a new nick to continue "
			"connecting.");
	    say("If you get disconnected, you will also need to do "
			"/server +%d to reconnect.", refnum);
	}
	update_all_status();
}

/*****************************************************************************/
/* GETTERS AND SETTERS FOR MORE MUNDANE THINGS */

/* A setter for a mundane integer field */
#define SET_IATTRIBUTE(param, member) \
void	set_server_ ## member (int servref, int param )	\
{							\
	Server *s;					\
							\
	if (!(s = get_server(servref)))			\
		return;					\
							\
	s-> member = param;				\
}

/* A getter for a mundane integer field */
#define GET_IATTRIBUTE(member) \
int	get_server_ ## member (int servref)		\
{							\
	Server *s;					\
							\
	if (!(s = get_server(servref)))			\
		return -1;				\
							\
	return s-> member ;				\
}

/* A setter for a mundane string field */
#define SET_SATTRIBUTE(param, member) \
void	set_server_ ## member (int servref, const char * param )	\
{							\
	Server *s;					\
							\
	if (!(s = get_server(servref)))			\
		return;					\
							\
	malloc_strcpy(&s-> member , param);		\
}

/* A getter for a mundane string field */
#define GET_SATTRIBUTE(member, default)			\
const char *	get_server_ ## member (int servref ) 	\
{							\
	Server *s;					\
							\
	if (!(s = get_server(servref)))			\
		return default ;			\
							\
	if (s-> member )				\
		return s-> member ;			\
	else						\
		return default ;			\
}

/* A getter and a setter for a mundane integer field */
#define IACCESSOR(param, member)		\
SET_IATTRIBUTE(param, member)			\
GET_IATTRIBUTE(member)

/* A getter and a setter for a mundane string field */
#define SACCESSOR(param, member, default)	\
SET_SATTRIBUTE(param, member)			\
GET_SATTRIBUTE(member, default)

/* A getter and a static setter for a mundane string field */
#define SSACCESSOR(param, member, default)	\
static SET_SATTRIBUTE(param, member)			\
GET_SATTRIBUTE(member, default)

/* Various mundane getters and setters */
IACCESSOR(v, sent)
IACCESSOR(v, line_length)
IACCESSOR(v, max_cached_chan_size)
IACCESSOR(v, ison_len)
IACCESSOR(v, ison_max)
IACCESSOR(v, userhost_max)
IACCESSOR(v, stricmp_table)
IACCESSOR(v, autoclose)
IACCESSOR(v, accept_cert)
SACCESSOR(chan, invite_channel, NULL)
SACCESSOR(nick, joined_nick, NULL)
SACCESSOR(nick, public_nick, NULL)
SACCESSOR(nick, recv_nick, NULL)
SACCESSOR(nick, sent_nick, NULL)
SACCESSOR(text, sent_body, NULL)
SACCESSOR(message, quit_message, get_string_var(QUIT_MESSAGE_VAR))
SACCESSOR(cookie, cookie, NULL)
SACCESSOR(ver, version_string, NULL)
SACCESSOR(name, default_realname, get_string_var(DEFAULT_REALNAME_VAR))
GET_SATTRIBUTE(realname, NULL)

/* * * * */
/* Getters and setters that require special handling somehow. */

/*
 * Getter and setter for (IRCNet) "Unique ID"
 */
GET_SATTRIBUTE(unique_id, NULL)
void	set_server_unique_id (int servref, const char * id)
{
	Server *s;

	if (!(s = get_server(servref)))
		return;

	malloc_strcpy(&s->unique_id , id);
	if (id && s->d_nickname && !my_stricmp(id, s->d_nickname))
		malloc_strcpy(&s->d_nickname, zero);
}


/*
 * Getter and setter for Server Status ("server state")
 */
GET_IATTRIBUTE(state)
static void	set_server_state (int refnum, int new_state)
{
	Server *s;
	int	old_state;
	const char *oldstr, *newstr;

	if (!(s = get_server(refnum)))
		return;

	if (new_state < 0 || new_state > SERVER_DELETED)
		return;			/* Not acceptable */

	old_state = s->state;
	if (old_state < 0 || old_state > SERVER_DELETED)
		oldstr = "UNKNOWN";
	else
		oldstr = server_states[old_state];

	s->state = new_state;
	newstr = server_states[new_state];
	do_hook(SERVER_STATE_LIST, "%d %s %s", refnum, oldstr, newstr);
	do_hook(SERVER_STATUS_LIST, "%d %s %s", refnum, oldstr, newstr);
	update_all_status();
}

const char *	get_server_state_str (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return empty_string;

	return server_states[s->state];
}


/*
 * Getter and setter for "name" (ie, "ourname")
 */
static void	set_server_name (int servref, const char * param )
{
	if (!(get_server(servref)))
		return;

	set_si(servref, "HOST", param);
}

const char *	get_server_name (int servref )
{
	if (!(get_server(servref)))
		return "<none>";

	return coalesce_empty(get_si(servref, "HOST"), "<none>");
}

static const char *	get_server_host (int servref )
{
	return get_server_name(servref);
}


/*
 * Getter and setter for "server_group"
 */
void	set_server_group (int servref, const char * param )
{
	if (!(get_server(servref)))
		return;

	set_si(servref, "GROUP", param);
}

const char *	get_server_group (int servref)
{
	if (!(get_server(servref)))
		return "<default>";

	return coalesce_empty(get_si(servref, "GROUP"), "<default>");
}

/*
 * Getter and setter for "server_type"
 */
static void	set_server_server_type (int servref, const char * param )
{
	if (!(get_server(servref)))
		return;

	set_si(servref, "TYPE", param);
}

/*
 * This returne either "IRC" or "IRC-SSL" for now.
 * XXX - I really regret calling this field "type".
 */
static const char *	get_server_type (int servref )
{
	if (!(get_server(servref)))
		return "IRC";

	return coalesce_empty(get_si(servref, "TYPE"), "IRC");
}

/*
 * Getter and setter for "vhost"
 */
static void	set_server_vhost (int servref, const char * param )
{
	if (!(get_server(servref)))
		return;

	set_si(servref, "VHOST", param);
}

const char *	get_server_vhost (int servref )
{
	if (!(get_server(servref)))
		return "<none>";

	return coalesce_empty(get_si(servref, "VHOST"), "<none>");
}

static void    set_server_cert (int refnum, const char *cert)
{
	if (!(get_server(refnum)))
		return;

	set_si(refnum, "CERT", cert);
}

const char *	get_server_cert (int servref )
{
	if (!(get_server(servref)))
		return NULL;

	if (!empty(get_si(servref, "CERT")))
		return get_si(servref, "CERT");
	else
		return NULL;		/* XXX Is this correct? */
}

void    set_server_cap_hold (int refnum, int value)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;
	s->cap_hold = value;
}

int	get_server_cap_hold (int servref )
{
	Server *s;

	if (!(s = get_server(servref)))
		return 0;

	return s->cap_hold;
}

/* 
 * Getter and setter for "itsname"
 */
static SET_SATTRIBUTE(name, itsname)
const char	*get_server_itsname (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return "<none>";

	if (s->itsname)
		return s->itsname;
	else
		return get_server_host(refnum);
}

/*
 * All this is to work around clang...
 */
void	set_server_doing_privmsg (int servref, int value)
{
	Server *s;

	if (!(s = get_server(servref)))
		return;

	if (value == 1)
		s->protocol_metadata |= DOING_PRIVMSG;
	else if (value == 0)
		s->protocol_metadata &= ~(DOING_PRIVMSG);
	else
		yell("set_server_doing_privmsg server %d value %d is invalid", servref, value);
}

int	get_server_doing_privmsg (int servref)
{
	Server *s;

	if (!(s = get_server(servref)))
		return -1;

	if ((s->protocol_metadata & DOING_PRIVMSG) == DOING_PRIVMSG)
		return 1;
	return 0;
}

void	set_server_doing_notice (int servref, int value)
{
	Server *s;

	if (!(s = get_server(servref)))
		return;

	if (value == 1)
		s->protocol_metadata |= DOING_NOTICE;
	else if (value == 0)
		s->protocol_metadata &= ~(DOING_NOTICE);
	else
		yell("set_server_doing_notice server %d value %d is invalid", servref, value);
}

int	get_server_doing_notice (int servref)
{
	Server *s;

	if (!(s = get_server(servref)))
		return -1;

	if ((s->protocol_metadata & DOING_NOTICE) == DOING_NOTICE)
		return 1;
	return 0;
}

void	set_server_doing_ctcp (int servref, int value)
{
	Server *s;

	if (!(s = get_server(servref)))
		return;

	if (value == 1)
		s->protocol_metadata |= DOING_CTCP;
	else if (value == 0)
		s->protocol_metadata &= ~(DOING_CTCP);
	else
		yell("set_server_doing_ctcp server %d value %d is invalid", servref, value);
}

int	get_server_doing_ctcp (int servref)
{
	Server *s;

	if (!(s = get_server(servref)))
		return -1;

	if ((s->protocol_metadata & DOING_CTCP) == DOING_CTCP)
		return 1;
	return 0;
}

int	get_server_protocol_state (int refnum)
{
	int	retval = 0;
	int	value;

	if (!get_server(refnum))
		return -1;

	if ((value = get_server_doing_ctcp(refnum)) < 0)
		return -1;
	if (value == 1)
		retval |= DOING_CTCP;

	if ((value = get_server_doing_notice(refnum)) < 0)
		return -1;
	if (value == 1)
		retval |= DOING_NOTICE;

	if ((value = get_server_doing_privmsg(refnum)) < 0)
		return -1;
	if (value == 1)
		retval |= DOING_PRIVMSG;

	return retval;
}

void	set_server_protocol_state (int refnum, int state)
{
	if (state < 0)
	{
		yell("set_server_protocol_state: refnum = %d, state = %d -- something goofed.  Tell #epic on EFNet what just happened", refnum, state);
		return;
	}

	if (!get_server(refnum))
		return;

	if ((state & DOING_CTCP) == DOING_CTCP)
		set_server_doing_ctcp(refnum, 1);
	else
		set_server_doing_ctcp(refnum, 0);

	if ((state & DOING_NOTICE) == DOING_NOTICE)
		set_server_doing_notice(refnum, 1);
	else
		set_server_doing_notice(refnum, 0);

	if ((state & DOING_PRIVMSG) == DOING_PRIVMSG)
		set_server_doing_privmsg(refnum, 1);
	else
		set_server_doing_privmsg(refnum, 0);
}

/***********************************************************************/
/* WAIT STUFF */
/*
 * server_hard_wait -- Do not return until one round trip to the server 
 *			is completed.
 *
 * Arguments:
 *	i	- A server refnum
 *
 * Notes:
 * 	- This is the /WAIT command.
 *	- This function does not return until this WAIT _and all 
 *	  subsequent WAITs launched while this one is pending_ 
 *	  have completed.  This is an unspecified amount of time.
 *	- See the comments for server_check_wait() for more info.
 */
void 	server_hard_wait (int i)
{
	Server *s;
	int	proto, old_from_server;
	char	reason[1024];

	if (!(s = get_server(i)))
		return;

	if (!is_server_registered(i))
		return;

	snprintf(reason, 1024, "WAIT on server %d", i);
	proto = get_server_protocol_state(i);
	old_from_server = from_server;

	s->waiting_out++;
	lock_stack_frame();
	send_to_aserver(i, "%s", hard_wait_nick);
	while ((s = get_server(i)) && (s->waiting_in < s->waiting_out))
		io(reason);

	set_server_protocol_state(i, proto);
	from_server = old_from_server;
}

/*
 * server_passive_wait - Register a non-recursive callback promise
 *
 * Arguments:
 *	i	- A server refnum
 *	stuff	- A block of code to run at a later time
 *
 * Notes:
 *	This is the /WAIT -CMD command.
 * 	'stuff' will be run after one round trip to the server 'i'.
 *	See the comments for server_check_wait() for more info.
 */
void	server_passive_wait (int i, const char *stuff)
{
	Server *s;
	WaitCmd	*new_wait;

	if (!(s = get_server(i)))
		return;

	new_wait = (WaitCmd *)new_malloc(sizeof(WaitCmd));
	new_wait->stuff = malloc_strdup(stuff);
	new_wait->next = NULL;

	if (s->end_wait_list)
		s->end_wait_list->next = new_wait;
	s->end_wait_list = new_wait;
	if (!s->start_wait_list)
		s->start_wait_list = new_wait;

	send_to_aserver(i, "%s", wait_nick);
}

/*
 * server_check_wait - A callback to see if a WAIT has completed
 *
 * Arguments:
 *	refnum	- A server refnum that has sent us a token (see below)
 *	nick	- The token sent to us
 *
 * Return value:
 *	1	- This token represents a valid WAIT request
 *	0	- This token does not represent a valid WAIT request.
 *
 * - Backstory about hard waits:
 * The /WAIT command performs a (to your script) blocking synchronization
 * with the server.  This means it does not return until all of the 
 * commands you have previously sent to the server have been completed
 * (and any /ONs have been run).  This is done by recursively calling 
 * the main loop until one round trip to the server is completed.
 *
 * How it does this is in server_hard_wait() [see above].  It will send
 * an invalid command to the server  (hard_wait_nick) and wait for the
 * server to send a 421 NOSUCHCOMMAND numeric back to us.  
 *
 * Ordinarily, this would be non-controversial, except that you might do
 * a /WAIT while another /WAIT is already pending.  This can get messy,
 * so how we choose to manage that is, _No WAIT shall return until ALL 
 * pending WAITs have completed_.  This means a WAIT does not return at 
 * the first possible convenience; but only when it is guaranteed to be
 * safe.  This means anything you do after a WAIT (such as cleanup) might
 * have occurred after several consecutive WAITs have happened.  That's just
 * a risk you have to take.
 *
 * - Backstory about soft waits:
 * The /WAIT -CMD command implements a "promise" feature, where it will
 * record a block of code to be run at a later time, after one round trip
 * to the server has occurred.  Because the client does not implement
 * closures, this is not as flexible, since you can only converse between
 * the calling scope and the /WAIT -CMD scope through global variables, and
 * that means you don't have re-entrancy.  However, you do get the promise
 * that your commands will be run at the first possible convenience.
 *
 * - /WAITs and /WAIT -CMDs play nicely with each other.
 *
 * This function should only be called by the 421 Numeric Handler.
 * If the "invalid command" is a hard wait, we record that
 * If the "invalid command" is a soft wait, we run the appropriate callback.
 */
int	server_check_wait (int refnum, const char *nick)
{
	Server	*s;

	if (!(s = get_server(refnum)))
		return 0;

	/* Hard waits */
	if ((s->waiting_out > s->waiting_in) && !strcmp(nick, hard_wait_nick))
	{
		s->waiting_in++;
		unlock_stack_frame();
	        return 1;
	}

	/* Soft waits */
	if (s->start_wait_list && !strcmp(nick, wait_nick))
	{
		WaitCmd *old = s->start_wait_list;

		s->start_wait_list = old->next;
		if (old->stuff)
		{
			call_lambda_command("WAIT", old->stuff, empty_string);
			new_free(&old->stuff);
		}
		if (s->end_wait_list == old)
			s->end_wait_list = NULL;
		new_free((char **)&old);
		return 1;
	}

	/* This invalid command is not a wait */
	return 0;
}

/*****************************************************************************/
/***** ALTNAME STUFF *****/

/*
 * add_server_altname - Add an alternate name for a refnum
 * 
 * Parameters:
 *	refnum	- A server refnum
 *	altname	- A new alternate name for 'refnum'
 *
 * The purpose of altnames is to give you something to refer to them
 * by.  Traditionally, you've had to refer to a server either by 
 * its name, or its refnum.  But then later people asked to be able
 * to refer to a server by its group, and then eventually by any 
 * random string. 
 *
 * For any value <x>, doing add_server_altname(refnum, <x>)
 * You may then later use <x> to refer to the server:
 *	/server <x>
 *	$serverctl(GET <x> ...stuff...)
 *	/window server <x>
 *	(etc)
 * The first altname (numbered 0) is the "shortname" and is 
 * auto-populated and used as %S on the status bar.
 */
static void	add_server_altname (int refnum, char *altname)
{
	Server *s;
	char *v;

	if (!(s = get_server(refnum)))
		return;

	v = malloc_strdup(altname);
	add_to_bucket(s->altnames, v, NULL);
}

/*
 * reset_server_altnames - Replace the altnames list with something new
 *
 * Parmeters:
 *	refnum		- A server refnum
 *	new_altnames 	- A space-separated list of dwords (altnames that
 *			  contain spaces must be double-quoted) that 
 *			  will replace the server's current altnames.
 *
 * The first word will be "altname 0" and will  be used by the %S 
 * status bar expando.
 *
 * You cannot modify the altnames in place; you can either append a new
 * altname (add_server_altname() above), or replace the entire list at once.
 * The user's script usually does this with:
 *	@ y = serverctl(GET $ref ALTNAMES)
 *	[do something to $y]
 * 	@serverctl(SET $ref ALTNAMES $y)
 */
static void	reset_server_altnames (int refnum, char *new_altnames)
{
	Server *s;
	int	i;
	char *	value;

	if (!(s = get_server(refnum)))
		return;

	for (i = 0; i < s->altnames->numitems; i++)
		/* XXX Free()ing this (const char *) is ok */
		new_free((char **)(intptr_t)&s->altnames->list[i].name);	

	s->altnames->numitems = 0;

	while ((value = new_next_arg(new_altnames, &new_altnames)))
		add_server_altname(refnum, value);
}

/*
 * get_server_altnames - Return all altnames for a server
 *
 * Parameter:
 *	refnum	- A server refnum
 *
 * Return value:
 *	NULL	- "refnum" is not a valid server refnum
 *	<other>	- A space-separated list of dwords (altnames that contain 
 *		  spaces are double-quoted, so you have to call new_next_word()
 *		  to iterate the words)
 *  THE RETURN VALUE IS YOUR STRING.  YOU MUST NEW_FREE() IT.
 */
static char *	get_server_altnames (int refnum)
{
	Server *s;
	char *	retval = NULL;
	int	i;

	if (!(s = get_server(refnum)))
		return NULL;

	for (i = 0; i < s->altnames->numitems; i++)
		malloc_strcat_word(&retval, space, s->altnames->list[i].name, DWORD_DWORDS);

	return retval;
}

/*
 * get_server_altname: Return the 'which'th altname for 'refnum'
 *
 * Parameters:
 *	refnum	- A server refnum
 *	which	- An integer >= 0, index into the altname list
 *
 * Returns:
 *	NULL	- "refnum" is not a valid server refnum
 *		  -or- "which" is not a valid altname index for "refnum"
 *	<other>	- The "which"th altname for "refnum"
 *		  THIS IS NOT YOUR STRING.  YOU MUST NOT MODIFY IT.
 *
 * Since 'altnames' is a bucket, it could technically have NULLs in it,
 * but it seems there's no way to delete an altname; you have to complete
 * reset the entire set.  So although it's _possible_, I don't believe it
 * happens in practice.  
 *
 * Thus, you could enumerate all altnames by starting at which=0 and 
 * stopping when you get a NULL back.
 */
const char *	get_server_altname (int refnum, int which)
{
	Server	*s;

	if (!(s = get_server(refnum)))
		return NULL;

	if (which < 0 || which > s->altnames->numitems - 1)
		return NULL;

	return s->altnames->list[which].name;
}

/*
 * shortname: Convert a server "ourname" into a server "shortname".
 *
 * Parameters:
 *	oname	- The "ourname" -- what you typed to /server or put 
 *		  in a server description (eg, "irc.efnet.com")
 *
 * Return value:
 *	The Shortname for 'oname':
 *	  1. If 'oname' starts with "irc", the first segment is removed
 *	     (this catches "irc.*" and "ircserver.*" and "irc-2.*")
 *	  2. If after this it is > 60 bytes, it is truncated
 *	THE RETURN VALUE IS YOUR STRING -- YOU MUST NEW_FREE() IT.
 * 
 * The intention is the "shortname" should be a server's default 
 * zeroth altname.  The zeroth altname is what appears as %S on your
 * status bar.
 *
 * This is used by serverdesc_insert(), the function that creates
 * new server refnums.
 */
static char *	shortname (const char *oname)
{
	char *name, *next, *rest;
	ssize_t	len;

	name = malloc_strdup(oname);

	/* If it's an IP address, just use it. */
	if (strtoul(name, &next, 10) && *next == '.')
		return name;

	/* If it doesn't have a dot, just use it. */
	if (!(rest = strchr(name, '.')))
		return name;

	/* If it starts with 'irc', skip that. */
	if (!strncmp(name, "irc", 3))
	{
		ov_strcpy(name, rest + 1);
		if (!(rest = strchr(name + 1, '.')))
			rest = name + strlen(name);
	}

	/* Truncate at 60 chars */
	if ((len = rest - name) > 60)
		len = 60;

	name[len] = 0;
	return name;
}


/*****************************************************************************/
/* 005 STUFF */

/*
 * make_options - Initialize the Bucket that will hold a server's 005 settings.
 *
 * Parameter:
 *	refnum	- A server whose options alist needs initializing
 *
 * This function must be called once and only one per refnum,
 *    and that call must be from servref_to_newserv().
 * If you call it elsewhere, it will leak memory (s->a005.list).
 * If you want to clean up/reset an 005 alist, use destroy_005(refnum)
 */
static void	make_options (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	s->options.list = NULL;
	s->options.max = 0;
	s->options.total_max = 0;
	s->options.func = (alist_func)strncmp;
	s->options.hash = HASH_SENSITIVE; /* One way to deal with rfc2812 */
}

/*
 * destroy_an_option - Clean up after an unwanted 005 setting.
 *
 * Parameter:
 *	item	- An 005 item that has been previously removed from the server.
 *
 * Once you have called this function, you must not make any reference to the
 * 'item' parameter you passed in.
 */
static void	destroy_an_option (OPTION_item *item)
{
	if (item) 
	{
		new_free(&((*item).name));
		new_free(&((*item).value));
		new_free(&item);
	}
}

/*
 * destroy_options - Remove all 005 settings for a server
 *
 * Parameters:
 *	refnum	- A server refnum
 *
 * This function releases all of the malloc()ed memory associated with 005
 * management for a server.  It should therefore be called when the server
 * gets disconnected or deleted.
 *
 * Technically, this may only be called for a server that has previously 
 * been called make_005(refnum) to set up the server's 005 Bucket.  This
 * would always be the case, but I'm just saying...
 */
static void	destroy_options (int refnum)
{
	Server *	s;
	OPTION_item *	new_i;

	if (!(s = get_server(refnum)))
		return;

	while ((new_i = (OPTION_item *)alist_pop((&s->options), 0)))
		destroy_an_option(new_i);
	s->options.max = 0;
	s->options.total_max = 0;
	new_free(&s->options.list);
}

/*
 * get_server_005s - return all settings passed to set_server_005()
 *
 * Parameters:
 *	refnum	- A server refnum 
 *	str	- A wildcard pattern.  Use "*" to get everything.
 *
 * Return value:
 *	A space-separated list of zero or more words containing the settings
 * 	that were previously passed to set_server_005(refnum, setting, ...);
 *	THIS IS YOUR STRING.  YOU MUST NEW_FREE() IT.
 *
 * This was previously a macro, but clang complained there was a NULL deref,
 * so I de-macrofied it so i could figure it out.
 */
static char *	get_server_005s (int refnum, const char *str)
{
	int	i;
	char *	ret = NULL;
	Server *s;

	if (!(s = get_server(refnum)))
		return malloc_strdup(empty_string);

	for (i = 0; i < s->options.max; i++)
	{
		if (s->options.list[i]->name == NULL)
			continue;	/* Ignore nulls */
		if (((OPTION_item *)(s->options.list[i]->data))->type != 0)
			continue;	/* Ignore non-005s */
		if (!str || !*str || wild_match(str, s->options.list[i]->name))
			malloc_strcat_wordlist(&ret, space, s->options.list[i]->name);
	}

	if (ret)
		return ret;
	RETURN_EMPTY;
}

/*
 * get_server_005 - Retrieve an 005 variable for a server
 *
 * Parameters:
 *	refnum	- The server that previously sent us an 005 numeric
 *	setting	- The server setting we want to get the value of
 *
 * Return Value:
 *	NULL	- "refnum" is not a valid server
 *		  -or- The server did not provide an 005 containing 'setting'
 *	<other>	- The 'value' value most recently passed to
 *			set_server_005(refnum, setting, value);
 *		  THIS IS NOT YOUR STRING.  You must not modify it.
 */
const char *	get_server_005 (int refnum, const char *setting)
{
	Server *s;
	OPTION_item *item;
	int cnt, loc;

	if (!(s = get_server(refnum)))
		return NULL;
	item = (OPTION_item *)find_alist_item(&s->options, setting, &cnt, &loc);
	if (cnt < 0)
	{
		if (item->type != 0)
			return NULL;
		return item->value;	/* for backwards compat */
	}
	else
		return NULL;
}

static OPTION_item *	new_005_item (int refnum, const char *setting)
{
	Server *	s;
	OPTION_item *	new_option;

	if (!(s = get_server(refnum)))
		return NULL;

	new_option = (OPTION_item *)new_malloc(sizeof(OPTION_item));
	(*new_option).name = malloc_strdup(setting);
	(*new_option).value = malloc_strdup(space);
	(*new_option).type = 0;
	add_to_alist((&s->options), setting, new_option);
	return new_option;
}

/*
 * set_server_005 - Associate an 005 numeric variable with the server
 *
 * Parameters:
 *	refnum 	- The server that sent us an 005 numeric
 *	setting - The variable name (the part before =)
 *	value 	- The value (the part after =)
 *	          If "value" is NULL or the empty string, it deletes "setting".
 *
 * It is a practice in modern IRC for the server to provide the client
 * some information about its configuration so that the client can 
 * parse things correctly.  This information is provided via the 005
 * numeric reply, which usually accompanies the VERSION reply.
 *
 * Example:
 *	:server.com 005 mynick mynick CHANTYPES=&# PREFIX=(ov)@+
 * Each of these parameters is divided into a "setting" (CHANTYPES, PREFIX) and
 * a "value".  The exact interpretation of the values is set by convention, and 
 * there isn't a form RFC that describes or requires them.  The client uses 
 * these values when they are provided to make better decisions.
 *
 * XXX - As a side effect, this function acts as a gatekeeper for flags that 
 * get set.  For now, we capture the CASEMAPPING setting to determine how we 
 * should treat lower and uppercase leters (RFC1459 says that {|} and [\] 
 * are equivalent, but ASCII says they're not.)   In theory, we should be 
 * handling this someplace else, but for now, we do it here.
 *
 * XXX - It is probably a hack to call update_all_status() here, but I'm not 
 * sure if i care that badly.
 */
void	set_server_005 (int refnum, char *setting, const char *value)
{
	Server *	s;
	OPTION_item *	new_option;

	if (!(s = get_server(refnum)))
		return;

	if (!(new_option = (OPTION_item *)alist_lookup((&s->options), setting, 0)))
		new_option = new_005_item(refnum, setting);
	malloc_strcpy(&(*new_option).value, malloc_strdup(value));
	new_option->type = 0;

	/* XXX This is a hack XXX */
	/* We need to set up a table to handle 005 callbacks. */
	if (!my_stricmp(setting, "CASEMAPPING"))
	{
	    if (!new_option->value)
		(void) 0; /* nothing */
	    else if (!my_stricmp(new_option->value, "rfc1459"))
		set_server_stricmp_table(refnum, 1);
	    else if (!my_stricmp(new_option->value, "ascii"))
		set_server_stricmp_table(refnum, 0);
	    else
		set_server_stricmp_table(refnum, 1);
	}

	update_all_status();
}

/*
 * get_all_server_groups - Return a list of all "group" fiends used by servers
 *
 * Return value:
 * 	A string containing a space separated list of all values of the "group"
 *	field in all servers, open or closed.
 *
 * THE RETURN VALUE IS YOUR STRING -- YOU MUST EVENTUALLY NEW_FREE() IT.
 * The order of the groups in the return value is unspecified and may change.
 * This implementation orders the groups by server refnum.
 */
static char *	get_all_server_groups (void)
{
	int	i, j;
	char *	retval = NULL;

	for (i = 0; i < number_of_servers; i++)
	{
	    if (!get_server(i))
		continue;

	    /* 
	     * A group is added to the return value if and only if
	     * there is no lower server refnum of the same group.
	     */
	    for (j = 0; j < i; j++)
	    {
		if (!get_server(j))
			continue;
		if (!my_stricmp(get_server_group(i), get_server_group(j)))
			goto sorry_wrong_number;
	    }

	    malloc_strcat_word(&retval, space, get_server_group(i), DWORD_DWORDS);
sorry_wrong_number:
	    ;
	}
	return retval;
}

/* Used by function_serverctl */
/*
 * $serverctl(INSERT server-desc)	TODO
 * $serverctl(UPDATE server-desc)	TODO
 * $serverctl(UPSERT server-desc)	TODO
 * $serverctl(DELETE server-desc)	TODO
 *
 * $serverctl(OMATCH [pattern])		Wildcard match "ourname"
 * $serverctl(IMATCH [pattern])		Wildcard match "itsname"
 * $serverctl(GMATCH [group])		Wildcard match "group"
 *
 * $serverctl(REFNUM server-desc)	Lookup server by desc
 * $serverctl(MAX)			Maximum server refnum in use
 * $serverctl(LAST_SERVER)		The last server to send us data
 * $serverctl(FROM_SERVER)		The current server
 *
 * $serverctl(ALLGROUPS)		All groups (deduplicated)
 * $serverctl(READ_FILE filename)	XXX Remove
 * $serverctl(DONT_CONNECT)		Used at startup to force -s
 *
 * $serverctl(GET 0 [ITEM])		Get field
 * $serverctl(SET 0 [ITEM] [VALUE])	Set field
 * $serverctl(RESET 0)			Reset protocol session (for znc)
 *
 * [ITEM] are one of the following:
 *		Values that are used pre-registration (connect time)
 *		Changing these values won't have effect until you reconnect.
 *		However, they will immediately be reflected by GET!
 *		===========================================================
 *	ADDRFAMILY		*		"v4" or "v6" or "4" or "6"
 *	CERT			*	*	TLS Client certificate 
 *	NAME			*	*	"Ourname" - the hostname you gave to /server
 *	PORT			*	*	The port the server uses (influences SSL/TLS)
 *	PROTOCOL		*	?	"IRC" or "IRC-SSL" (influences SSL/TLS, overrides PORT)
 *	SSL			*	*	Whether to use SSL/TLS or not
 *	TLS				*	New name of "SSL"
 *	VHOST			*	*	The vhost to use (established on reconnect)
 *
 *		Values that are used for registration
 *		Changing these values won't have effect until you reconnect.
 *		However, they will immediately be reflected by GET!
 *		==================================================
 *	DEFAULT_REALNAME	*	*	Realname in WHOIS (established on reconnect)
 *	NICKNAME		*	*	The nickname to use (established on reconnect)
 *	PASSWORD		*	*	The password to use (established on reconnect)
 *	REALNAME		*	*	Former name of "DEFAULT_REALNAME"
 *	UMODE			*	*	The umode to use (established on reconnect)
 *	
 *		Values that are used immediately post-registration
 *		Changing these values won't have effect until you reconnect.
 *		However, they will immediately be reflected by GET!
 *		==================================================
 *	AWAY			*	*	Auto-away myself on reconnect
 *
 *		Values that do not change at runtime
 *		These control how epic behaves
 *		====================================
 *				Get	Set	Description
 *	ALTNAME			*	*	Get: first altname; Set: add altname
 *	ALTNAMES		*	*	All "altnames" used for lookups
 *	AUTOCLOSE		*	*	Close when "No windows for this server"
 *	GROUP			*	*	User-specified grouping
 *	ISONLEN			*	*	
 *	MAXCACHESIZE		*	*	Channels bigger than this won't send /WHO on join
 *	MAXISON			*	*	How many isons to chonk per request
 *	MAXUSERHOST		*	*	How many userhosts to chonk per request
 *	PRIMARY				*	- Deprecated -
 *	QUIT_MESSAGE		*	*	The quit message to use for /QUIT or /DISCONNECT
 *
 *		Values that reflect the state of the world at runtime
 *		Getting them when not connected doesn't make any sense, even if they return something
 *		Setting these doesn't make any sense, even if it lets you
 *		======================================================================================
 *				Get	Set	Description
 *	005 [item]		*	*	Get the 005 [item]'s value
 *	005s			*		Get a list of all 005 items the server supports
 *	ADDRSLEFT		*		How many ip addresses do i have left before i give up?
 *	AWAY_STATUS		*	*	What is my actual away status right now?
 *	CONNECTED		*		- deprecated -
 *	COOKIE			*	*	The reconnection "magic cookie"
 *	FULLDESC		*		A string you can use to lookup this server
 *	ITSNAME			*	*	"Itsname" - what the server calls itself on IRC
 *						This is a IRC protocol name, not a DNS hostname!
 *	LOCALPORT		*		When connected, the local sockaddr port
 *	NEXT_SERVER_IN_GROUP	*		The next server refnum with the same "GROUP"
 *	OPEN			*		Whether we are "open" or not (bah)
 *	PADDR			*		The p-addr of the server we connected to
 *	SSL_VERIFY_RESULT	*		+ The server's TLS certificate was verified
 *	SSL_VERIFY_ERROR	*		+ "" "" failed verification
 *	SSL_PEM			*		+ "" "" PEM representation
 *	SSL_CERT_HASH		*		+ "" "" certificate hash 
 *	SSL_PKEY_BITS		*		+ "" "" bit-size
 *	SSL_SUBJECT		*		+ "" "" "Subject Name"
 *	SSL_SUBJECT_URL		*		+ "" "" Subject Name, URL encoded
 *	SSL_ISSUER		*		+ "" "" "Issuer Name"
 *	SSL_ISSUER_URL		*		+ "" "" "Issuer Name", URL encoded
 *	SSL_VERSION		*		+ "" "" protocol version (ie, TLSv3)
 *	SSL_CHECKHOST_RESULT	*		+ Whether the "" "" was self-signed
 *	SSL_CHECKHOST_ERROR	*		+ An error string
 *	SSL_SELF_SIGNED		*		+ Whether the "" "" was self-signed
 *	SSL_SELF_SIGNED_ERROR	*		+ An error string
 *	SSL_OTHER_ERROR		*		+ An error string (serious)
 *	SSL_MOST_SERIOUS_ERROR	*		+ Which TLS verification error was the most serious
 *	SSL_SANS		*		+ "" "" Subject Alternate Names
 *	STATE			*		The operational state of the server, right now
 *	STATUS			*		Old name for "STATE"
 *	UNIQUE_ID		*	*	IRCNet's "default number nickname"
 *	USERHOST		*	*	What the server says our userhost is.
 *	VERSION			*	*	The version string of the irc server
 *
 *		Values that allow script to control-flow the client's reconnection
 *		======================================================
 *	CAP_HOLD			*	Set to 1 when CAP LS is sent; doesn't send CAP END until you set it to 0
 *	SSL_ACCEPT_CERT		*	*	Do you want to accept the server's TLS cert? (GET returns suggestion/fallback)
 */
char 	*serverctl 	(char *input)
{
	int	refnum, num, len;
	char	*listc, *listc1;
	const char *ret;
	char *	retval = NULL;

	GET_FUNC_ARG(listc, input);
	len = strlen(listc);
	if (!my_strnicmp(listc, "INSERT", len)) {
		/* XXX TODO XXX */
	} else if (!my_strnicmp(listc, "DELETE", len)) {
		/* XXX TODO XXX */
	} else if (!my_strnicmp(listc, "LAST_SERVER", len)) {
		RETURN_INT(last_server);
	} else if (!my_strnicmp(listc, "FROM_SERVER", len)) {
		RETURN_INT(from_server);
	} else if (!my_strnicmp(listc, "DONT_CONNECT", len)) {
		if (input && *input)
		{
			int	new_dont_connect = atol(input);
			dont_connect = new_dont_connect;
		}
		RETURN_INT(dont_connect);
	} else if (!my_strnicmp(listc, "REFNUM", len)) {
		char *server;

		GET_FUNC_ARG(server, input);
		refnum = serverdesc_lookup(server);
		if (refnum != NOSERV)
			RETURN_INT(refnum);
		RETURN_EMPTY;
	} else if (!my_strnicmp(listc, "UPDATE", len)) {
		int   servref;

		GET_INT_ARG(servref, input);
		refnum = serverdesc_update_aserver(input, servref);
		if (refnum != NOSERV)
			RETURN_INT(refnum);
		RETURN_EMPTY;
	} else if (!my_strnicmp(listc, "ALLGROUPS", len)) {
		retval = get_all_server_groups();
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "GET", len)) {
		GET_INT_ARG(refnum, input);
		if (!get_server(refnum))
			RETURN_EMPTY;

		GET_FUNC_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "AWAY", len)) {
			ret = get_server_away_message(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "AWAY_STATUS", len)) {
			num = get_server_away_status(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "MAXCACHESIZE", len)) {
			num = get_server_max_cached_chan_size(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "MAXISON", len)) {
			num = get_server_ison_max(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "MAXUSERHOST", len)) {
			num = get_server_userhost_max(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "ISONLEN", len)) {
			num = get_server_ison_len(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "CONNECTED", len)) {
			num = is_server_registered(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "COOKIE", len)) {
			ret = get_server_cookie(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "GROUP", len)) {
			ret = get_server_group(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "ITSNAME", len)) {
			ret = get_server_itsname(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "NAME", len)) {
			ret = get_server_name(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "NICKNAME", len)) {
			ret = get_server_nickname(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "PASSWORD", len)) {
			ret = get_server_password(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "PORT", len)) {
			num = get_server_port(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "PADDR", len)) {
			ret = get_server_remote_paddr(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "LOCALPORT", len)) {
			num = get_server_local_port(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "QUIT_MESSAGE", len)) {
			if (!(ret = get_server_quit_message(refnum)))
				ret = empty_string;
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "SSL", len)) {
			ret = get_server_type(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "UMODE", len)) {
			ret = get_server_umode(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "UNIQUE_ID", len)) {
			ret = get_server_unique_id(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "USERHOST", len)) {
			ret = get_server_userhost(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "VERSION", len)) {
			ret = get_server_version_string(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "005", len)) {
			GET_FUNC_ARG(listc1, input);
			ret = get_server_005(refnum, listc1);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "005s", len)) {
			retval = get_server_005s(refnum, input);
			RETURN_MSTR(retval);
		} else if (!my_strnicmp(listc, "STATE", len)) {
			RETURN_STR(get_server_state_str(refnum));
		} else if (!my_strnicmp(listc, "STATUS", len)) {
			RETURN_STR(get_server_state_str(refnum));
		} else if (!my_strnicmp(listc, "ALTNAME", len)) {
			retval = get_server_altnames(refnum);
			RETURN_MSTR(retval);
		} else if (!my_strnicmp(listc, "ALTNAMES", len)) {
			retval = get_server_altnames(refnum);
			RETURN_MSTR(retval);
		} else if (!my_strnicmp(listc, "ADDRFAMILY", len)) {
			SSu a;

			a = get_server_remote_addr(refnum);
			if (family(&a) == AF_INET)
				RETURN_STR("ipv4");
			else if (family(&a) == AF_INET6)
				RETURN_STR("ipv6");
			else
				RETURN_STR("unknown");
		} else if (!my_strnicmp(listc, "PROTOCOL", len)) {
			RETURN_STR(get_server_type(refnum));
		} else if (!my_strnicmp(listc, "VHOST", len)) {
			RETURN_STR(get_server_vhost(refnum));
		} else if (!my_strnicmp(listc, "ADDRSLEFT", len)) {
			RETURN_INT(server_addrs_left(refnum));
		} else if (!my_strnicmp(listc, "AUTOCLOSE", len)) {
			RETURN_INT(get_server_autoclose(refnum));
		} else if (!my_strnicmp(listc, "FULLDESC", len)) {
			RETURN_STR("XXX TODO XXX");
		} else if (!my_strnicmp(listc, "CERT", len)) {
			ret = get_server_cert(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "REALNAME", len)) {
			RETURN_STR(get_server_realname(refnum));
		} else if (!my_strnicmp(listc, "DEFAULT_REALNAME", len)) {
			RETURN_STR(get_server_default_realname(refnum));
		} else if (!my_strnicmp(listc, "OPEN", len)) {
			RETURN_INT(is_server_open(refnum));
		} else if (!my_strnicmp(listc, "NEXT_SERVER_IN_GROUP", len)) {
			RETURN_INT(next_server_in_group(refnum, 1));
		} else if (!my_strnicmp(listc, "SSL_", 4)) {
			Server *s;

			if (!(s = get_server(refnum)) || !get_server_ssl_enabled(refnum))
				RETURN_EMPTY;

			if (!my_strnicmp(listc, "SSL_CIPHER", len)) {
				RETURN_STR(get_ssl_cipher(s->des));
			} else if (!my_strnicmp(listc, "SSL_VERIFY_RESULT", len)) {
				RETURN_EMPTY;	/* XXX :( */
			} else if (!my_strnicmp(listc, "SSL_VERIFY_ERROR", len)) {
				RETURN_INT(get_ssl_verify_error(s->des));
			} else if (!my_strnicmp(listc, "SSL_PEM", len)) {
				RETURN_STR(get_ssl_pem(s->des));
			} else if (!my_strnicmp(listc, "SSL_CERT_HASH", len)) {
				RETURN_STR(get_ssl_cert_hash(s->des));
			} else if (!my_strnicmp(listc, "SSL_PKEY_BITS", len)) {
				RETURN_INT(get_ssl_pkey_bits(s->des));
			} else if (!my_strnicmp(listc, "SSL_SUBJECT", len)) {
				RETURN_STR(get_ssl_subject(s->des));
			} else if (!my_strnicmp(listc, "SSL_SUBJECT_URL", len)) {
				RETURN_STR(get_ssl_u_cert_subject(s->des));
			} else if (!my_strnicmp(listc, "SSL_ISSUER", len)) {
				RETURN_STR(get_ssl_issuer(s->des));
			} else if (!my_strnicmp(listc, "SSL_ISSUER_URL", len)) {
				RETURN_STR(get_ssl_u_cert_issuer(s->des));
			} else if (!my_strnicmp(listc, "SSL_VERSION", len)) {
				RETURN_STR(get_ssl_ssl_version(s->des));
			} else if (!my_strnicmp(listc, "SSL_CHECKHOST_RESULT", len)) {
				RETURN_EMPTY;	/* XXX :( */
			} else if (!my_strnicmp(listc, "SSL_CHECKHOST_ERROR", len)) {
				RETURN_INT(get_ssl_checkhost_error(s->des));
			} else if (!my_strnicmp(listc, "SSL_SELF_SIGNED", len)) {
				RETURN_EMPTY;	/* XXX :( */
			} else if (!my_strnicmp(listc, "SSL_SELF_SIGNED_ERROR", len)) {
				RETURN_INT(get_ssl_self_signed_error(s->des));
			} else if (!my_strnicmp(listc, "SSL_OTHER_ERROR", len)) {
				RETURN_INT(get_ssl_other_error(s->des));
			} else if (!my_strnicmp(listc, "SSL_MOST_SERIOUS_ERROR", len)) {
				RETURN_INT(get_ssl_most_serious_error(s->des));
			} else if (!my_strnicmp(listc, "SSL_SANS", len)) {
				RETURN_STR(get_ssl_sans(s->des));
			} else if (!my_strnicmp(listc, "SSL_ACCEPT_CERT", len)) {
				RETURN_INT(get_server_accept_cert(refnum));
			}
		}
	} else if (!my_strnicmp(listc, "SET", len)) {
		GET_INT_ARG(refnum, input);
		if (!get_server(refnum))
			RETURN_EMPTY;

		GET_FUNC_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "AWAY", len)) {
			set_server_away_message(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "AWAY_STATUS", len)) {
			int	value;
			GET_INT_ARG(value, input);
			set_server_away_status(refnum, value);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "MAXCACHESIZE", len)) {
			int	size;
			GET_INT_ARG(size, input);
			set_server_max_cached_chan_size(refnum, size);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "MAXISON", len)) {
			int	size;
			GET_INT_ARG(size, input);
			set_server_ison_max(refnum, size);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "MAXUSERHOST", len)) {
			int	size;
			GET_INT_ARG(size, input);
			set_server_userhost_max(refnum, size);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "ISONLEN", len)) {
			int	size;
			GET_INT_ARG(size, input);
			set_server_ison_len(refnum, size);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "CAP_HOLD", len)) {
			int	value;
			GET_INT_ARG(value, input);
			set_server_cap_hold(refnum, value);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "CERT", len)) {
			set_server_cert(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "CONNECTED", len)) {
			RETURN_EMPTY;		/* Read only. */
		} else if (!my_strnicmp(listc, "COOKIE", len)) {
			set_server_cookie(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "GROUP", len)) {
			set_server_group(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "ITSNAME", len)) {
			set_server_itsname(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "NAME", len)) {
			set_server_name(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "NICKNAME", len)) {
			change_server_nickname(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "PASSWORD", len)) {
			set_server_password(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "PORT", len)) {
			int port;

			GET_INT_ARG(port, input);
			set_server_port(refnum, port);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "PRIMARY", len)) {
			primary_server = refnum;
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "QUIT_MESSAGE", len)) {
			set_server_quit_message(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "SSL", len)) {
			set_server_server_type(refnum, input);
		} else if (!my_strnicmp(listc, "TLS", len)) {
			set_server_server_type(refnum, input);
		} else if (!my_strnicmp(listc, "UMODE", len)) {
			if (is_server_open(refnum) == 0) {
				clear_user_modes(refnum);
				update_server_umode(refnum, input);
				RETURN_INT(1);
			}
			RETURN_EMPTY;		/* Read only for now */
		} else if (!my_strnicmp(listc, "UNIQUE_ID", len)) {
			set_server_unique_id(refnum, input);
		} else if (!my_strnicmp(listc, "USERHOST", len)) {
			set_server_userhost(refnum, input);
		} else if (!my_strnicmp(listc, "VERSION", len)) {
			set_server_version_string(refnum, input);
		} else if (!my_strnicmp(listc, "VHOST", len)) {
			set_server_vhost(refnum, input);
		} else if (!my_strnicmp(listc, "005", len)) {
			GET_FUNC_ARG(listc1, input);
			set_server_005(refnum, listc1, input);
			RETURN_INT(!!*input);
		} else if (!my_strnicmp(listc, "ALTNAME", len)) {
			add_server_altname(refnum, input);
		} else if (!my_strnicmp(listc, "ALTNAMES", len)) {
			reset_server_altnames(refnum, input);
		} else if (!my_strnicmp(listc, "AUTOCLOSE", len)) {
			int newval;

			GET_INT_ARG(newval, input);
			set_server_autoclose(refnum, newval);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "REALNAME", len)) {
			set_server_default_realname(refnum, input);
		} else if (!my_strnicmp(listc, "DEFAULT_REALNAME", len)) {
			set_server_default_realname(refnum, input);
		} else if (!my_strnicmp(listc, "SSL_", 4)) {
			if (!get_server(refnum) || !get_server_ssl_enabled(refnum))
				RETURN_EMPTY;

			if (!my_strnicmp(listc, "SSL_ACCEPT_CERT", len)) {
				int	val = 0;
				val = my_atol(input);
				set_server_accept_cert(refnum, val);
			}
			RETURN_INT(1);
		}
	} else if (!my_strnicmp(listc, "OMATCH", len)) {
		int	i;

		for (i = 0; i < number_of_servers; i++)
			if (get_server(i) && wild_match(input, get_server_name(i)))
				malloc_strcat_wordlist(&retval, space, ltoa(i));
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "IMATCH", len)) {
		int	i;

		for (i = 0; i < number_of_servers; i++)
			if (get_server(i) && wild_match(input, get_server_itsname(i)))
				malloc_strcat_wordlist(&retval, space, ltoa(i));
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "GMATCH", len)) {
		int	i;

		for (i = 0; i < number_of_servers; i++)
			if (get_server(i) && wild_match(input, get_server_group(i)))
				malloc_strcat_wordlist(&retval, space, ltoa(i));
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "MAX", len)) {
		RETURN_INT(number_of_servers);
	} else if (!my_strnicmp(listc, "READ_FILE", len)) {
		serverdesc_import_file(input);
	} else if (!my_strnicmp(listc, "RESET", len)) {
		GET_INT_ARG(refnum, input);
		if (!get_server(refnum))
			RETURN_EMPTY;

		server_close_soft(refnum);
		RETURN_INT(1);
	} else
		RETURN_EMPTY;

	RETURN_EMPTY;
}

/*
 * got_my_userhost: A callback function to userhostbase()
 *
 * Parameters:
 *	refnum	- A server refnum
 *	item	- The result of a USERHOST request
 *	nick	- The value passed to 'args' (for here, it is NULL)
 *	stuff	- The value passed to 'subargs' (for here, it is NULL)
 *
 * When you connect to a server, we ask do a USERHOST for ourselves, so we
 * know what our public IP address is.  This is needed for 
 * for CTCP FINGER, the $X expando, and 
 * (in the future) for determining how long protocol messages can be.
 * 
 * XXX I suppose this doesn't belong here.  But where else shall it go?
 */
static void 	got_my_userhost (int refnum, UserhostItem *__U(item), const char *__U(nick), const char *__U(stuff))
{
	char *freeme;

	freeme = malloc_strdup3(item->user, "@", item->host);
	set_server_userhost(refnum, freeme);
	new_free(&freeme);
}


/*
 * server_more_addrs: Are there IPs for this server we haven't tried yet?
 *
 * Parameters:
 *	refnum	- A server refnum
 *
 * Return value:
 *	0	- "refnum" is not a valid server refnum
 *		  -or- There are no untried IPs for "refnum"
 *	1	- There are untried IPs for "refnum"
 *
 * This is used by window_check_servers() which is a failsafe function.
 * If you're trying to connect to a server and a connection gets 
 * "stuck" or you lose patience, you might do a /reconnect or /disconnect.
 * This results in a window connected to a server that is CLOSED; however
 * window_check_servers() will call here to find out if there are other
 * IP addresses that can be tried, and if so, it resets the server to 
 * READY.  This is what allows /reconnect to do the right thing if you
 * try to connect to an IP address that is unreachable.
 */
int	server_more_addrs (int refnum)
{
	Server	*s;

	if (!(s = get_server(refnum)))
		return 0;

	if (s->addr_counter < s->addrs_total)
		return 1;
	else
		return 0;
}

/*
 * server_addrs_left: How many DNS entries do we have left to try?
 *
 * Parameters:
 *	refnum	- A server refnum
 *
 * Return value:
 *	0	- "refnum" is not a valid server refnum
 *	>0	- The number of dns entries still untried.
 *
 * When you connect to a server, the client will do a dns lookup
 * on the "ourname", which will return 0 or more IP addresses.
 * Each of them are tried in sequence until one of them results 
 * in a server that accepts us.  During the period of time between
 * our DNS lookup and our receiving a 001 numeric, we keep the 
 * addresses around.  This will tell you how many are left to go
 * before we give up.
 *
 * This is used by $serverctl(GET x ADDRSLEFT).
 */
static int	server_addrs_left (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	return s->addrs_total - s->addr_counter;
}

Server *      get_server (int server)
{
        if (server == -1 && from_server >= 0)
                server = from_server;
        if (server < 0 || server >= number_of_servers)
                return NULL;
        return server_list[server];
}


/* This was moved from ircaux.c */
static char *  get_my_fallback_userhost (void)
{
        static char 	uh[BIG_BUFFER_SIZE];
        const char *	u;
	char 		h[NAME_LEN + 1];

	u = get_string_var(DEFAULT_USERNAME_VAR);
	if (!u || !*u)
		u = "Unknown";
	gethostname(h, sizeof(h));

	snprintf(uh, sizeof(uh), "%s@%s", u, h);
        return uh;
}

