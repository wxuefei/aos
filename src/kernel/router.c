/*_
 * Copyright 2014 Scyphus Solutions Co. Ltd.  All rights reserved.
 *
 * Authors:
 *      Hirochika Asai  <asai@scyphus.co.jp>
 */

#include <aos/const.h>
#include "kernel.h"

/* Temporary */
int arch_dbg_printf(const char *fmt, ...);

static int lock;
extern struct netdev_list *netdev_head;

#define ND_TABLE_SIZE           4096
#define ARP_TABLE_SIZE          4096
#define NAT66_TABLE_SIZE        65536
#define ARP_TIMEOUT             300
#define KTXBUF_SIZE             768


typedef int (*router_rx_cb_t)(const u8 *, u32, int);

int e1000_routing(struct netdev *, router_rx_cb_t);
int e1000_tx_buf(struct netdev *, u8 **, u16 **, u16);
int e1000_tx_commit(struct netdev *);
int e1000_tx_set(struct netdev *, u64, u16, u16);






struct iphdr {
    u8 ip_vhl;
    u8 ip_tos;
    u16 ip_len;
    u16 ip_id;
    u16 ip_off;
    u8 ip_ttl;
    u8 ip_proto;
    u16 ip_sum;
    u8 ip_src[4];
    u8 ip_dst[4];
} __attribute__ ((packed));

struct icmp_hdr {
    u8 type;
    u8 code;
    u16 checksum;
    u16 ident;
    u16 seq;
    // data...
} __attribute__ ((packed));

struct ip6hdr {
    u32 ip6_vtf;
    u16 ip6_len;
    u8 ip6_next;
    u8 ip6_limit;
    u8 ip6_src[16];
    u8 ip6_dst[16];
} __attribute__ ((packed));

struct icmp6_hdr {
    u8 type;
    u8 code;
    u16 checksum;
    // data...
} __attribute__ ((packed));


struct router_nat4 {

};

struct nat66_entry {
    u8 orig_addr[16];
    u8 priv_addr[16];
    u64 expire;
    int state;
};
struct router_nat66 {
    int sz;
    struct nat66_entry *ent;
};



/* FIXME to use buffer directly */
struct ktxdesc {
    u64 address;
    u16 length;
    u16 vlan;
    int status;
};

struct ktxbuf {
    struct ktxdesc *buf;
    u32 bufsz;
    u32 head;
    u32 tail;
};



struct arp_entry {
    u8 protoaddr[4];
    u8 hwaddr[6];
    /*void *netif;*/
    u64 expire;
    int state;
};
struct router_arp {
    int sz;
    struct arp_entry *ent;
};
struct nd_entry {
    u8 neighbor[16];
    u8 linklayeraddr[6];
    void *netif;
    u64 expire;
    int state;
};
struct router_nd {
    int sz;
    struct nd_entry *ent;
};

struct router {
    struct router_nat66 nat66;
};

#if 0
/* RIB */
struct rib6 {
    u8 ipaddr[16];
    int preflen;
    /* Outgoing interface */
    struct netdev *next;
    int vlan;
};
/* RIB+ND */
struct fib6 {
    struct rib6 *rib6;
    u8 dstmac[6];
};
#endif

struct ipv4_route {
    u8 addr[4];
    u8 mask;
};

struct ipv4_addr {
    u8 addr[4];
    u8 mask;
    int flags;
};
struct ipv4_addr_list {
    struct ipv4_addr *addr;
    struct ipv4_addr_list *next;
};
struct ipv6_addr {
    u8 addr[16];
    u8 preflen;
    int scope;
    int flags;
};
struct ipv6_addr_list {
    struct ipv6_addr *addr;
    struct ipv6_addr_list *next;
};
struct l3if {
    char *name;
    struct netdev *netdev;
    int vlan;
    struct ipv4_addr_list *ip4list;
    struct ipv6_addr_list *ip6list;
    /* Neighbor info */
    struct router_arp arp;
    struct router_nd nd;
    /* Buffer */
    struct ktxbuf txbuf;
};
struct l3if_list {
    struct l3if *l3if;
    struct l3if_list *next;
};

static struct l3if_list *l3if_head;

/*
680
nat-pool-panda
203.178.158.192/26
2001:200:0:ff68::/64

910
exp-panda
172.16.92.0/23
2001:200:0:ff91::/64
 */



static int
_ipv4_add_route(const u8 *prefix, int mask, const u8 *nexthop)
{
    return 0;
}

static int
_ipv4_check_route(const u8 *prefix, int mask, u8 *nexthop)
{
    return 0;
}



#define KTXBUF_AVAILABLE        0
#define KTXBUF_PENDING          1
#define KTXBUF_PENDING_ARP      2
#define KTXBUF_PENDING_ND       3
#define KTXBUF_CTS              0xfe /* Clear to send */

static int
_get_ktxbuf(struct l3if *l3if , struct ktxdesc **desc)
{
    int avl;

    /* Get available TX buffer */
    avl = l3if->txbuf.bufsz
        - ((l3if->txbuf.bufsz - l3if->txbuf.head + l3if->txbuf.tail)
           % l3if->txbuf.bufsz);
    if ( avl <= 0 ) {
        /* No buffer available */
        return -1;
    }

    /* Retrieve a buffer */
    *desc = &l3if->txbuf.buf[l3if->txbuf.tail];
    (*desc)->status = KTXBUF_PENDING;

    l3if->txbuf.tail = (l3if->txbuf.tail + 1) % l3if->txbuf.bufsz;

    return 0;
}

