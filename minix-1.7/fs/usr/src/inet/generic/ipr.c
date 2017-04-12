/*
ipr.c
*/

#include "inet.h"
#include "clock.h"

#include "assert.h"
#include "io.h"
#include "ipr.h"

INIT_PANIC();

#define ROUTE_NR	32
#define DIST_UNREACHABLE	512

PRIVATE route_t route_table[ROUTE_NR];
PRIVATE int fixed_routes;

FORWARD route_t *get_route_ent ARGS(( ipaddr_t dest ));

PUBLIC void ipr_init()
{
	int i;
	route_t *route_ind;

	for (i= 0, route_ind= route_table; i<ROUTE_NR; i++, route_ind++)
		route_ind->rt_flags= RTF_EMPTY;
	fixed_routes= 0;
}

PUBLIC int iproute_frag(dest, ttl, nexthop, port)
ipaddr_t dest;
int ttl;
ipaddr_t *nexthop;
int *port;
{
	route_t *route_ent;

	route_ent= get_route_ent(dest);
	if (!route_ent || route_ent->rt_dist > ttl)
		return EDSTNOTRCH;

	*nexthop= route_ent->rt_gateway;
	*port= route_ent->rt_port;
	return 1;
}

PRIVATE route_t *get_route_ent(dest)
ipaddr_t dest;
{
	route_t *route_ind, *route_hi;
	route_t *bestroute, *sec_bestroute, tmproute;
	time_t currtim;

	currtim= get_time();

	route_hi= &route_table[ROUTE_NR];
	bestroute= 0;
	for (route_ind= route_table; route_ind<route_hi; route_ind++)
	{
		if (!(route_ind->rt_flags & RTF_INUSE))
			continue;
		if (route_ind->rt_exp_tim && route_ind->rt_exp_tim<currtim)
		{
			route_ind->rt_flags &= ~RTF_INUSE;
			continue;
		}
		if ((dest ^ route_ind->rt_dest) & route_ind->rt_netmask)
			continue;

		if (!bestroute)
		{
			bestroute= route_ind;
			continue;
		}
		if (bestroute->rt_netmask != route_ind->rt_netmask)
		{
			if (route_ind->rt_dist > bestroute->rt_dist ||
				(route_ind->rt_dist == bestroute->
				rt_dist && ntohl(route_ind->rt_netmask)>
				ntohl(bestroute->rt_netmask)))
				bestroute= route_ind;
			continue;
		}
		sec_bestroute= bestroute;
		if (bestroute->rt_gateway == route_ind->rt_gateway)
		{
			if (route_ind->rt_timestamp > 
				bestroute->rt_timestamp)
				bestroute= route_ind;
		}
		else
		{
			if (route_ind->rt_dist < bestroute->rt_dist)
				bestroute= route_ind;
		}
		if (bestroute->rt_dist > sec_bestroute->rt_dist)
		{
			tmproute= *bestroute;
			*bestroute= *sec_bestroute;
			*sec_bestroute= tmproute;
			route_ind= bestroute= sec_bestroute;
		}
	}
#if DEBUG & 256
 { where(); if (!bestroute){ printf("no route to "); writeIpAddr(dest);
	printf("\n"); } else { printf ("route to ");
	writeIpAddr(bestroute->rt_dest); printf(" via ");
	writeIpAddr(bestroute->rt_gateway); printf(" at distance %d\n",
	bestroute->rt_dist); } }
#endif
	return bestroute;
}

PUBLIC route_t *ipr_add_route(dest, netmask, gateway, port, timeout, dist,
	fixed, preference)
