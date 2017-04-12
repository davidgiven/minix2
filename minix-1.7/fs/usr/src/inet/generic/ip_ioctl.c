/*
ip_ioctl.c
*/

#include "inet.h"
#include "buf.h"
#include "type.h"

#include "arp.h"
#include "assert.h"
#include "clock.h"
#include "icmp_lib.h"
#include "ip.h"
#include "ip_int.h"
#include "ipr.h"

INIT_PANIC();

FORWARD int ip_checkopt ARGS(( ip_fd_t *ip_fd ));
FORWARD void reply_thr_get ARGS(( ip_fd_t *ip_fd, size_t
	reply, int for_ioctl ));

PUBLIC int ip_ioctl (fd, req)
int fd;
int req;
{
	ip_fd_t *ip_fd;
	int type;

assert (fd>=0 && fd<=IP_FD_NR);
	ip_fd= &ip_fd_table[fd];
	type= req & IOCTYPE_MASK;

	assert (ip_fd->if_flags & IFF_INUSE);

	switch (type)
	{
	case NWIOSIPOPT & IOCTYPE_MASK:
		{
			nwio_ipopt_t *ipopt;
			nwio_ipopt_t oldopt, newopt;
			acc_t *data;
			int result;
			unsigned int new_en_flags, new_di_flags,
				old_en_flags, old_di_flags;
			unsigned long new_flags;

			if (req != NWIOSIPOPT)
				break;

			data= (*ip_fd->if_get_userdata)(ip_fd->if_srfd, 0,
				sizeof(nwio_ipopt_t), TRUE);

			data= bf_packIffLess (data, sizeof(nwio_ipopt_t));
			assert (data->acc_length == sizeof(nwio_ipopt_t));

			ipopt= (nwio_ipopt_t *)ptr2acc_data(data);
			oldopt= ip_fd->if_ipopt;
			newopt= *ipopt;

			old_en_flags= oldopt.nwio_flags & 0xffff;
			old_di_flags= (oldopt.nwio_flags >> 16) & 0xffff;
			new_en_flags= newopt.nwio_flags & 0xffff;
			new_di_flags= (newopt.nwio_flags >> 16) &
				0xffff;
			if (new_en_flags & new_di_flags)
			{
				reply_thr_get (ip_fd, EBADMODE, TRUE);
				return NW_OK;
			}

			/* NWIO_ACC_MASK */
			if (new_di_flags & NWIO_ACC_MASK)
			{
				reply_thr_get (ip_fd, EBADMODE, TRUE);
				return NW_OK;
				/* you can't disable access modes */
			}

			if (!(new_en_flags & NWIO_ACC_MASK))
				new_en_flags |= (old_en_flags &
					NWIO_ACC_MASK);

			/* NWIO_LOC_MASK */
			if (!((new_en_flags | new_di_flags) &
				NWIO_LOC_MASK))
			{
				new_en_flags |= (old_en_flags &
					NWIO_LOC_MASK);
				new_di_flags |= (old_di_flags &
					NWIO_LOC_MASK);
			}

			/* NWIO_BROAD_MASK */
			if (!((new_en_flags | new_di_flags) &
				NWIO_BROAD_MASK))
			{
				new_en_flags |= (old_en_flags &
					NWIO_BROAD_MASK);
				new_di_flags |= (old_di_flags &
					NWIO_BROAD_MASK);
			}

			/* NWIO_REM_MASK */
			if (!((new_en_flags | new_di_flags) &
				NWIO_REM_MASK))
			{
				new_en_flags |= (old_en_flags &
					NWIO_REM_MASK);
				new_di_flags |= (old_di_flags &
					NWIO_REM_MASK);
				newopt.nwio_rem= oldopt.nwio_rem;
			}

			/* NWIO_PROTO_MASK */
			if (!((new_en_flags | new_di_flags) &
				NWIO_PROTO_MASK))
			{
				new_en_flags |= (old_en_flags &
					NWIO_PROTO_MASK);
				new_di_flags |= (old_di_flags &
					NWIO_PROTO_MASK);
				newopt.nwio_proto= oldopt.nwio_proto;
			}

			/* NWIO_HDR_O_MASK */
			if (!((new_en_flags | new_di_flags) &
				NWIO_HDR_O_MASK))
			{
				new_en_flags |= (old_en_flags &
					NWIO_HDR_O_MASK);
				new_di_flags |= (old_di_flags &
					NWIO_HDR_O_MASK);
				newopt.nwio_tos= oldopt.nwio_tos;
				newopt.nwio_ttl= oldopt.nwio_ttl;
				newopt.nwio_df= oldopt.nwio_df;
				newopt.nwio_hdropt= oldopt.nwio_hdropt;
			}

			/* NWIO_RW_MASK */
			if (!((new_en_flags | new_di_flags) &
				NWIO_RW_MASK))
			{
				new_en_flags |= (old_en_flags &
					NWIO_RW_MASK);
				new_di_flags |= (old_di_flags &
					NWIO_RW_MASK);
			}

			new_flags= ((unsigned long)new_di_flags << 16) |
				new_en_flags;

			if ((new_flags & NWIO_RWDATONLY) && (new_flags &
				(NWIO_REMANY|NWIO_PROTOANY|NWIO_HDR_O_ANY)))
			{
				reply_thr_get(ip_fd, EBADMODE, TRUE);
				return NW_OK;
			}

			newopt.nwio_flags= new_flags;
			ip_fd->if_ipopt= newopt;

			result= ip_checkopt(ip_fd);

			if (result<0)
				ip_fd->if_ipopt= oldopt;

			bf_afree(data);
			reply_thr_get (ip_fd, result, TRUE);
			return NW_OK;
		}

	case NWIOGIPOPT & IOCTYPE_MASK:
		{
			nwio_ipopt_t *ipopt;
			acc_t *acc;
			int result;

			if (req != NWIOGIPOPT)
				break;
			acc= bf_memreq(sizeof(nwio_ipopt_t));

			ipopt= (nwio_ipopt_t *)ptr2acc_data(acc);

			*ipopt= ip_fd->if_ipopt;

			result= (*ip_fd->if_put_userdata)(ip_fd->
				if_srfd, 0, acc, TRUE);
			return (*ip_fd->if_put_userdata)(ip_fd->
				if_srfd, result, (acc_t *)0, TRUE);
		}
	case NWIOSIPCONF & IOCTYPE_MASK:
		{
			nwio_ipconf_t *ipconf;
			ip_port_t *ip_port;
			acc_t *data;
			int old_ip_flags;

			ip_port= ip_fd->if_port;

			if (req != NWIOSIPCONF)
				break;

			data= (*ip_fd->if_get_userdata)(ip_fd->if_srfd,
				0, sizeof(nwio_ipconf_t), TRUE);

			data= bf_packIffLess (data,
				sizeof(nwio_ipconf_t));
			assert (data->acc_length == sizeof(nwio_ipconf_t));

			old_ip_flags= ip_port->ip_flags;

			ipconf= (nwio_ipconf_t *)ptr2acc_data(data);

			if (ipconf->nwic_flags & ~NWIC_FLAGS)
				return (*ip_fd->if_put_userdata)(ip_fd->
					if_srfd, EBADMODE, (acc_t *)0, TRUE);
			if (ipconf->nwic_flags & NWIC_IPADDR_SET)
			{
				ip_port->ip_ipaddr= ipconf->nwic_ipaddr;
				ip_port->ip_flags |= IPF_IPADDRSET;
				switch (ip_port->ip_dl_type)
				{
				case IPDL_ETH:
					set_ipaddr (ip_port->ip_dl.
						dl_eth.de_port,
						ip_port->ip_ipaddr);
					break;
				default:
					ip_panic(( "unknown dl_type" ));
				}
			}
			if (ipconf->nwic_flags & NWIC_NETMASK_SET)
			{
				ip_port->ip_netmask=
					ipconf->nwic_netmask;
				ip_port->ip_flags |= IPF_NETMASKSET;
			}
			if (!(old_ip_flags & IPF_IPADDRSET) && 
				(ip_port->ip_flags & IPF_IPADDRSET) &&
				!(ip_port->ip_flags & IPF_NETMASKSET))
			{
				icmp_getnetmask(ip_port-ip_port_table);
			}

			bf_afree(data);
			return (*ip_fd->if_put_userdata)(ip_fd->
				if_srfd, NW_OK, (acc_t *)0, TRUE);
		}
	case NWIOGIPCONF & IOCTYPE_MASK:
		{
			nwio_ipconf_t *ipconf;
			ip_port_t *ip_port;
			acc_t *data;
			int result;

			ip_port= ip_fd->if_port;

			if (req != NWIOGIPCONF)
				break;

			if (!(ip_port->ip_flags & IPF_IPADDRSET))
			{
				ip_fd->if_flags |= IFF_GIPCONF_IP;
#if DEBUG & 256
 { where(); printf("(ip_fd_t *)0x%x->if_flags= 0x%x\n", ip_fd,
 ip_fd->if_flags); }
#endif
				return NW_SUSPEND;
			}
			ip_fd->if_flags &= ~IFF_GIPCONF_IP;
			data= bf_memreq(sizeof(nwio_ipconf_t));
			ipconf= (nwio_ipconf_t *)ptr2acc_data(data);
			ipconf->nwic_flags= NWIC_IPADDR_SET;
			ipconf->nwic_ipaddr= ip_port->ip_ipaddr;
			ipconf->nwic_netmask= ip_port->ip_netmask;
			if (ip_port->ip_flags & IPF_NETMASKSET)
				ipconf->nwic_flags |= NWIC_NETMASK_SET;

			result= (*ip_fd->if_put_userdata)(ip_fd->
				if_srfd, 0, data, TRUE);
			return (*ip_fd->if_put_userdata)(ip_fd->
				if_srfd, result, (acc_t *)0, TRUE);
		}
	case NWIOIPGROUTE & IOCTYPE_MASK:
		{
			acc_t *data;
			nwio_route_t *route_ent;
			int result;

			if (req != NWIOIPGROUTE)
				break;

			data= (*ip_fd->if_get_userdata)(ip_fd->if_srfd,
				0, sizeof(nwio_route_t), TRUE);

			data= bf_packIffLess (data, sizeof(nwio_route_t) );
			route_ent= (nwio_route_t *)ptr2acc_data(data);

			result= ipr_get_route(route_ent->nwr_ent_no, route_ent);
			if (result>=0)
				(*ip_fd->if_put_userdata)(ip_fd->if_srfd, 0,
					data, TRUE);
			return (*ip_fd->if_put_userdata)(ip_fd->if_srfd,
				result, (acc_t *)0, TRUE);
		}
	case NWIOIPSROUTE & IOCTYPE_MASK:
		{
			acc_t *data;
			nwio_route_t *route_ent;
			route_t *route;
			int result;

			if (req != NWIOIPSROUTE)
				break;

			data= (*ip_fd->if_get_userdata)(ip_fd->if_srfd,
				0, sizeof(nwio_route_t), TRUE);

			data= bf_packIffLess (data, sizeof(nwio_route_t) );
			route_ent= (nwio_route_t *)ptr2acc_data(data);
			route= ipr_add_route(route_ent->nwr_dest,
				route_ent->nwr_netmask, route_ent->nwr_gateway,
				ip_fd->if_port-ip_port_table, (time_t)0, 
				route_ent->nwr_dist, !!(route_ent->nwr_flags &
					NWRF_FIXED), route_ent->nwr_pref);
			bf_afree(data);
			if (route)
				result= NW_OK;
			else
			{
#if DEBUG
 { where(); printf("out of routing table entries\n"); }
#endif
				result= ENOMEM;
			}
			return (*ip_fd->if_put_userdata)(ip_fd->if_srfd,
				result, (acc_t *)0, TRUE);
		}
	default:
		break;
	}
#if DEBUG
 { where(); printf("replying EBADIOCTL\n"); }
#endif
	return (*ip_fd->if_put_userdata)(ip_fd-> if_srfd, EBADIOCTL,
		(acc_t *)0, TRUE);
}

