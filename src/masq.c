/*
** masq.c - oidentd IP masquerading handler.
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

#include <config.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "oidentd.h"
#include "util.h"
#include "missing.h"
#include "inet_util.h"
#include "masq.h"
#include "options.h"
#include "forward.h"

struct sockaddr_storage proxy;

#if MASQ_SUPPORT

in_port_t fwdport;

extern char *ret_os;

static bool blank_line(const char *buf);

/*
** Returns true if the buffer contains only
** blank characters (spaces and/or tabs).  Returns
** false otherwise.
*/

static bool blank_line(const char *buf) {
	const char *p;

	for (p = buf; *p; ++p) {
		if (*p != ' ' && *p != '\t')
			return false;
	}

	return true;
}

/*
** Parse the masquerading map file.
** Returns 0 on success, -1 on failure.
*/

int find_masq_entry(struct sockaddr_storage *host,
					char *user,
					size_t user_len,
					char *os,
					size_t os_len)
{
	FILE *fp;
	struct sockaddr_storage addr;
	u_int32_t line_num;
	char buf[4096];

	fp = fopen(MASQ_MAP, "r");
	if (!fp) {
		if (errno != EEXIST)
			debug("fopen: %s: %s", MASQ_MAP, strerror(errno));
		return -1;
	}

	line_num = 0;

	while (fgets(buf, sizeof(buf), fp)) {
		struct sockaddr_storage stemp;
		char *p, *temp;

		++line_num;
		p = strchr(buf, '\n');
		if (!p) {
			debug("[%s:%d] Line too long", MASQ_MAP, line_num);
			goto failure;
		}
		*p = '\0';

		if (buf[0] == '#')
			continue;

		if (blank_line(buf))
			continue;

		p = strchr(buf, '\r');
		if (p)
			*p = '\0';

		p = strtok(buf, " \t");
		if (!p) {
			debug("[%s:%d] Missing address parameter", MASQ_MAP, line_num);
			goto failure;
		}

		temp = strchr(p, '/');
		if (temp)
			*temp++ = '\0';

		if (get_addr(p, &stemp) == -1) {
			debug("[%s:%d] Invalid address: %s", MASQ_MAP, line_num, p);
			goto failure;
		}

		sin_copy(&addr, &stemp);

		if (stemp.ss_family == AF_INET && temp) {
			in_addr_t mask, mask2;
			char *end;

			mask = strtoul(temp, &end, 10);

			if (*end != '\0') {
				if (get_addr(temp, &stemp) == -1) {
					debug("[%s:%d] Invalid address: %s",
						MASQ_MAP, line_num, temp);

					goto failure;
				}

				mask2 = SIN4(&stemp)->sin_addr.s_addr;
			} else {
				if (mask < 1 || mask > 31) {
					debug("[%s:%d] Invalid mask: %s",
						MASQ_MAP, line_num, temp);

					goto failure;
				}

				mask2 = htonl(~(((uint32_t) 1 << (32 - mask)) - 1));
			}

			SIN4(&addr)->sin_addr.s_addr &= mask2;
			SIN4(host)->sin_addr.s_addr &= mask2;
		}

		if (!sin_equal(&addr, host))
			continue;

		p = strtok(NULL, " \t");
		if (!p) {
			debug("[%s:%d] Missing user parameter", MASQ_MAP, line_num);
			goto failure;
		}

		if (strlen(p) >= user_len) {
			debug("[%s:%d] Username too long (limit is %ld)",
				MASQ_MAP, line_num, user_len);

			goto failure;
		}

		xstrncpy(user, p, user_len);

		p = strtok(NULL, " \t");
		if (!p) {
			debug("[%s:%d] Missing OS parameter", MASQ_MAP, line_num);

			goto failure;
		}

		if (strlen(p) >= os_len) {
			debug("[%s:%d] OS name too long (limit is %ld)",
				MASQ_MAP, line_num, os_len);

			goto failure;
		}

		xstrncpy(os, p, os_len);

		fclose(fp);
		return 0;
	}

failure:
	fclose(fp);
	return -1;
}

/*
** Forward an ident request to another machine, return the response to the
** client that has connected to us and requested it.
*/

int fwd_request(	int sock,
					in_port_t real_lport,
					in_port_t masq_lport,
					in_port_t real_fport,
					in_port_t masq_fport,
					struct sockaddr_storage *mrelay)
{
	char ipbuf[MAX_IPLEN];
	char user[512];
	int ret;

	ret = forward_request(mrelay, fwdport, masq_lport, masq_fport,
		user, sizeof user);
	if (ret == -1)
		return -1;

	sockprintf(sock, "%d,%d:USERID:%s:%s\r\n",
		real_lport, real_fport, ret_os, user);

	get_ip(mrelay, ipbuf, sizeof(ipbuf));
	o_log(LOG_INFO,
		"[%s] Successful lookup (by forward): %d (%d) , %d (%d) : %s",
		ipbuf, real_lport, masq_lport, real_fport, masq_fport, user);

	return 0;
}

#else

/*
** Handle a request to a host that's IP masquerading through us.
** Returns non-zero on failure.
*/

int masq(	int sock __notused,
			in_port_t lport __notused,
			in_port_t fport __notused,
			struct sockaddr_storage *local __notused,
			struct sockaddr_storage *remote __notused)
{
	return -1;
}

#endif
