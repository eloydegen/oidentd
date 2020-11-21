/*
** solaris4.c - SunOS 5.4 kernel access functions
**
** Copyright (c) 1995-1997 Casper Dik     <Casper.Dik@Holland.Sun.COM>
** Copyright (c) 1997-1999 Peter Eriksson <pen@lysator.liu.se>
** Copyright (c) 2001-2006 Ryan McCabe    <ryan@numb.org>
** Copyright (c) 2018-2019 Janik Rabe     <info@janikrabe.com>
**
** This program is free software; you can redistribute it and/or
** modify it as you wish - as long as you don't claim that you wrote
** it.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include <config.h>

#define _KMEMUSER
#define _KERNEL

/* some definition conflicts. but we must define _KERNEL */

#define exit			kernel_exit
#define strsignal		kernel_strsignal

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/param.h>
#include <netinet/in.h>

#include <stdio.h>
#include <kvm.h>
#include <nlist.h>
#include <math.h>
#include <sys/fcntl.h>
#include <sys/cred.h>
#include <sys/file.h>
#include <sys/stream.h>
#include <inet/common.h>
#include <inet/ip.h>

#undef exit
#undef strsignal

#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <syslog.h>
#include <pwd.h>

#include "oidentd.h"
#include "util.h"
#include "inet_util.h"
#include "missing.h"

#define N_FANOUT		0
#define N_PRACTIVE		1

static struct kainfo {
	kvm_t *kd;
	struct proc *nextp;
	struct proc currentp;
	struct nlist nl[3];
} *kinfo;

#define FANOUT_OFFSET(x) (kinfo->nl[N_FANOUT].n_value + sizeof(ipc_t *) * (x))

static int getbuf(kvm_t *kd, off_t addr, void *dst, size_t len);

/*
** This is needed as stdlib.h can't be included as it causes
** a clash with exit(), as declared by another header file.
*/

extern void free(void *ptr);

/*
** Workaround for Solaris 2.x bug in kvm_setproc,
** kvm_setproc doesn't reread practive.
*/

static int xkvm_setproc(struct kainfo *kp) {
	int ret;

	ret = getbuf(kp->kd, (off_t) kp->nl[N_PRACTIVE].n_value, &kp->nextp,
			sizeof(kp->nextp));

	return ret;
}

static struct proc *xkvm_nextproc(struct kainfo *kp) {
	int ret = getbuf(kp->kd, (off_t) kp->nextp, &kp->currentp,
				sizeof(kp->currentp));

	if (ret == -1)
		return NULL;

	kp->nextp = kp->currentp.p_next;

	return &kp->currentp;
}

/*
** Open the kernel memory device.
** Return 0 on success, or -1 with errno set.
*/

int k_open(void) {
#warning "Support for this version of Solaris is deprecated and may be removed in the future"
	o_log(LOG_CRIT, "Support for this version of Solaris is deprecated and may be removed in the future");

	kinfo = xmalloc(sizeof(struct kainfo));

	/*
	** Open the kernel memory device
	*/

	kinfo->kd = kvm_open(NULL, NULL, NULL, O_RDONLY, NULL);
	if (!kinfo->kd) {
		debug("kvm_open: %s", strerror(errno));
		free(kinfo);
		return -1;
	}

	kinfo->nl[0].n_name = "ipc_tcp_fanout";
	kinfo->nl[1].n_name = "practive";
	kinfo->nl[2].n_name = NULL;

	/*
	** Extract offsets to the needed variables in the kernel
	*/

	if (kvm_nlist(kinfo->kd, kinfo->nl) != 0) {
		debug("kvm_nlist: %s", strerror(errno));
		kvm_close(kinfo->kd);
		free(kinfo);
		return -1;
	}

	return 0;
}

/*
** Get a piece of kernel memory with error handling.
** Returns 0 if call succeeded, else -1.
*/

static int getbuf(kvm_t *kd, off_t addr, void *dst, size_t len) {
	int i;
	ssize_t status = -1;

	for (i = 0; i < 5; ++i) {
		status = kvm_read(kd, addr, dst, len);
		if (status >= 0)
			break;
	}

	if (status < 0)
		return -1;

	return 0;
}

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
	in_addr_t laddr4 = SIN4(laddr)->sin_addr.s_addr;
	in_addr_t faddr4 = SIN4(faddr)->sin_addr.s_addr;
	ipc_t *icp;
	ipc_t ic;
	u_int offset;
	u_int32_t zero = 0;
	file_t tf;
	queue_t sqr;
	struct proc *procp;
	int ret;
	in_port_t *ports;
	in_addr_t *locaddr, *raddr;
	ipc_t *alticp = NULL;
	u_int altoffset;