PRIVATE int ip_checkopt (ip_fd)
ip_fd_t *ip_fd;
{
/* bug: we don't check access modes yet */

	unsigned long flags;
	unsigned int en_di_flags;
	ip_port_t *port;
	int result;

	flags= ip_fd->if_ipopt.nwio_flags;
	en_di_flags= (flags >>16) | (flags & 0xffff);

	if (flags & NWIO_HDR_O_SPEC)
	{
		result= ip_chk_hdropt (ip_fd->if_ipopt.nwio_hdropt.iho_data,
			ip_fd->if_ipopt.nwio_hdropt.iho_opt_siz);
		if (result<0)
			return result;
	}
	if ((en_di_flags & NWIO_ACC_MASK) &&
		(en_di_flags & NWIO_LOC_MASK) &&
		(en_di_flags & NWIO_BROAD_MASK) &&
		(en_di_flags & NWIO_REM_MASK) &&
		(en_di_flags & NWIO_PROTO_MASK) &&
		(en_di_flags & NWIO_HDR_O_MASK) &&
		(en_di_flags & NWIO_RW_MASK))
	{
		ip_fd->if_flags |= IFF_OPTSET;

		if (ip_fd->if_rd_buf)
			if (get_time() > ip_fd->if_exp_tim ||
				!ip_ok_for_fd(ip_fd, ip_fd->if_rd_buf))
			{
				bf_afree(ip_fd->if_rd_buf);
				ip_fd->if_rd_buf= 0;
			}
	}

	else
	{
		ip_fd->if_flags &= ~IFF_OPTSET;
		if (ip_fd->if_rd_buf)
		{
			bf_afree(ip_fd->if_rd_buf);
			ip_fd->if_rd_buf= 0;
		}
	}

	return NW_OK;
}

PRIVATE void reply_thr_get(ip_fd, reply, for_ioctl)
ip_fd_t *ip_fd;
size_t reply;
int for_ioctl;
{
	acc_t *result;
	result= (ip_fd->if_get_userdata)(ip_fd->if_srfd, reply,
		(size_t)0, for_ioctl);
	assert (!result);
}