ipaddr_t dest;
ipaddr_t netmask;
ipaddr_t gateway;
int port;
time_t timeout;
int dist;
int fixed;
i32_t preference;
{
	int i;
	route_t *route_ind;
	route_t *oldest_route;
	time_t currtim;

#if DEBUG & 256
 { where(); printf("ipr_add_route(dest= "); writeIpAddr(dest);
	printf(", netmask= "); writeIpAddr(netmask);
	printf(", gateway= "); writeIpAddr(gateway);
	printf(", port= %d, timeout= %ld, dist= %d, fixed= %d, pref= %d\n", 
		port, timeout, dist, fixed, preference); }
#endif
	if (fixed)
	{
		if (fixed_routes >= IPR_MAX_FIXED_ROUTES)
			return 0;
		fixed_routes++;
	}
	oldest_route= 0;
	currtim= get_time();
	for (i= 0, route_ind= route_table; i<ROUTE_NR; i++, route_ind++)
	{
		if (!(route_ind->rt_flags & RTF_INUSE))
		{
			oldest_route= route_ind;
			break;
		}
		if (route_ind->rt_exp_tim && route_ind->rt_exp_tim < currtim)
		{
			oldest_route= route_ind;
			break;
		}
		if (route_ind->rt_dest == dest &&
			route_ind->rt_netmask == netmask &&
			route_ind->rt_gateway == gateway)
		{
			if (!fixed && (route_ind->rt_flags & RTF_FIXED))
				continue;
			oldest_route= route_ind;
			break;
		}
		if (route_ind->rt_flags & RTF_FIXED)
			continue;
		if (!oldest_route)
		{
			oldest_route= route_ind;
			continue;
		}
		if (route_ind->rt_timestamp < oldest_route->rt_timestamp)
			oldest_route= route_ind;
	}
assert (oldest_route);
	oldest_route->rt_dest= dest;
	oldest_route->rt_gateway= gateway;
	oldest_route->rt_netmask= netmask;
	if (timeout)
		oldest_route->rt_exp_tim= currtim + timeout;
	else
		oldest_route->rt_exp_tim= 0;
	oldest_route->rt_timestamp= currtim;
	oldest_route->rt_dist= dist;
	oldest_route->rt_port= port;
	oldest_route->rt_flags= RTF_INUSE;
	oldest_route->rt_pref= preference;
	if (fixed)
		oldest_route->rt_flags |= RTF_FIXED;
	return oldest_route;
}

PUBLIC void ipr_gateway_down(gateway, timeout)
ipaddr_t gateway;
time_t timeout;
{
	route_t *route_ind, *route;
	time_t currtim;
	int i;

	currtim= get_time();
	for (i= 0, route_ind= route_table; i<ROUTE_NR; i++, route_ind++)
	{
		if (!(route_ind->rt_flags & RTF_INUSE))
			continue;
		if (route_ind->rt_gateway != gateway)
			continue;
		if (route_ind->rt_exp_tim && route_ind->rt_exp_tim < currtim)
		{
			route_ind->rt_flags &= ~RTF_INUSE;
			continue;
		}
		if (!(route_ind->rt_flags & RTF_FIXED))
		{
			route_ind->rt_timestamp= currtim;
			if (timeout)
				route_ind->rt_exp_tim= currtim+timeout;
			else
				route_ind->rt_exp_tim= 0;
			route_ind->rt_dist= DIST_UNREACHABLE;
			continue;
		}
#if DEBUG
 { where(); printf("adding route\n"); }
#endif
		route= ipr_add_route(route_ind->rt_dest, route_ind->rt_netmask,
			gateway, route_ind->rt_port, timeout, DIST_UNREACHABLE,
			FALSE, 0);
assert (route);
	}
}

PUBLIC void ipr_destunrch(dest, netmask, timeout)
ipaddr_t dest;
ipaddr_t netmask;
time_t timeout;
{
	route_t *route;

	route= get_route_ent(dest);

	if (!route)
	{
#if DEBUG
 { where(); printf("got a dest unreachable for "); writeIpAddr(dest);
	printf("but no route present\n"); }
#endif
		return;
	}
#if DEBUG
 { where(); printf("adding route\n"); }
#endif
	route= ipr_add_route(dest, netmask, route->rt_gateway, route->rt_port,
		timeout, DIST_UNREACHABLE, FALSE, 0);
assert (route);
}

