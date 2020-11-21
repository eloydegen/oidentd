%{
/*
** cfg_parse.y - oidentd configuration parser.
** Copyright (c) 2001-2006 Ryan McCabe <ryan@numb.org>
** Copyright (c) 2018-2019 Janik Rabe  <info@janikrabe.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License, version 2,
** as published by the Free Software Foundation.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
**
*/

#include <config.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <pwd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "oidentd.h"
#include "util.h"
#include "missing.h"
#include "inet_util.h"
#include "user_db.h"
#include "options.h"

extern struct user_info *default_user;
extern u_int32_t current_line;
extern int parser_mode;

static FILE *open_user_config(const struct passwd *pw);
static int extract_port_range(const char *token, struct port_range *range);
static void free_cap_entries(struct user_cap *free_cap);
static void yyerror(const char *err);

void yyrestart(FILE *fp);
int yylex();

static struct user_info *cur_user;
static struct user_cap *cur_cap;
list_t *pref_list;

u_int16_t default_caps;

%}

%union {
	int value;
	char *string;
}

%token TOK_USER
%token TOK_DEFAULT
%token TOK_GLOBAL
%token TOK_FROM
%token TOK_TO
%token TOK_FPORT
%token TOK_LPORT
%token TOK_FORCE
%token TOK_REPLY
%token TOK_FORWARD
%token <value> TOK_ALLOWDENY
%token <value> TOK_CAP
%token <string> TOK_STRING

%%

program:
	/* empty */
|
	program parse_global
;

parse_global:
	user_rule
|
	{
		if (parser_mode != PARSE_USER) {
			o_log(LOG_CRIT,
				"[line %d] This construct is valid only for user configuration files", current_line);
			YYABORT;
		}

		cur_cap = xcalloc(1, sizeof(struct user_cap));
		cur_cap->caps = default_caps;
	} user_range_rule {
		list_prepend(&pref_list, cur_cap);
	}
;

user_rule:
	user_statement
|
	default_statement
;

default_statement:
	TOK_DEFAULT {
		if (parser_mode != PARSE_SYSTEM)
			YYABORT;

		cur_user = xmalloc(sizeof(struct user_info));
		cur_user->cap_list = NULL;

		user_db_set_default(cur_user);
	} '{' target_rule '}'
;

