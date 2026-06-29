/*
 * Stub <linux/if_addr.h> for RT-Smart (musl libc without Linux kernel headers)
 *
 * Only provides the minimum definitions needed by libwebsockets.
 * The actual netlink routing code is guarded by LWS_WITH_IPV6 which
 * we don't enable, so this stub just needs to satisfy the include.
 */

#ifndef _STUB_LINUX_IF_ADDR_H
#define _STUB_LINUX_IF_ADDR_H

/* Interface address flags (also defined as fallback in sort-dns.c) */
#ifndef IFA_F_DEPRECATED
#define IFA_F_DEPRECATED 0
#endif
#ifndef IFA_F_HOMEADDRESS
#define IFA_F_HOMEADDRESS 0
#endif
#ifndef IFA_F_TEMPORARY
#define IFA_F_TEMPORARY 0
#endif

/* Netlink address message structure (unused without LWS_WITH_IPV6) */
struct ifaddrmsg {
	unsigned char ifa_family;
	unsigned char ifa_prefixlen;
	unsigned char ifa_flags;
	unsigned char ifa_scope;
	int           ifa_index;
};

#endif /* _STUB_LINUX_IF_ADDR_H */
