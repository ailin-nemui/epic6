/*
 * names.h: Header for names.c
 *
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __names_h__
#define __names_h__

	void	add_channel		(const char *, int); 
	void	remove_channel		(const char *, int);
	void	add_to_channel		(const char *, const char *, int, int, int, int, int);
	void	add_userhost_to_channel	(const char *, const char *, int, const char *);
	void	remove_from_channel	(const char *, const char *, int);
	void	rename_nick		(const char *, const char *, int);
	const char *	check_channel_type	(const char *);
	int	im_on_channel		(const char *, int);
	int	is_on_channel		(const char *, const char *);
	int	is_chanop		(const char *, const char *);
	int	is_chanvoice		(const char *, const char *);
	int	is_halfop		(const char *, const char *);
	int	number_on_channel	(const char *, int);
	char *	create_nick_list	(const char *, int);
	char *	create_chops_list	(const char *, int);
	char *	create_nochops_list	(const char *, int);
	int     chanmodetype		(char);
	int	channel_is_syncing	(const char *, int);
	void	channel_not_waiting	(const char *, int); 
	void	update_channel_mode	(const char *, const char *);
	const char *	get_channel_key		(const char *, int);
	const char *	get_channel_mode	(const char *, int);
	int	is_channel_private	(const char *, int);
	int	is_channel_nomsgs	(const char *, int);
	int	is_channel_anonymous	(const char *, int);
	char *	scan_channel		(char *);
	void	list_channels		(void);
	BUILT_IN_KEYBINDING(switch_channels);
	const char *	window_current_channel	(int, int);
	char *	window_all_channels	(int, int);
	int	is_current_channel	(const char *, int);
	void	destroy_server_channels	(int);
	const char *	what_channel		(const char *, int);
	const char *	walk_channels		(int, const char *);
	const char *	fetch_userhost		(int, const char *, const char *);
	int	get_channel_limit	(const char *, int);
	int	get_channel_oper	(const char *, int);
	int	get_channel_voice	(const char *, int);
	int     get_channel_halfop	(const char *, int);
	int	get_channel_window	(const char *, int);
	void	set_channel_window	(const char *, int, int, int);
	void	move_channel_to_window	(const char *, int, int, int);
	void	reassign_window_channels (int);
	char *	create_channel_list	(int);
	void	cant_join_channel	(const char *, int);
	int	auto_rejoin_callback	(void *);
	void	channel_server_delete	(int);
	void	channel_check_windows	(void);
	void    channels_swap_windows 	(int oldref, int newref);
	void    channels_merge_windows 	(int oldref, int newref);
	int     window_claims_channel 	(int window, int winserv, const char *channel);

#endif /* _NAMES_H_ */
