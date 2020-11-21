/*
** util.h - oidentd utility functions.
** Copyright (c) 1998-2006 Ryan McCabe <ryan@numb.org>
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
*/

#ifndef __OIDENTD_UTIL_H
#define __OIDENTD_UTIL_H

#ifndef MIN
#	define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif

typedef struct list {
	struct list *next;
	void *data;
} list_t;

struct udb_lookup_res {
	/*
	** 0 if no match was found or an error occurred
	** 1 if a local match was found
	** 2 if a non-local match was found and the reply sent to the client
	*/
	char status;

	/*
	** The matching UID if a local match was found
	** Otherwise, MISSING_UID as reserved by POSIX
	*/
	uid_t uid;
};

int o_log(int priority, const char *fmt, ...) __format((printf, 2, 3));
int drop_privs(uid_t new_uid, gid_t new_gid);
int go_background(void);

struct udb_lookup_res get_udb_user(	in_port_t lport,
					in_port_t fport,
					const struct sockaddr_storage *laddr,
					const struct sockaddr_storage *faddr,
					int sock);

FILE *safe_open(const struct passwd *pw, const char *filename);

void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *ptr, size_t len);

char *xstrncpy(char *dest, const char *src, size_t n);
char *xstrdup(const char *string);

list_t *list_prepend(list_t **list, void *new_data);
void list_destroy(list_t *list, void (*free_data)(void *));

int find_user(const char *temp_user, uid_t *uid);
int find_group(const char *temp_group, gid_t *gid);

int seed_prng(void);
unsigned long prng_next(void);
unsigned int randval(unsigned int i);

#endif
