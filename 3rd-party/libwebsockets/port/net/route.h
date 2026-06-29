/*
 * Stub <net/route.h> for RT-Smart (musl libc without Linux kernel headers)
 *
 * Provides the minimum definitions needed by libwebsockets for
 * platform routing operations (WoL-related in unix-sockets.c).
 */

#ifndef _STUB_NET_ROUTE_H
#define _STUB_NET_ROUTE_H

#include <sys/socket.h>
#include <net/if.h>

/* Routing table entry */
struct rtentry {
	unsigned long rt_pad1;
	struct sockaddr rt_dst;
	struct sockaddr rt_gateway;
	struct sockaddr rt_genmask;
	unsigned short rt_flags;
	short          rt_pad2;
	unsigned long  rt_pad3;
	void          *rt_pad4;
	short          rt_metric;
	char          *rt_dev;
	unsigned long  rt_mtu;
	unsigned long  rt_window;
	unsigned short rt_irtt;
};

/* RT flags */
#define RTF_UP        0x0001
#define RTF_GATEWAY   0x0002
#define RTF_HOST      0x0004

/* Socket IOCTL for routing */
#define SIOCADDRT     0x890B

#endif /* _STUB_NET_ROUTE_H */
