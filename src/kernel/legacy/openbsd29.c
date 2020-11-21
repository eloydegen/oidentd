/*
** openbsd29.c - Low level kernel access functions for OpenBSD 2.9 and greater
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
*/

#include <config.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/tcp.h>
#include <netinet/ip_var.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#include "oidentd.h"
#include "util.h"
#include "inet_util.h"
#include "missing.h"
#include "options.h"

extern struct sockaddr_storage proxy;

/*
** System-dependent initialization; called only once.
** Called before privileges are dropped.
** Returns non-zero on failure.
*/

int core_init(void) {
	return 0;
}

/*
** Returns the UID of the owner of an IPv4 connection,
** or MISSING_UID on failure.
*/

uid_t get_user4(	in_port_t lport,
				in_port_t fport,
				struct sockaddr_storage *laddr,
				struct sockaddr_storage *faddr)
{
	struct tcp_ident_mapping tir;
	struct sockaddr_in *fin, *lin;
	int mib[] = { CTL_NET, PF_INET, IPPROTO_TCP, TCPCTL_IDENT };
	int error;
	size_t i;

	memset(&tir, 0, sizeof(tir));

	tir.faddr.ss_family = AF_INET;
	tir.faddr.ss_len = sizeof(struct sockaddr);
	fin = (struct sockaddr_in *) &tir.faddr;
	fin->sin_port = fport;

	if (!opt_enabled(PROXY) || !sin_equal(faddr, &proxy))
		memcpy(&fin->sin_addr, &SIN4(faddr)->sin_addr, sizeof(struct in_addr));

	tir.laddr.ss_family = AF_INET;
	tir.laddr.ss_len = sizeof(struct sockaddr);
	lin = (struct sockaddr_in *) &tir.laddr;
	lin->sin_port = lport;
	memcpy(&lin->sin_addr, &SIN4(laddr)->sin_addr, sizeof(struct in_addr));

	i = sizeof(tir);

	error = sysctl(mib, sizeof(mib) / sizeof(int), &tir, &i, NULL, 0);

	if (error == 0 && tir.ruid != -1)
		return tir.ruid;

	if (error == -1)
		debug("sysctl: %s", strerror(errno));

	return MISSING_UID;
}

#if WANT_IPV6

/*
** Returns the UID of the owner of an IPv6 connection,
** or MISSING_UID on failure.
*/

uid_t get_user6(	in_port_t lport,
				in_port_t fport,
				struct sockaddr_storage *laddr,
				struct sockaddr_storage *faddr)
{
	struct tcp_ident_mapping tir;
	struct sockaddr_in6 *fin;
	struct sockaddr_in6 *lin;
	int mib[] = { CTL_NET, PF_INET, IPPROTO_TCP, TCPCTL_IDENT };
	int error;
	size_t i;

	memset(&tir, 0, sizeof(tir));

	fin = (struct sockaddr_in6 *) &tir.faddr;
	fin->sin6_family = AF_INET6;
	fin->sin6_len = sizeof(struct sockaddr_in6);

	if (faddr->ss_len > sizeof(tir.faddr))
		return MISSING_UID;

	memcpy(&fin->sin6_addr, &SIN6(faddr)->sin6_addr, sizeof(tir.faddr));
	fin->sin6_port = fport;

	lin = (struct sockaddr_in6 *) &tir.laddr;
	lin->sin6_family = AF_INET6;
	lin->sin6_len = sizeof(struct sockaddr_in6);

	if (laddr->ss_len > sizeof(tir.laddr))
		return MISSING_UID;

	memcpy(&lin->sin6_addr, &SIN6(laddr)->sin6_addr, sizeof(tir.laddr));
	lin->sin6_port = lport;

	i = sizeof(tir);
	error = sysctl(mib, sizeof(mib) / sizeof(int), &tir, &i, NULL, 0);

	if (error == 0 && tir.ruid != -1)
		return tir.ruid;

	if (error == -1)
		debug("sysctl: %s", strerror(errno));

	return MISSING_UID;
}

#endif

/*
** Open the kernel memory device.
** Return 0 on success, or -1 with errno set.
**
** No kmem access required; nothing to do.
*/

int k_open(void) {
#warning "Support for this version of OpenBSD is deprecated and may be removed in the future"
	o_log(LOG_CRIT, "Support for this version of OpenBSD is deprecated and may be removed in the future");

	return 0;
}