PUBLIC void ipr_redirect(dest, netmask, old_gateway, new_gateway, new_port,
	timeout)
ipaddr_t dest;
ipaddr_t netmask;
ipaddr_t old_gateway;
ipaddr_t new_gateway;
int new_port;
time_t timeout;
{
	route_t *route;

	route= get_route_ent(dest);

	if (!route)
	{
#if DEBUG
 { where(); printf("got a redirect for "); writeIpAddr(dest);
	printf("but no route present\n"); }
#endif
		return;
	}
	if (route->rt_gateway != old_gateway)
	{
#if DEBUG
 { where(); printf("got a redirect from "); writeIpAddr(old_gateway);
	printf(" for "); writeIpAddr(dest); printf(" but curr gateway is ");
	writeIpAddr(route->rt_gateway); printf("\n"); }
#endif
		return;
	}
	if (route->rt_flags & RTF_FIXED)
	{
		if ( route->rt_dest == dest)
		{
#if DEBUG
 { where(); printf("got a redirect for "); writeIpAddr(dest);
	printf("but route is fixed\n"); }
#endif
			return;
		}
	}
	else
	{
#if DEBUG
 { where(); printf("adding route\n"); }
#endif
		route= ipr_add_route(dest, netmask, route->rt_gateway,
			route->rt_port, timeout, DIST_UNREACHABLE, FALSE, 0);
assert(route);
	}
#if DEBUG
 { where(); printf("adding route\n"); }
#endif
	route= ipr_add_route(dest, netmask, new_gateway, new_port,
		timeout, 1, FALSE, 0);
assert (route);
}

PUBLIC void ipr_ttl_exc(dest, netmask, timeout)
ipaddr_t dest;
ipaddr_t netmask;
time_t timeout;
{
	route_t *route;
	int new_dist;

	route= get_route_ent(dest);

	if (!route)
	{
#if DEBUG
 { where(); printf("got a ttl exceeded for "); writeIpAddr(dest);
	printf("but no route present\n"); }
#endif
		return;
	}

	new_dist= route->rt_dist * 2;
	if (new_dist>IP_MAX_TTL)
	{
		new_dist= route->rt_dist+1;
		if (new_dist>IP_MAX_TTL)
		{
#if DEBUG
 { where(); printf("got a ttl exceeded for "); writeIpAddr(dest);
	printf(" but dist is %d\n", route->rt_dist); }
#endif
			return;
		}
	}

#if DEBUG
 { where(); printf("adding route\n"); }
#endif
	route= ipr_add_route(dest, netmask, route->rt_gateway, route->rt_port,
		timeout, new_dist, FALSE, 0);
assert (route);
}

int ipr_get_route(ent_no, route_ent)
int ent_no;
nwio_route_t *route_ent;
{
	route_t *route;

	if (ent_no<0 || ent_no>= ROUTE_NR)
		return ENOENT;

	route= &route_table[ent_no];
	if (route->rt_exp_tim && route->rt_exp_tim < get_time())
		route->rt_flags &= ~RTF_INUSE;

	route_ent->nwr_ent_count= ROUTE_NR;
	route_ent->nwr_dest= route->rt_dest;
	route_ent->nwr_netmask= route->rt_netmask;
	route_ent->nwr_gateway= route->rt_gateway;
	route_ent->nwr_dist= route->rt_dist;
	route_ent->nwr_flags= NWRF_EMPTY;
	if (route->rt_flags & RTF_INUSE)
	{
		route_ent->nwr_flags |= NWRF_INUSE;
		if (route->rt_flags & RTF_FIXED)
			route_ent->nwr_flags |= NWRF_FIXED;
	}
	route_ent->nwr_pref= route->rt_pref;
	return 0;
}