#if defined(BIG_ENDIAN) || defined(_BIG_ENDIAN)
	altoffset = fport >> 8;
#else
	altoffset = lport >> 8;
#endif

	altoffset ^= fport ^ lport;
	altoffset ^= SIN4(faddr)->sin_addr.S_un.S_un_b.s_b4;

	if (lport > 8 || fport != 0)
		altoffset ^= 1;

	altoffset &= 0xff;

	ret = getbuf(kip->kd, (off_t) FANOUT_OFFSET(altoffset),
			&alticp, sizeof(ipc_t *));

	if (ret == -1)
		alticp = NULL;

	offset = fport ^ lport;
	offset ^= (u_int) SIN4(faddr)->sin_addr.S_un.S_un_b.s_b4 ^ (offset >> 8);
	offset &= 0xff;

	ret = getbuf(kinfo->kd, (off_t) FANOUT_OFFSET(offset),
			&icp, sizeof(ipc_t *));

	if (ret == -1) {
		icp = alticp;
		alticp = NULL;
	}

	if (!icp)
		return MISSING_UID;

	locaddr = (in_addr_t *) &ic.ipc_tcp_laddr;
	raddr = (in_addr_t *) &ic.ipc_tcp_faddr;
	ports = (in_port_t *) &ic.ipc_tcp_ports;

	while (icp) {
		if (getbuf(kinfo->kd, (off_t) icp, &ic, sizeof(ic)) == -1)
			return MISSING_UID;

		if (fport == ports[0] && lport == ports[1] &&
			(!memcmp(&laddr4, locaddr, 4) || !memcmp(&zero, locaddr, 4)) &&
			!memcmp(&faddr4, raddr, 4))
		{
			break;
		}

		icp = ic.ipc_hash_next;

		if (!icp) {
			icp = alticp;
			alticp = NULL;
		}
	}

	if (!icp)
		return MISSING_UID;

	ret = getbuf(kip->kd, (off_t) ic.ipc_rq + offsetof(queue_t, q_stream),
			&sqr.q_stream, sizeof(sqr.q_stream));

	if (ret == -1)
		return MISSING_UID;

	/*
	** At this point sqr.qstream holds the pointer to the stream we're
	** interested in. Now we're going to find the file pointer
	** that refers to the vnode that refers to this stream stream
	*/

	if (xkvm_setproc(kinfo->kd) != 0)
		return MISSING_UID;

	while ((procp = xkvm_nextproc(kinfo->kd))) {
		struct uf_entry files[NFPCHUNK];
		int nfiles = procp->p_user.u_nofiles;
		off_t addr = (off_t) procp->p_user.u_flist;

		while (nfiles > 0) {
			int nread = nfiles > NFPCHUNK ? NFPCHUNK : nfiles;
			int size = nread * sizeof(struct uf_entry);
			int i;
			struct file *last = NULL;
			vnode_t vp;

			if (getbuf(kinfo->kd, addr, &files[0], size) == -1)
				return MISSING_UID;

			for (i = 0; i < nread; ++i) {
				if (files[i].uf_ofile == 0 || files[i].uf_ofile == last)
					continue;

				last = files[i].uf_ofile;

				if (getbuf(kinfo->kd, (off_t) last, &tf, sizeof(tf)) == -1)
					return MISSING_UID;

				if (!tf.f_vnode)
					continue;

				ret = getbuf(kinfo->kd,
						(off_t) tf.f_vnode + offsetof(vnode_t, v_stream),
						&vp.v_stream,
						sizeof(vp.v_stream));

				if (ret == -1)
					return MISSING_UID;

				if (vp.v_stream == sqr.q_stream) {
					cred_t cr;

					ret = getbuf(kinfo->kd, (off_t) tf.f_cred, &cr, sizeof(cr));
					if (ret == -1)
						return MISSING_UID;

					return cr.cr_ruid;
				}
			}

			nfiles -= nread;
			addr += size;
		}
	}

	return MISSING_UID;
}
