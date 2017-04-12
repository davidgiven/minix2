/*
ipr.h
*/

#ifndef IPR_H
#define IPR_H

typedef struct route
{
	ipaddr_t rt_dest;
	ipaddr_t rt_gateway;
	ipaddr_t rt_netmask;
	time_t rt_exp_tim;
	time_t rt_timestamp;
	int rt_dist;
	int rt_port;
	int rt_flags;
	i32_t rt_pref;
} route_t;

#define RTF_EMPTY	0
#define RTF_INUSE	1
#define RTF_FIXED	2

#define IPR_MAX_FIXED_ROUTES	16

#define IPR_UNRCH_TIMEOUT	(60L * HZ)
#define IPR_TTL_TIMEOUT		(60L * HZ)
#define IPR_REDIRECT_TIMEOUT	(20 * 60L * HZ)
#define IPR_GW_DOWN_TIMEOUT	(60L * HZ)

/* Prototypes */

int iproute_frag ARGS(( ipaddr_t dest, int ttl, ipaddr_t *nexthop,
	int *port ));
void ipr_init ARGS(( void ));
route_t *ipr_add_route ARGS(( ipaddr_t dest, ipaddr_t netmask, 
	ipaddr_t gateway, int port, time_t timeout, int dist, int fixed,
	i32_t freference ));
void ipr_gateway_down ARGS(( ipaddr_t gateway, time_t timeout ));
void ipr_redirect ARGS(( ipaddr_t dest, ipaddr_t netmask,
	ipaddr_t old_gateway, ipaddr_t new_gateway, int new_port,
	time_t timeout ));
void ipr_destunrch ARGS(( ipaddr_t dest, ipaddr_t netmask,
	time_t timeout ));
void ipr_ttl_exc ARGS(( ipaddr_t dest, ipaddr_t netmask,
	time_t timeout ));
int ipr_get_route ARGS(( int ent_no, nwio_route_t *route_ent ));

#endif /* IPR_H */
