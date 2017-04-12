/*
server/ip/gen/route.h
*/

#ifndef __SERVER__IP__GEN__ROUTE_H__
#define __SERVER__IP__GEN__ROUTE_H__

typedef struct nwio_route
{
	u32_t nwr_ent_no;
	u32_t nwr_ent_count;
	ipaddr_t nwr_dest;
	ipaddr_t nwr_netmask;
	ipaddr_t nwr_gateway;
	u32_t nwr_dist;
	u32_t nwr_flags;
	u32_t nwr_pref;
} nwio_route_t;

#define NWRF_EMPTY	0
#define NWRF_INUSE	1
#define NWRF_FIXED	2

#endif /* __SERVER__IP__GEN__ROUTE_H__ */