user_statement:
	TOK_USER TOK_STRING {
		if (parser_mode != PARSE_SYSTEM) {
			free($2);
			YYABORT;
		}

		cur_user = xmalloc(sizeof(struct user_info));
		cur_user->cap_list = NULL;

		if (find_user($2, &cur_user->user) != 0) {
			o_log(LOG_CRIT, "[line %u] Invalid user: \"%s\"", current_line, $2);
			free($2);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		if (user_db_lookup(cur_user->user)) {
			o_log(LOG_CRIT,
				"[line %u] User \"%s\" already has a capability entry",
				current_line, $2);
			free($2);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		free($2);
	} '{' target_rule '}'
	{
		user_db_add(cur_user);
	}
;

target_rule:
	target_statement
|
	target_rule target_statement
;

target_statement:
	{
		cur_cap = xcalloc(1, sizeof(struct user_cap));
		cur_cap->caps = default_caps;
	} range_rule {
		list_prepend(&cur_user->cap_list, cur_cap);
	}
;

range_rule:
	TOK_DEFAULT '{' cap_rule '}' {
		if (cur_user == default_user)
			default_caps = cur_cap->caps;
	}
|
	rule_specification_list_req '{' cap_rule '}'
;

rule_specification_list_req:
	rule_specification_list rule_specification
;

rule_specification_list:
	/* empty */
|
	rule_specification_list rule_specification
;

rule_specification:
	to_statement
|
	fport_statement
|
	from_statement
|
	lport_statement
;

to_statement:
	TOK_TO TOK_STRING {
		if (cur_cap->dest) {
			if (parser_mode == PARSE_SYSTEM) {
				o_log(LOG_CRIT, "[line %u] 'to' can only be specified once",
					current_line);
			}

			free($2);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		cur_cap->dest = xmalloc(sizeof(struct sockaddr_storage));

		if (get_addr($2, cur_cap->dest) == -1) {
			if (parser_mode == PARSE_SYSTEM) {
				o_log(LOG_CRIT, "[line %u] Bad address: \"%s\"",
					current_line, $2);
			}

			free($2);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		free($2);
	}
;

fport_statement:
	TOK_FPORT TOK_STRING {
		if (cur_cap->fport) {
			if (parser_mode == PARSE_SYSTEM) {
				o_log(LOG_CRIT, "[line %u] 'fport' can only be specified once",
					current_line);
			}

			free($2);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		cur_cap->fport = xmalloc(sizeof(struct port_range));

		if (extract_port_range($2, cur_cap->fport) == -1) {
			if (parser_mode == PARSE_SYSTEM)
				o_log(LOG_CRIT, "[line %u] Bad port or port range: \"%s\"", current_line, $2);

			free($2);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		free($2);
	}
;

from_statement:
	TOK_FROM TOK_STRING {
		if (cur_cap->src) {
			if (parser_mode == PARSE_SYSTEM) {
				o_log(LOG_CRIT, "[line %u] 'from' can only be specified once",
					current_line);
			}

			free($2);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		cur_cap->src = xmalloc(sizeof(struct sockaddr_storage));

		if (get_addr($2, cur_cap->src) == -1) {
			if (parser_mode == PARSE_SYSTEM) {
				o_log(LOG_CRIT, "[line %u] Bad address: \"%s\"",
					current_line, $2);
			}

			free($2);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		free($2);
	}
;

lport_statement:
	TOK_LPORT TOK_STRING {
		if (cur_cap->lport) {
			if (parser_mode == PARSE_SYSTEM) {
				o_log(LOG_CRIT, "[line %u] 'lport' can only be specified once",
					current_line);
			}

			free($2);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		cur_cap->lport = xmalloc(sizeof(struct port_range));

		if (extract_port_range($2, cur_cap->lport) == -1) {
			if (parser_mode == PARSE_SYSTEM)
				o_log(LOG_CRIT, "[line %u] Bad port or port range: \"%s\"", current_line, $2);

			free($2);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		free($2);
	}
;

cap_rule:
	cap_statement
|
	cap_rule cap_statement
;

force_reply:
	TOK_FORCE TOK_REPLY TOK_STRING {
		cur_cap->caps = CAP_REPLY;
		cur_cap->action = ACTION_FORCE;
		cur_cap->data.replies.num = 1;
		cur_cap->data.replies.data = xrealloc(cur_cap->data.replies.data,
			sizeof(u_char *));
		cur_cap->data.replies.data[0] = $3;
	}
|
	force_reply TOK_STRING {
		if (cur_cap->data.replies.num < 0xFF) {
			cur_cap->data.replies.data = xrealloc(cur_cap->data.replies.data,
				++cur_cap->data.replies.num * sizeof(u_char *));
			cur_cap->data.replies.data[cur_cap->data.replies.num - 1] = $2;
		} else {
			o_log(LOG_CRIT, "[line %u] No more than 255 replies may be specified",
				current_line);
			free_cap_entries(cur_cap);
			YYABORT;
		}
	}
;

force_forward:
	TOK_FORCE TOK_FORWARD TOK_STRING TOK_STRING {
		cur_cap->caps = CAP_FORWARD;
		cur_cap->action = ACTION_FORCE;
		cur_cap->data.forward.host = xmalloc(sizeof(struct sockaddr_storage));

		if (get_addr($3, cur_cap->data.forward.host) == -1) {
			if (parser_mode == PARSE_SYSTEM) {
				o_log(LOG_CRIT, "[line %u] Bad address: \"%s\"",
					current_line, $3);
			}

			free($3); free($4);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		if (get_port($4, &cur_cap->data.forward.port) == -1) {
			if (parser_mode == PARSE_SYSTEM)
				o_log(LOG_CRIT, "[line %u] Bad port: \"%s\"", current_line, $4);

			free($3); free($4);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		free($3); free($4);
	}
;

cap_statement:
	TOK_FORCE TOK_CAP {
		cur_cap->caps = $2;
		cur_cap->action = ACTION_FORCE;
	}
|
	force_reply
|
	force_forward
|
	TOK_ALLOWDENY TOK_CAP {
		if ($1 == ACTION_ALLOW)
			cur_cap->caps |= $2;
		else
			cur_cap->caps &= ~$2;

		cur_cap->action = $1;
	}
|
	TOK_ALLOWDENY TOK_FORWARD {
		if ($1 == ACTION_ALLOW)
			cur_cap->caps |= CAP_FORWARD;
		else
			cur_cap->caps &= ~CAP_FORWARD;

		cur_cap->action = $1;
	}
;

user_range_rule:
	TOK_GLOBAL '{' user_cap_rule '}'
|
	rule_specification_list_req '{' user_cap_rule '}'
;

user_reply:
	TOK_REPLY TOK_STRING {
		cur_cap->caps = CAP_REPLY;
		cur_cap->data.replies.num = 1;
		cur_cap->data.replies.data = xrealloc(cur_cap->data.replies.data,
			sizeof(u_char *));
		cur_cap->data.replies.data[0] = $2;
	}
|
	user_reply TOK_STRING {
		if (cur_cap->data.replies.num < MAX_RANDOM_REPLIES
				&& cur_cap->data.replies.num < 0xFF) {
			cur_cap->data.replies.data = xrealloc(cur_cap->data.replies.data,
				++cur_cap->data.replies.num * sizeof(u_char *));
			cur_cap->data.replies.data[cur_cap->data.replies.num - 1] = $2;
		}
	}
;

user_forward:
	TOK_FORWARD TOK_STRING TOK_STRING {
		cur_cap->caps = CAP_FORWARD;
		cur_cap->data.forward.host = xmalloc(sizeof(struct sockaddr_storage));

		if (get_addr($2, cur_cap->data.forward.host) == -1) {
			if (parser_mode == PARSE_SYSTEM) {
				o_log(LOG_CRIT, "[line %u] Bad address: \"%s\"",
					current_line, $2);
			}

			free($2); free($3);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		if (get_port($3, &cur_cap->data.forward.port) == -1) {
			if (parser_mode == PARSE_SYSTEM)
				o_log(LOG_CRIT, "[line %u] Bad port: \"%s\"", current_line, $3);

			free($2); free($3);
			free_cap_entries(cur_cap);
			YYABORT;
		}

		free($2); free($3);
	}
;

user_cap_rule:
	TOK_CAP {
		if ($1 == CAP_SPOOF || $1 == CAP_SPOOF_ALL || $1 == CAP_SPOOF_PRIVPORT)
		{
			free_cap_entries(cur_cap);
			YYABORT;
		}

		cur_cap->caps = $1;
	}
|
	user_reply
|
	user_forward
;

%%

/*
** Read in the system-wide configuration file.
*/

int read_config(const char *path) {
	FILE *fp;
	int ret;

	fp = fopen(path, "r");
	if (!fp) {
		if (errno == ENOENT) {
			/*
			** If a configuration file is specified on the
			** command line, return an error if it can't be opened,
			** even if it doesn't exist.
			*/

			if (!strcmp(path, CONFFILE)) {
				struct user_info *temp_default;

				temp_default = user_db_create_default();
				user_db_set_default(temp_default);
				return 0;
			}
		}

		o_log(LOG_CRIT, "Error opening configuration file: %s: %s",
			path, strerror(errno));
		return -1;
	}

	yyrestart(fp);
	current_line = 1;
	parser_mode = PARSE_SYSTEM;
	ret = yyparse();

	fclose(fp);

	/*
	** Make sure there's a default to fall back on.
	*/

	if (!default_user) {
		struct user_info *temp_default;

		temp_default = user_db_create_default();
		user_db_set_default(temp_default);
	}

	return ret;
}

/*
** Open the user's configuration file for reading by the parser.
*/

static FILE *open_user_config(const struct passwd *pw) {
	FILE *fp = NULL;

#if XDGBDIR_SUPPORT
	if (!fp)
		fp = safe_open(pw, USER_CONF_XDG);
#endif

	if (!fp)
		fp = safe_open(pw, USER_CONF);

	if (!fp)
		return NULL;

	yyrestart(fp);
	current_line = 1;
	parser_mode = PARSE_USER;

	return fp;
}

/*
** Read in a user's configuration file.
*/

list_t *user_db_get_pref_list(const struct passwd *pw) {
	FILE *fp;
	int ret;

	fp = open_user_config(pw);
	if (!fp)
		return NULL;

	cur_cap = NULL;
	pref_list = NULL;

	ret = yyparse();
	fclose(fp);

	if (ret != 0) {
		list_destroy(pref_list, user_db_cap_destroy_data);
		return NULL;
	}

	return pref_list;
}

static void yyerror(const char *err) {
	if (parser_mode == PARSE_USER)
		free_cap_entries(cur_cap);
	else
		o_log(LOG_CRIT, "[line %u] %s", current_line, err);
}

/*
** Extract a port range from a token.
*/

static int extract_port_range(const char *token, struct port_range *range) {
	char *p;

	p = strchr(token, ':');
	if (p)
		*p++ = '\0';

	if (*token == '\0')
		range->min = PORT_MIN;
	else if (get_port(token, &range->min) == -1)
		return -1;

	if (!p) {
		range->max = range->min;
	} else {
		if (*p == '\0')
			range->max = PORT_MAX;
		else if (get_port(p, &range->max) == -1)
			return -1;
	}

	return 0;
}

static void free_cap_entries(struct user_cap *free_cap) {
	user_db_cap_destroy_data(free_cap);

	if (free_cap != cur_cap)
		free(cur_cap);

	free(free_cap);
	free(cur_user);

	cur_cap = NULL;
	cur_user = NULL;
	pref_list = NULL;
}