static int
_commit_ktxbuf(struct l3if *l3if)
{
    u32 i;
    int flag;
    int ret;

    flag = 0;

    i = l3if->txbuf.head;
    while ( i != (l3if->txbuf.tail % l3if->txbuf.bufsz) ) {
        if ( KTXBUF_CTS == l3if->txbuf.buf[i].status )  {
            ret = e1000_tx_set(l3if->netdev, l3if->txbuf.buf[i].address,
                               l3if->txbuf.buf[i].length,
                               l3if->txbuf.buf[i].vlan);
            if ( ret < 0 ) {
                /* Failed */
                flag = 1;
            } else {
                l3if->txbuf.buf[i].status = KTXBUF_AVAILABLE;
                if ( !flag ) {
                    l3if->txbuf.head = (l3if->txbuf.head + 1)
                        % l3if->txbuf.bufsz;
                }
            }
        } else if ( KTXBUF_AVAILABLE == l3if->txbuf.buf[i].status && !flag ) {
            l3if->txbuf.head = (l3if->txbuf.head + 1) % l3if->txbuf.bufsz;
        } else {
            flag = 1;
        }
        /* Next */
        i = (i + 1) % l3if->txbuf.bufsz;
    }

    return 0;
}

/*
 * Comput checksum
 */
static u16
_checksum(const u8 *buf, int len)
{
    int nleft;
    u32 sum;
    const u16 *cur;
    union {
        u16 us;
        u8 uc[2];
    } last;
    u16 ret;

    nleft = len;
    sum = 0;
    cur = (const u16 *)buf;

    while ( nleft > 1 ) {
        sum += *cur;
        cur += 1;
        nleft -= 2;
    }
    if ( 1 == nleft ) {
        last.uc[0] = *(const u8 *)cur;
        last.uc[1] = 0;
        sum += last.us;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    ret = ~sum;

    return ret;
}

/*
 * Comput ICMPv6 checksum
 */
static u16
_icmpv6_checksum(const u8 *src, const u8 *dst, u32 len, const u8 *data)
{
    u8 phdr[40];
    int nleft;
    u32 sum;
    const u16 *cur;
    union {
        u16 us;
        u8 uc[2];
    } last;
    u16 ret;
    int i;

    kmemcpy(phdr, src, 16);
    kmemcpy(phdr+16, dst, 16);
    phdr[32] = (len >> 24) & 0xff;
    phdr[33] = (len >> 16) & 0xff;
    phdr[34] = (len >> 8) & 0xff;
    phdr[35] = len & 0xff;
    phdr[36] = 0;
    phdr[37] = 0;
    phdr[38] = 0;
    phdr[39] = 58;

    sum = 0;
    for ( i = 0; i < 20; i++ ) {
        sum += *(const u16 *)(phdr + i * 2);
    }

    nleft = len;
    cur = (const u16 *)data;
    while ( nleft > 1 ) {
        sum += *cur;
        cur += 1;
        nleft -= 2;
    }
    if ( 1 == nleft ) {
        last.uc[0] = *(const u8 *)cur;
        last.uc[1] = 0;
        sum += last.us;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    ret = ~sum;

    return ret;
}

/*
 * Swap bytes
 */
static u16
_swapw(u16 w)
{
    return ((w & 0xff) << 8) | (w >> 8);
}




/*
 * Add IPv4 address
 */
static int
_add_ipv4_addr(const char *name, u8 a0, u8 a1, u8 a2, u8 a3, u8 mask)
{
    struct l3if_list *l3if_list;
    struct l3if *l3if;
    struct ipv4_addr_list *ipv4_addr_list;
    struct ipv4_addr *ipv4_addr;

    /* Search the interface */
    l3if = NULL;
    l3if_list = l3if_head;
    while ( NULL != l3if_list ) {
        if ( 0 == kstrcmp(l3if_list->l3if->name, name) ) {
            l3if = l3if_list->l3if;
            break;
        }
        l3if_list = l3if_list->next;
    }
    if ( NULL == l3if ) {
        /* Not found */
        return -1;
    }

    /* Allocate for IPv4 address */
    ipv4_addr_list = kmalloc(sizeof(struct ipv4_addr_list));
    if ( NULL == ipv4_addr_list ) {
        return -1;
    }
    ipv4_addr = kmalloc(sizeof(struct ipv4_addr));
    if ( NULL == ipv4_addr ) {
        kfree(ipv4_addr_list);
        return -1;
    }
    ipv4_addr->addr[0] = a0;
    ipv4_addr->addr[1] = a1;
    ipv4_addr->addr[2] = a2;
    ipv4_addr->addr[3] = a3;
    ipv4_addr->mask = mask;
    ipv4_addr->flags = 0;

    /* Prepend */
    ipv4_addr_list->addr = ipv4_addr;
    ipv4_addr_list->next = l3if->ip4list;
    l3if->ip4list = ipv4_addr_list;

    return 0;
}

/*
 * Check if the specified IPv4 address is assigned to the L3 interface
 */
static int
_check_ipv4(struct l3if *l3if, const u8 *addr)
{
    struct ipv4_addr_list *addr_list;

    addr_list = l3if->ip4list;
    while ( NULL != addr_list ) {
        if ( 0 == kmemcmp(addr_list->addr->addr, addr, 4) ) {
            return 0;
        }
        addr_list = addr_list->next;
    }

    return -1;
}

/*
 * Check if the specified IPv6 address is assigned to the L3 interface
 */
static int
_check_ipv6(struct l3if *l3if, const u8 *addr)
{
    struct ipv6_addr_list *addr_list;

    addr_list = l3if->ip6list;
    while ( NULL != addr_list ) {
        if ( 0 == kmemcmp(addr_list->addr->addr, addr, 16) ) {
            return 0;
        }
        addr_list = addr_list->next;
    }

    /* Multicast */
    if ( addr[0] == 0xff && addr[1] == 0x02 && addr[2] == 0x00
         && addr[3] == 0x00 && addr[4] == 0x00 && addr[5] == 0x00
         && addr[6] == 0x00 && addr[7] == 0x00 && addr[8] == 0x00
         && addr[9] == 0x00 && addr[10] == 0x00 && addr[11] == 0x00
         && addr[12] == 0x00 && addr[13] == 0x00 && addr[14] == 0x00
         && (addr[15] == 0x01 || addr[15] == 0x02) ) {
        /* All router / node multicast */
        return 0;
    } else if ( addr[0] == 0xff && addr[1] == 0x02 && addr[2] == 0x00
         && addr[3] == 0x00 && addr[4] == 0x00 && addr[5] == 0x00
         && addr[6] == 0x00 && addr[7] == 0x00 && addr[8] == 0x00
         && addr[9] == 0x00 && addr[10] == 0x00 && addr[11] == 0x01
         && addr[12] == 0xff ) {
        /* Solicited-Node Address */
        return 0;
    }

    return -1;
}


/*
 * Add IPv6 address
 */
static int
_add_ipv6_addr(const char *name, int scope, u16 a0, u16 a1, u16 a2, u16 a3,
               u16 a4, u16 a5, u16 a6, u16 a7, u8 preflen)
{
    struct l3if_list *l3if_list;
    struct l3if *l3if;
    struct ipv6_addr_list *ipv6_addr_list;
    struct ipv6_addr *ipv6_addr;

    /* Search the interface */
    l3if = NULL;
    l3if_list = l3if_head;
    while ( NULL != l3if_list ) {
        if ( 0 == kstrcmp(l3if_list->l3if->name, name) ) {
            l3if = l3if_list->l3if;
            break;
        }
        l3if_list = l3if_list->next;
    }
    if ( NULL == l3if ) {
        /* Not found */
        return -1;
    }

    /* Allocate for IPv6 address */
    ipv6_addr_list = kmalloc(sizeof(struct ipv6_addr_list));
    if ( NULL == ipv6_addr_list ) {
        return -1;
    }
    ipv6_addr = kmalloc(sizeof(struct ipv6_addr));
    if ( NULL == ipv6_addr ) {
        kfree(ipv6_addr_list);
        return -1;
    }
    ipv6_addr->addr[0] = (a0 >> 8);
    ipv6_addr->addr[1] = a0 & 0xff;
    ipv6_addr->addr[2] = (a1 >> 8);
    ipv6_addr->addr[3] = a1 & 0xff;
    ipv6_addr->addr[4] = (a2 >> 8);
    ipv6_addr->addr[5] = a2 & 0xff;
    ipv6_addr->addr[6] = (a3 >> 8);
    ipv6_addr->addr[7] = a3 & 0xff;
    ipv6_addr->addr[8] = (a4 >> 8);
    ipv6_addr->addr[9] = a4 & 0xff;
    ipv6_addr->addr[10] = (a5 >> 8);
    ipv6_addr->addr[11] = a5 & 0xff;
    ipv6_addr->addr[12] = (a6 >> 8);
    ipv6_addr->addr[13] = a6 & 0xff;
    ipv6_addr->addr[14] = (a7 >> 8);
    ipv6_addr->addr[15] = a7 & 0xff;
    ipv6_addr->preflen = preflen;
    ipv6_addr->flags = 0;
    ipv6_addr->scope = scope;

    /* Prepend */
    ipv6_addr_list->addr = ipv6_addr;
    ipv6_addr_list->next = l3if->ip6list;
    l3if->ip6list = ipv6_addr_list;

    return 0;
}

/*
 * Create an L3 interface
 */
static int
_create_l3interface(const char *name, struct netdev *netdev, int vlan)
{
    int i;
    struct l3if_list *l3if_list;
    struct l3if *l3if;

    /* Allocate for L3 interface instance */
    l3if_list = kmalloc(sizeof(struct l3if_list));
    if ( NULL == l3if_list ) {
        return -1;
    }
    l3if = kmalloc(sizeof(struct l3if));
    if ( NULL == l3if ) {
        kfree(l3if_list);
        return -1;
    }

    /* Copy the name */
    l3if->name = kstrdup(name);
    if ( NULL == l3if->name ) {
        kfree(l3if_list);
        kfree(l3if);
        return -1;
    }

    /* Allocate ARP table */
    l3if->arp.sz = ARP_TABLE_SIZE;
    l3if->arp.ent = kmalloc(sizeof(struct arp_entry) * l3if->arp.sz);
    if ( NULL == l3if->arp.ent ) {
        /* Error */
        panic("Could not allocate memory for ARP table.\r\n");
    }
    for ( i = 0; i < l3if->arp.sz; i++ ) {
        l3if->arp.ent[i].state = -1;
    }

    /* Allocate ND table */
    l3if->nd.sz = ND_TABLE_SIZE;
    l3if->nd.ent = kmalloc(sizeof(struct nd_entry) * l3if->nd.sz);
    if ( NULL == l3if->nd.ent ) {
        /* Error */
        panic("Could not allocate memory for ND table.\r\n");
    }
    for ( i = 0; i < l3if->nd.sz; i++ ) {
        l3if->nd.ent[i].state = -1;
    }

    /* TX buffer */
    l3if->txbuf.bufsz = KTXBUF_SIZE;
    l3if->txbuf.buf = kmalloc(sizeof(struct ktxdesc) * l3if->txbuf.bufsz);
    if ( NULL == l3if->txbuf.buf ) {
        /* Error */
        panic("Could not allocate memory for TX buffer.\r\n");
    }
    for ( i = 0; i < l3if->txbuf.bufsz; i++ ) {
        l3if->txbuf.buf[i].status = KTXBUF_AVAILABLE;
        l3if->txbuf.buf[i].address = (u64)kmalloc(8192);
        if ( NULL == l3if->txbuf.buf[i].address ) {
            /* Error */
            panic("Could not allocate memory for TX buffer.\r\n");
        }
    }
    l3if->txbuf.head = 0;
    l3if->txbuf.tail = 0;

    /* Set other parameters */
    l3if->netdev = netdev;
    l3if->vlan = vlan;
    l3if->ip4list = NULL;
    l3if->ip6list = NULL;

    /* Prepend */
    l3if_list->l3if = l3if;
    l3if_list->next = l3if_head;
    l3if_head = l3if_list;


    /* Add link-local address */
    _add_ipv6_addr(name, 0, 0xfe80, 0, 0, 0,
                   ((u16)netdev->macaddr[0] << 8) | (u16)netdev->macaddr[1]
                   | 0x0200,
                   ((u16)netdev->macaddr[2] << 8) | 0xff,
                   0xfe00 | netdev->macaddr[3],
                   ((u16)netdev->macaddr[4] << 8) | netdev->macaddr[5],
                   64);

    return 0;
}

/* Register an ARP entry */
static int
_register_arp(struct l3if *l3if, const u8 *ipaddr, const u8 *macaddr)
{
    int i;
    u64 nowms;
    u64 expire;

    nowms = arch_clock_get() / 1000 / 1000;
    expire = nowms + 300 * 1000;

    for ( i = 0; i < ARP_TABLE_SIZE; i++ ) {
        /* Check the existing entry */
        if ( l3if->arp.ent[i].state >= 0 ) {
            if ( 0 == kmemcmp(l3if->arp.ent[i].protoaddr, ipaddr, 4) ) {
                /* Found then update it */
                l3if->arp.ent[i].state = 1;
                l3if->arp.ent[i].expire = expire;
                return 0;
            }
        }
    }

    for ( i = 0; i < ARP_TABLE_SIZE; i++ ) {
        /* Search available entry */
        if ( -1 == l3if->arp.ent[i].state ) {
            kmemcpy(l3if->arp.ent[i].protoaddr, ipaddr, 4);
            kmemcpy(l3if->arp.ent[i].hwaddr, macaddr, 6);
            l3if->arp.ent[i].state = 1;
            l3if->arp.ent[i].expire = expire;

            arch_dbg_printf("Registered an ARP entry \r\n");
            return 0;
        }
    }

    return -1;
}

/*
 * Resolve MAC address from ARP table
 */
static int
_resolve_arp(struct l3if *l3if, const u8 *ipaddr, u8 *macaddr)
{
    int i;

    for ( i = 0; i < ARP_TABLE_SIZE; i++ ) {
        /* Check the existing entry */
        if ( l3if->arp.ent[i].state >= 0 ) {
            if ( 0 == kmemcmp(l3if->arp.ent[i].protoaddr, ipaddr, 4) ) {
                /* Found then insert it */
                kmemcpy(macaddr, l3if->arp.ent[i].hwaddr, 6);
                return 0;
            }
        }
    }

    return -1;
}

/* Register an ND entry */
static int
_register_nd(struct l3if *l3if, const u8 *ipaddr, const u8 *macaddr)
{
    int i;
    u64 nowms;
    u64 expire;

    nowms = arch_clock_get() / 1000 / 1000;
    expire = nowms + 300 * 1000;

    for ( i = 0; i < ND_TABLE_SIZE; i++ ) {
        /* Check the existing entry */
        if ( l3if->nd.ent[i].state >= 0 ) {
            if ( 0 == kmemcmp(l3if->nd.ent[i].neighbor, ipaddr, 16) ) {
                /* Found then update it */
                l3if->nd.ent[i].state = 1;
                l3if->nd.ent[i].expire = expire;
                return 0;
            }
        }
    }

    for ( i = 0; i < ND_TABLE_SIZE; i++ ) {
        /* Search available entry */
        if ( -1 == l3if->nd.ent[i].state ) {
            kmemcpy(l3if->nd.ent[i].neighbor, ipaddr, 16);
            kmemcpy(l3if->nd.ent[i].linklayeraddr, macaddr, 6);
            l3if->nd.ent[i].state = 1;
            l3if->nd.ent[i].expire = expire;
            l3if->nd.ent[i].netif = l3if;

            arch_dbg_printf("Registered an ND entry \r\n");
            return 0;
        }
    }

    return -1;
}

/*
 * Resolve MAC address from ND table
 */
static int
_resolve_nd(struct l3if *l3if, const u8 *ipaddr, u8 *macaddr)
{
    int i;

    for ( i = 0; i < ND_TABLE_SIZE; i++ ) {
        /* Check the existing entry */
        if ( l3if->nd.ent[i].state >= 0 ) {
            if ( 0 == kmemcmp(l3if->nd.ent[i].neighbor, ipaddr, 16) ) {
                /* Found then insert it */
                kmemcpy(macaddr, l3if->nd.ent[i].linklayeraddr, 6);
                return 0;
            }
        }
    }

    return -1;
}

/*
 * ARP
 */
static int
_rx_arp(struct l3if *l3if, const u8 *pkt, u32 len, int vlan)
{
    /* ARP */
    if ( len < 14 + 28 ) {
        return -1;
    }
    /* Valid ARP */
    if ( 0x00 == pkt[14] && 0x01 == pkt[15]
         && 0x08 == pkt[16] && 0x00 == pkt[17]
         && 0x06 == pkt[18] && 0x04 == pkt[19] ) {
        /* HW = 0001, PROTO = 0800, HLEN = 06, PLEN = 04 */

        /* 1 = Req, 2 = Res */
        u16 mode = (((u16)pkt[20]) << 8) | pkt[21];

        if ( 1 == mode ) {
            /* Check the destination address */
            if ( _check_ipv4(l3if, pkt+38) < 0 ) {
                return -1;
            }
            /* ARP request */
            _register_arp(l3if, pkt+28, pkt+22);

            struct ktxdesc *txdesc;
            u8 *txpkt;
            u16 *txlen;
            int ret;
            //e1000_tx_buf(l3if->netdev, &txpkt, &txlen, vlan);
            ret = _get_ktxbuf(l3if, &txdesc);
            txdesc->status = KTXBUF_CTS;
            txdesc->vlan = vlan;
            txpkt = (u8 *)txdesc->address;
            txlen = &txdesc->length;

            kmemcpy(txpkt, pkt+22, 6);
            kmemcpy(txpkt+6, l3if->netdev->macaddr, 6);
            txpkt[12] = 0x08;
            txpkt[13] = 0x06;
            txpkt[14] = 0x00;
            txpkt[15] = 0x01;
            txpkt[16] = 0x08;
            txpkt[17] = 0x00;
            txpkt[18] = 0x06;
            txpkt[19] = 0x04;
            txpkt[20] = 0x00;
            txpkt[21] = 0x02;
            kmemcpy(txpkt+22, l3if->netdev->macaddr, 6);
            kmemcpy(txpkt+28, pkt+38, 4);
            kmemcpy(txpkt+32, pkt+22, 6);
            kmemcpy(txpkt+38, pkt+28, 4);
            /**txlen = 42;*/
            *txlen = 60;

            _commit_ktxbuf(l3if);
            e1000_tx_commit(l3if->netdev);
        } else if ( 2 == mode ) {
            /* ARP reply, then check the destination */
            if ( !kmemcmp(pkt+32, l3if->netdev->macaddr, 6) ) {
                return -1;
            }
            /* Check the IP address */
            if ( _check_ipv4(l3if, pkt+38) < 0 ) {
                return -1;
            }
            /* Register */
            _register_arp(l3if, pkt+28, pkt+22);
        }
    }

    return 0;
}


/*
 * IPv4 to self
 */
static int
_rx_ipv4_to_self(struct l3if *l3if, const u8 *pkt, u32 len, int vlan)
{
    struct iphdr *ip = (struct iphdr *)(pkt + 14);
    u16 ip_hdrlen = (ip->ip_vhl & 0xf) << 2;
    u16 p_len = _swapw(ip->ip_len);
    u16 chksum;

    if ( p_len < ip_hdrlen ) {
        return -1;
    }
    p_len -= ip_hdrlen;

    if ( 1 == ip->ip_proto ) {
        /* ICMP */
        struct icmp_hdr *icmp = (struct icmp_hdr *)(pkt+14+ip_hdrlen);
        if ( 8 == icmp->type ) {
            /* ICMP echo request */




            struct ktxdesc *txdesc;
            u8 *txpkt;
            int ret;
            ret = _get_ktxbuf(l3if, &txdesc);
            if ( ret < 0 ) {
                /* Buffer full */
                return -1;
            }
            txdesc->status = KTXBUF_CTS;
            txdesc->vlan = vlan;
            txpkt = (u8 *)txdesc->address;



            ret = _resolve_arp(l3if, ip->ip_src, txpkt);
            if ( ret < 0 ) {
                /* ARP request */
                txpkt[0] = 0xff;
                txpkt[1] = 0xff;
                txpkt[2] = 0xff;
                txpkt[3] = 0xff;
                txpkt[4] = 0xff;
                txpkt[5] = 0xff;
                kmemcpy(txpkt+6, l3if->netdev->macaddr, 6);
                txpkt[12] = 0x08;
                txpkt[13] = 0x06;
                txpkt[14] = 0x00;
                txpkt[15] = 0x01;
                txpkt[16] = 0x08;
                txpkt[17] = 0x00;
                txpkt[18] = 0x06;
                txpkt[19] = 0x04;
                txpkt[20] = 0x00;
                txpkt[21] = 0x01;
                kmemcpy(txpkt+22, l3if->netdev->macaddr, 6);
                kmemcpy(txpkt+28, ip->ip_dst, 4);
                txpkt[32] = 0;
                txpkt[33] = 0;
                txpkt[34] = 0;
                txpkt[35] = 0;
                txpkt[36] = 0;
                txpkt[37] = 0;
                kmemcpy(txpkt+38, ip->ip_src, 4);
                txdesc->length = 42;
                txdesc->length = 60;
            } else {
                kmemcpy(txpkt+6, l3if->netdev->macaddr, 6);
                txpkt[12] = 0x08;
                txpkt[13] = 0x00;
                kmemcpy(txpkt+14, pkt+14, ip_hdrlen + p_len);
                txpkt[22] = 64;
                txpkt[24] = 0;
                txpkt[25] = 0;

                chksum = _checksum(txpkt + 14, ip_hdrlen);
                txpkt[24] = chksum & 0xff;
                txpkt[25] = chksum >> 8;
                txpkt[26] = pkt[30];
                txpkt[27] = pkt[31];
                txpkt[28] = pkt[32];
                txpkt[29] = pkt[33];
                txpkt[30] = pkt[26];
                txpkt[31] = pkt[27];
                txpkt[32] = pkt[28];
                txpkt[33] = pkt[29];
                txpkt[14 + ip_hdrlen] = 0;
                txpkt[14 + ip_hdrlen + 2] = 0;
                txpkt[14 + ip_hdrlen + 3] = 0;
                chksum = _checksum(txpkt + 14 + ip_hdrlen, p_len);
                txpkt[14 + ip_hdrlen + 2] = chksum & 0xff;
                txpkt[14 + ip_hdrlen + 3] = chksum >> 8;

                txdesc->length = 14 + ip_hdrlen + p_len;
#if 0
                arch_dbg_printf("Send an ICMP packet. %x\r\n", ip->ip_vhl);
#endif
            }
        }
    }
    _commit_ktxbuf(l3if);

    return 0;
}

/*
 * Execute routing
 */
static int
_rx_ip_routing(struct l3if *l3if, const u8 *pkt, u32 len, int vlan)
{
#if 0
    const u8 *dstmac = pkt;
    const u8 *srcmac = pkt + 6;
#endif

    struct iphdr *ip = (struct iphdr *)(pkt + 14);
    u16 ip_hdrlen = (ip->ip_vhl & 0xf) << 2;
    u16 p_len = _swapw(ip->ip_len);
    if ( p_len < ip_hdrlen ) {
        return -1;
    }
    p_len -= ip_hdrlen;

    /* Do routing!!! */
    arch_dbg_printf("Routing an IP packet. %x\r\n", ip->ip_vhl);
    int ttl = ip->ip_ttl;
    ttl--;
    if ( ip->ip_ttl < 1 ) {
        /* ICMP time exceeded */
    } else {
        /* Do routing */
    }

    return 0;
}


/*
 * IPv6 to self
 */
static int
_rx_ipv6_to_self(struct l3if *l3if, const u8 *pkt, u32 len, int vlan)
{
    /* IPv6 */
    struct ip6hdr *ip6 = (struct ip6hdr *)(pkt + 14);
    u16 ip6_len = _swapw(ip6->ip6_len);

    if ( ip6->ip6_next == 0x3a ) {
        /* ICMPv6 */
        struct icmp6_hdr *icmp6 = (struct icmp6_hdr *)(pkt + 14 + 40);
        u16 chksum = _icmpv6_checksum(ip6->ip6_src, ip6->ip6_dst, ip6_len,
                                      pkt + 14 + 40);
        if ( chksum != 0 && chksum != 0xffff ) {
            return -1;
        }
        if ( icmp6->type == 128 && icmp6->code == 0 ) {
            /* Echo request */
            struct ktxdesc *txdesc;
            u8 *txpkt;
            u16 *txlen;
            int ret;
            //e1000_tx_buf(l3if->netdev, &txpkt, &txlen, vlan);
            ret = _get_ktxbuf(l3if, &txdesc);
            txdesc->status = KTXBUF_CTS;
            txdesc->vlan = vlan;
            txpkt = (u8 *)txdesc->address;
            txlen = &txdesc->length;

            ret = _resolve_nd(l3if, ip6->ip6_src, txpkt);
            if ( ret < 0 ) {
                /* Neighbor solicitation */
                txpkt[0] = 0x33;
                txpkt[1] = 0x33;
                txpkt[2] = 0xff;
                txpkt[3] = ip6->ip6_src[13];
                txpkt[4] = ip6->ip6_src[14];
                txpkt[5] = ip6->ip6_src[15];
                kmemcpy(txpkt+6, l3if->netdev->macaddr, 6);
                txpkt[12] = 0x86;
                txpkt[13] = 0xdd;
                /* IPv6 header */
                txpkt[14] = 0x60;
                txpkt[15] = 0x00;
                txpkt[16] = 0x00;
                txpkt[17] = 0x00;

                txpkt[18] = 0x00;
                txpkt[19] = 32;
                txpkt[20] = 58;
                txpkt[21] = 255;

                /* FIXME */
                kmemcpy(txpkt+22, l3if->ip6list->addr->addr, 16);
                txpkt[38] = 0xff;
                txpkt[39] = 0x02;
                txpkt[40] = 0x00;
                txpkt[41] = 0x00;
                txpkt[42] = 0x00;
                txpkt[43] = 0x00;
                txpkt[44] = 0x00;
                txpkt[45] = 0x00;
                txpkt[46] = 0x00;
                txpkt[47] = 0x00;
                txpkt[48] = 0x00;
                txpkt[49] = 0x01;
                txpkt[50] = 0xff;
                txpkt[51] = ip6->ip6_src[13];
                txpkt[52] = ip6->ip6_src[14];
                txpkt[53] = ip6->ip6_src[15];
                /* ICMPv6 */
                txpkt[54] = 135;
                txpkt[55] = 0;
                txpkt[56] = 0;
                txpkt[57] = 0;
                txpkt[58] = 0;
                txpkt[59] = 0;
                txpkt[60] = 0;
                txpkt[61] = 0;

                kmemcpy(txpkt+62, ip6->ip6_src, 16);
                txpkt[78] = 1;
                txpkt[79] = 1;
                kmemcpy(txpkt+80, l3if->netdev->macaddr, 6);
                /* Checksum */
                chksum = _icmpv6_checksum(txpkt+22, txpkt+38, 32,
                                          txpkt+14+40);
                txpkt[56] = chksum & 0xff;
                txpkt[57] = chksum >> 8;
                *txlen = 86;
                _commit_ktxbuf(l3if);
            } else {
                kmemcpy(txpkt+6, l3if->netdev->macaddr, 6);
                txpkt[12] = 0x86;
                txpkt[13] = 0xdd;
                kmemcpy(txpkt+14, pkt+14, 40 + ip6_len);
                kmemcpy(txpkt+14+8, ip6->ip6_dst, 16);
                kmemcpy(txpkt+14+8+16, ip6->ip6_src, 16);
                txpkt[21] = 64;
                /* ICMPv6 */
                txpkt[54] = 129;
                txpkt[55] = 0;
                txpkt[56] = 0;
                txpkt[57] = 0;

                chksum = _icmpv6_checksum(ip6->ip6_dst, ip6->ip6_src,
                                          ip6_len, txpkt+54);
                txpkt[56] = chksum & 0xff;
                txpkt[57] = chksum >> 8;

                *txlen = 14 + 40 + ip6_len;
                _commit_ktxbuf(l3if);
            }
        } else if ( icmp6->type == 133 ) {
            /* Router solicitation */
        } else if ( icmp6->type == 135 && icmp6->code == 0 ) {
            if ( ip6_len == 8 + 16 ) {
                /* DAD */
            }
            if ( ip6_len < 8 + 16 + 8 ) {
                return -1;
            }

            /* Neighbor solicitation */
            const u8 *target = pkt + 14 + 40 + 8;

            /* Type(1), length(1) in 8-octet from type, value */
            const u8 *option = pkt + 14 + 40 + 8 + 16;
            if ( option[0] != 1 || option[1] != 1 ) {
                return -1;
            }

            if ( _check_ipv6(l3if, target) < 0 ) {
                /* This host is not the target */
                arch_dbg_printf("Not target \r\n");
                return -1;
            }

            _register_nd(l3if, ip6->ip6_src, option + 2);

            struct ktxdesc *txdesc;
            u8 *txpkt;
            u16 *txlen;
            int ret;

            ret = _get_ktxbuf(l3if, &txdesc);
            txdesc->status = KTXBUF_CTS;
            txdesc->vlan = vlan;
            txpkt = (u8 *)txdesc->address;
            txlen = &txdesc->length;

            kmemcpy(txpkt, option + 2, 6);
            kmemcpy(txpkt+6, l3if->netdev->macaddr, 6);
            txpkt[12] = 0x86;
            txpkt[13] = 0xdd;
            /* IPv6 header */
            txpkt[14] = 0x60;
            txpkt[15] = 0x00;
            txpkt[16] = 0x00;
            txpkt[17] = 0x00;

            txpkt[18] = 0x00;
            txpkt[19] = 32;
            txpkt[20] = 58;
            txpkt[21] = 255;

            /* FIXME */
            kmemcpy(txpkt+22, target, 16);
            kmemcpy(txpkt+38, pkt+22, 16);
            /* ICMPv6 */
            txpkt[54] = 136;
            txpkt[55] = 0;
            txpkt[56] = 0;
            txpkt[57] = 0;
            txpkt[58] = (1<<7) | (1<<6) | (1<<5);
            txpkt[59] = 0;
            txpkt[60] = 0;
            txpkt[61] = 0;

            kmemcpy(txpkt+62, target, 16);
            txpkt[78] = 2;
            txpkt[79] = 1;
            kmemcpy(txpkt+80, l3if->netdev->macaddr, 6);
            /* Checksum */
            chksum = _icmpv6_checksum(txpkt+22, txpkt+38, 32, txpkt+14+40);
            txpkt[56] = chksum & 0xff;
            txpkt[57] = chksum >> 8;
            *txlen = 86;

            _commit_ktxbuf(l3if);
            e1000_tx_commit(l3if->netdev);

        } else if ( icmp6->type == 136 ) {
            /* Neighbor advertisement */
        } else {
            arch_dbg_printf("IPv6 ICMP packet %d\r\n", icmp6->type);
        }

    }

    return 0;
}

static int
_rx_ipv6_routing(struct l3if *l3if, const u8 *pkt, u32 len, int vlan)
{



    return 0;
}



/*
 * RX callback
 */
static int
_rx_cb(const u8 *pkt, u32 len, int vlan)
{
    struct l3if_list *l3if_list;
    struct l3if *l3if;

#if 0
    arch_dbg_printf("YYY VLAN=%d len=%d\r\n", vlan, len);
    arch_dbg_printf(" %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x"
                    " %02x %02x %02x %02x\r\n",
                    pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5], pkt[6],
                    pkt[7], pkt[8], pkt[9], pkt[10], pkt[11], pkt[12], pkt[13]);
#endif

    /* Search vlan interface */
    l3if_list = l3if_head;
    l3if = NULL;
    while ( NULL != l3if_list ) {
        if ( vlan == l3if_list->l3if->vlan ) {
            l3if = l3if_list->l3if;
            break;
        }
        l3if_list = l3if_list->next;
    }
    if ( NULL == l3if ) {
        /* The corresponding VLAN interface was not found */
        return -1;
    }

    /* Check the destination */
    if ( !(0xff == pkt[0] && 0xff == pkt[1] && 0xff == pkt[2]
           && 0xff == pkt[3] && 0xff == pkt[4] && 0xff == pkt[5])
         && !(0x33 == pkt[0] && 0x33 == pkt[1])
         && 0 != kmemcmp(pkt, l3if->netdev->macaddr, 6) ) {
        /* Neither broadcast, IPv6 multicast nor unicast to this interface */
        return -1;
    }

    /* Check the ether type */
    if ( 0x08 == pkt[12] && 0x06 == pkt[13] ) {
        return _rx_arp(l3if, pkt, len, vlan);
    } else if ( 0x08 == pkt[12] && 0x00 == pkt[13] ) {
        /* IPv4 then check the destination */
        struct iphdr *ip = (struct iphdr *)(pkt + 14);
        u16 ip_hdrlen = (ip->ip_vhl & 0xf) << 2;
        u16 chksum = _checksum(pkt + 14, ip_hdrlen);

        /* Check the version */
        if ( (ip->ip_vhl >> 4) != 4 ) {
            return -1;
        }

        /* Verify the checksum */
        if ( 0 != chksum && 0xffff != chksum ) {
            /* Invalid checksum */
            return -1;
        }

        /* Check the IP address */
        if ( 0 == _check_ipv4(l3if, ip->ip_dst) ) {
            /* Destination is self */
            return _rx_ipv4_to_self(l3if, pkt, len, vlan);
        } else {
            return _rx_ip_routing(l3if, pkt, len, vlan);
        }
    } else if ( 0x86 == pkt[12] && 0xdd == pkt[13] ) {
        /* IPv6 */
        struct ip6hdr *ip6 = (struct ip6hdr *)(pkt + 14);

        /* Check the version */
        if ( ((ip6->ip6_vtf >> 4) & 0xf) != 6 ) {
            arch_dbg_printf("Invalid IPv6 packet: %x\r\n",
                            ip6->ip6_vtf);
            return -1;
        }

        if ( 0 == _check_ipv6(l3if, ip6->ip6_dst) ) {
            return _rx_ipv6_to_self(l3if, pkt, len, vlan);
        } else {
            return _rx_ipv6_routing(l3if, pkt, len, vlan);
        }
    }

    return 0;
}

/*
 * Router processess
 */
void
proc_router(void)
{
    struct router *rt;
    int i;

    /* Initialize the lock variable */
    lock = 0;

    /* Initialize interfaces */
    l3if_head = NULL;

    /* Print a starter message */
    arch_dbg_printf("Start router\r\n");

    /* Allocate a router instance */
    rt = kmalloc(sizeof(struct router));

    /* Allocate NAT66 table */
    rt->nat66.sz = ND_TABLE_SIZE;
    rt->nat66.ent = kmalloc(sizeof(struct nd_entry) * rt->nat66.sz);
    if ( NULL == rt->nat66.ent ) {
        /* Error */
        panic("Could not allocate memory for NAT66 table.\r\n");
    }
    for ( i = 0; i < rt->nat66.sz; i++ ) {
        rt->nat66.ent[i].state = -1;
    }

    /* Initialize network interface */
    struct netdev_list *list;
    int ret;
    list = netdev_head;
    ret = _create_l3interface("ve680", list->netdev, 680);
    if ( ret < 0 ) {
        panic("Could not create an interface.\r\n");
    }
    ret = _add_ipv4_addr("ve680", 203, 178, 158, 194, 26);
    if ( ret < 0 ) {
        panic("Could not assign an IPv4 address.\r\n");
    }
    ret = _add_ipv6_addr("ve680", 1, 0x2001, 0x200, 0, 0xff68, 0, 0, 0, 2, 64);
    if ( ret < 0 ) {
        panic("Could not assign an IPv6 address.\r\n");
    }
    ret = _create_l3interface("ve910", list->netdev, 910);
    if ( ret < 0 ) {
        panic("Could not create an interface.\r\n");
    }
    ret = _add_ipv4_addr("ve910", 172, 16, 92, 1, 23);
    if ( ret < 0 ) {
        panic("Could not assign an IPv4 address.\r\n");
    }
    ret = _add_ipv6_addr("ve910", 1, 0x2001, 0x200, 0, 0xff91, 0, 0, 0, 1, 64);
    if ( ret < 0 ) {
        panic("Could not assign an IPv6 address.\r\n");
    }

    /* For testing */
    ret = _create_l3interface("ve0", list->netdev, 0);
    if ( ret < 0 ) {
        panic("Could not create an interface.\r\n");
    }
    ret = _add_ipv4_addr("ve0", 192, 168, 56, 3, 24);
    //ret = _add_ipv4_addr("ve0", 10, 211, 55, 3, 24);
    if ( ret < 0 ) {
        panic("Could not assign an IPv4 address.\r\n");
    }
    ret = _add_ipv6_addr("ve0", 1, 0x2001, 0xdb8, 0x0, 0x1, 0, 0, 0, 3, 64);
    if ( ret < 0 ) {
        panic("Could not assign an IPv6 address.\r\n");
    }

    e1000_routing(list->netdev, _rx_cb);

    /* Free the router instance */
    kfree(rt->nat66.ent);
    kfree(rt);
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */