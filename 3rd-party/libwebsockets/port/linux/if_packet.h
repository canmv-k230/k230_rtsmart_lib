/*
 * Stub <linux/if_packet.h> for RT-Smart (musl libc without Linux kernel headers)
 *
 * Only provides the minimum definitions needed by libwebsockets.
 */

#ifndef _STUB_LINUX_IF_PACKET_H
#define _STUB_LINUX_IF_PACKET_H

#include <sys/socket.h>

/* AF_PACKET socket address structure */
struct sockaddr_ll {
	unsigned short sll_family;
	unsigned short sll_protocol;
	int            sll_ifindex;
	unsigned short sll_hatype;
	unsigned char  sll_pkttype;
	unsigned char  sll_halen;
	unsigned char  sll_addr[8];
};

/* AF_PACKET is already defined in sys/socket.h on musl */
#ifndef AF_PACKET
#define AF_PACKET 17
#endif

#endif /* _STUB_LINUX_IF_PACKET_H */
