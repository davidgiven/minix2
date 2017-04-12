/*
eth.c
*/

#include "inet.h"
#include "buf.h"
#include "clock.h"
#include "osdep_eth.h"

#include "assert.h"
#include "buf.h"
#include "eth.h"
#include "eth_int.h"
#include "io.h"
#include "sr.h"
#include "type.h"

INIT_PANIC();

#define ETH_FD_NR	32
#define EXPIRE_TIME	60*HZ	/* seconds */

typedef struct eth_fd
{
	int ef_flags;
	eth_port_t *ef_port;
	int ef_srfd;
	get_userdata_t ef_get_userdata;
	put_userdata_t ef_put_userdata;
	nwio_ethopt_t ef_ethopt;
	size_t ef_write_count;
	acc_t *ef_rd_buf;
	acc_t *ef_rd_tail;
	time_t ef_exp_tim;
	int ef_pack_stat;
} eth_fd_t;

#define EFF_FLAGS	0xf
#	define EFF_EMPTY	0x0
#	define EFF_INUSE	0x1
#	define EFF_BUSY		0x6
#		define	EFF_READ_IP	0x2
#		define 	EFF_WRITE_IP	0x4
#	define EFF_OPTSET       0x8

FORWARD int eth_checkopt ARGS(( eth_fd_t *eth_fd ));
FORWARD void eth_buffree ARGS(( int priority, size_t reqsize ));
FORWARD int ok_for_me ARGS(( eth_fd_t *fd, acc_t *pack ));
FORWARD int packet2user ARGS(( eth_fd_t *fd ));
FORWARD void reply_thr_get ARGS(( eth_fd_t *eth_fd,
	size_t result, int for_ioctl ));
FORWARD void reply_thr_put ARGS(( eth_fd_t *eth_fd,
	size_t result, int for_ioctl ));
FORWARD void restart_write_fd ARGS(( eth_fd_t *eth_fd ));

PUBLIC eth_port_t eth_port_table[ETH_PORT_NR];

PRIVATE eth_fd_t eth_fd_table[ETH_FD_NR];
/* PRIVATE message mess, repl_mess; */

PUBLIC void eth_init()
{
	int i;

	assert (BUF_S >= sizeof(nwio_ethopt_t));
	assert (BUF_S >= ETH_HDR_SIZE);	/* these are in fact static assertions,
					   thus a good compiler doesn't
					   generate any code for this */


	for (i=0; i<ETH_FD_NR; i++)
		eth_fd_table[i].ef_flags= EFF_EMPTY;
	for (i=0; i<ETH_PORT_NR; i++)
		eth_port_table[i].etp_flags= EFF_EMPTY;

	bf_logon(eth_buffree);

	eth_init0();
	/* eth_init1(); etc */
}

PUBLIC int eth_open(port, srfd, get_userdata, put_userdata)
int port, srfd;
get_userdata_t get_userdata;
put_userdata_t put_userdata;
{
	int i;
	eth_port_t *eth_port;
	eth_fd_t *eth_fd;

#if DEBUG & 256
 { where(); printf("eth_open (%d, ...)\n", port); }
#endif
	eth_port= &eth_port_table[port];
	if (!(eth_port->etp_flags & EPF_ENABLED))
		return EGENERIC;

	for (i=0; i<ETH_FD_NR && (eth_fd_table[i].ef_flags & EFF_INUSE);
		i++);

	if (i>=ETH_FD_NR)
	{
#if DEBUG
 { where(); printf("out of fds\n"); }
#endif
		return EOUTOFBUFS;
	}

	eth_fd= &eth_fd_table[i];

	eth_fd->ef_flags= EFF_INUSE;
	eth_fd->ef_ethopt.nweo_flags=NWEO_DEFAULT;
	eth_fd->ef_port= eth_port;
	eth_fd->ef_srfd= srfd;
	eth_fd->ef_rd_buf= 0;
	eth_fd->ef_get_userdata= get_userdata;
	eth_fd->ef_put_userdata= put_userdata;
	return i;
}

PUBLIC int eth_ioctl(fd, req)
int fd;
int req;
{
	acc_t *data;
	int type;
	eth_fd_t *eth_fd;
	eth_port_t *eth_port;

#if DEBUG & 256
 { where(); printf("eth_ioctl (%d, ...)\n", fd); }
#endif
	eth_fd= &eth_fd_table[fd];
	eth_port= eth_fd->ef_port;
	type= req & IOCTYPE_MASK;

	assert (eth_fd->ef_flags & EFF_INUSE);

	switch (type)
	{
	case NWIOSETHOPT & IOCTYPE_MASK:
		{
			nwio_ethopt_t *ethopt;
			nwio_ethopt_t oldopt, newopt;
			int result;
			u32_t new_en_flags, new_di_flags,
				old_en_flags, old_di_flags;
			u32_t flags;
			eth_fd_t *loc_fd;
			int i;

			if (req != NWIOSETHOPT)
				break;
	
#if DEBUG & 256
 { where(); printf("calling *get_userdata\n"); }
#endif
			data= (*eth_fd->ef_get_userdata)(eth_fd->
				ef_srfd, 0, sizeof(nwio_ethopt_t), TRUE);

                        ethopt= (nwio_ethopt_t *)ptr2acc_data(data);
			oldopt= eth_fd->ef_ethopt;
			newopt= *ethopt;

#if DEBUG & 256
 { where(); printf("newopt.nweo_flags= 0x%x\n", newopt.nweo_flags); }
#endif
			old_en_flags= oldopt.nweo_flags & 0xffff;
			old_di_flags= (oldopt.nweo_flags >> 16) & 0xffff;
			new_en_flags= newopt.nweo_flags & 0xffff;
			new_di_flags= (newopt.nweo_flags >> 16) & 0xffff;
			if (new_en_flags & new_di_flags)
			{
				bf_afree(data);
				reply_thr_get (eth_fd, EBADMODE, TRUE);
				return NW_OK;
			}	

			/* NWEO_ACC_MASK */
			if (new_di_flags & NWEO_ACC_MASK)
			{
				bf_afree(data);
				reply_thr_get (eth_fd, EBADMODE, TRUE);
				return NW_OK;
			}	
					/* you can't disable access modes */

			if (!(new_en_flags & NWEO_ACC_MASK))
				new_en_flags |= (old_en_flags & NWEO_ACC_MASK);


			/* NWEO_LOC_MASK */
			if (!((new_en_flags | new_di_flags) & NWEO_LOC_MASK))
			{
				new_en_flags |= (old_en_flags & NWEO_LOC_MASK);
				new_di_flags |= (old_di_flags & NWEO_LOC_MASK);
			}

			/* NWEO_BROAD_MASK */
			if (!((new_en_flags | new_di_flags) & NWEO_BROAD_MASK))
			{
				new_en_flags |= (old_en_flags & NWEO_BROAD_MASK);
				new_di_flags |= (old_di_flags & NWEO_BROAD_MASK);
			}

			/* NWEO_MULTI_MASK */
			if (!((new_en_flags | new_di_flags) & NWEO_MULTI_MASK))
			{
				new_en_flags |= (old_en_flags & NWEO_MULTI_MASK);
				new_di_flags |= (old_di_flags & NWEO_MULTI_MASK);
				newopt.nweo_multi= oldopt.nweo_multi;
			}

			/* NWEO_PROMISC_MASK */
			if (!((new_en_flags | new_di_flags) & NWEO_PROMISC_MASK))
			{
				new_en_flags |= (old_en_flags & NWEO_PROMISC_MASK);
				new_di_flags |= (old_di_flags & NWEO_PROMISC_MASK);
			}

			/* NWEO_REM_MASK */
			if (!((new_en_flags | new_di_flags) & NWEO_REM_MASK))
			{
				new_en_flags |= (old_en_flags & NWEO_REM_MASK);
				new_di_flags |= (old_di_flags & NWEO_REM_MASK);
				newopt.nweo_rem= oldopt.nweo_rem;
			}

			/* NWEO_TYPE_MASK */
			if (!((new_en_flags | new_di_flags) & NWEO_TYPE_MASK))
			{
				new_en_flags |= (old_en_flags & NWEO_TYPE_MASK);
				new_di_flags |= (old_di_flags & NWEO_TYPE_MASK);
				newopt.nweo_type= oldopt.nweo_type;
			}

			/* NWEO_RW_MASK */
			if (!((new_en_flags | new_di_flags) & NWEO_RW_MASK))
			{
				new_en_flags |= (old_en_flags & NWEO_RW_MASK);
				new_di_flags |= (old_di_flags & NWEO_RW_MASK);
			}

			newopt.nweo_flags= ((unsigned long)new_di_flags << 16) |
				new_en_flags;

			eth_fd->ef_ethopt= newopt;

			result= eth_checkopt(eth_fd);

			if (result<0)
				eth_fd->ef_ethopt= oldopt;
			else
			{
				unsigned long opt_flags;
				unsigned changes;
				opt_flags= oldopt.nweo_flags ^
					eth_fd->ef_ethopt.nweo_flags;
				changes= ((opt_flags >> 16) | opt_flags) &
					0xffff;
				if (changes & (NWEO_BROAD_MASK |
					NWEO_MULTI_MASK | NWEO_PROMISC_MASK))
				{
					flags= NWEO_NOFLAGS;
					for (i=0, loc_fd= eth_fd_table; 
						i<ETH_FD_NR; i++, loc_fd++)
					{
						if (!(loc_fd->ef_flags | 
						~(EFF_INUSE | EFF_OPTSET)))
							continue;
						if (loc_fd->ef_port 
							!= eth_port)
							continue;
						flags |= loc_fd->ef_ethopt.
							nweo_flags;
					}
					eth_set_rec_conf(eth_port, flags);
				}
			}

			bf_afree(data);
			reply_thr_get (eth_fd, result, TRUE);
			return NW_OK;	
		}

	case NWIOGETHOPT & IOCTYPE_MASK:
		{
			nwio_ethopt_t *ethopt;
			acc_t *acc;
			int result;

			if (req != NWIOGETHOPT)
				break;
			acc= bf_memreq(sizeof(nwio_ethopt_t));

			ethopt= (nwio_ethopt_t *)ptr2acc_data(acc);

			*ethopt= eth_fd->ef_ethopt;

			result= (*eth_fd->ef_put_userdata)(eth_fd->
				ef_srfd, 0, acc, TRUE);
			if (result >= 0)
				reply_thr_put(eth_fd, NW_OK, TRUE);
			return result;
		}
	case NWIOGETHSTAT & IOCTYPE_MASK:
		{
			nwio_ethstat_t *ethstat;
			acc_t *acc;
			int result;

assert (sizeof(nwio_ethstat_t) <= BUF_S);

			if (req != NWIOGETHSTAT)
				break;

			eth_port= eth_fd->ef_port;
			if (!(eth_port->etp_flags & EPF_ENABLED))
			{
				reply_thr_put(eth_fd, EGENERIC, TRUE);
				return NW_OK;
			}

			acc= bf_memreq(sizeof(nwio_ethstat_t));
compare (bf_bufsize(acc), ==, sizeof(*ethstat));

			ethstat= (nwio_ethstat_t *)ptr2acc_data(acc);

			ethstat->nwes_addr= eth_port->etp_ethaddr;

			result= eth_get_stat(eth_port, &ethstat->nwes_stat);
assert (result == 0);
#if DEBUG & 256
 { where(); printf("returning NW_OK\n"); }
#endif
compare (bf_bufsize(acc), ==, sizeof(*ethstat));
			result= (*eth_fd->ef_put_userdata)(eth_fd->
				ef_srfd, 0, acc, TRUE);
			if (result >= 0)
				reply_thr_put(eth_fd, NW_OK, TRUE);
			return result;
		}
	default:
		break;
	}
	reply_thr_put(eth_fd, EBADIOCTL, TRUE);
	return NW_OK;
}

PUBLIC int eth_write(fd, count)
int fd;
size_t count;
{
	eth_fd_t *eth_fd;
	eth_port_t *eth_port;

#if DEBUG & 256
 { where(); printf("eth_write (%d, ...)\n", fd); }
#endif
	eth_fd= &eth_fd_table[fd];
	eth_port= eth_fd->ef_port;

	if (!(eth_fd->ef_flags & EFF_OPTSET))
	{
		reply_thr_get (eth_fd, EBADMODE, FALSE);
		return NW_OK;
	}

	assert (!(eth_fd->ef_flags & EFF_WRITE_IP));

	eth_fd->ef_write_count= count;
	if (eth_fd->ef_ethopt.nweo_flags & NWEO_RWDATONLY)
		count += ETH_HDR_SIZE;

	if (count<ETH_MIN_PACK_SIZE || count>ETH_MAX_PACK_SIZE)
	{
#if DEBUG
 { where(); printf("illegal packetsize (%d)\n",count); }
#endif
		reply_thr_get (eth_fd, EPACKSIZE, FALSE);
		return NW_OK;
	}
	eth_fd->ef_flags |= EFF_WRITE_IP;
	restart_write_fd(eth_fd);
	if (eth_fd->ef_flags & EFF_WRITE_IP)
		return NW_SUSPEND;
	else
		return NW_OK;
}

PUBLIC int eth_read (fd, count)
int fd;
size_t count;
{
	eth_fd_t *eth_fd;
	acc_t *acc, *acc2;

#if DEBUG & 256
 { where(); printf("eth_read (%d, ...)\n", fd); }
#endif
	eth_fd= &eth_fd_table[fd];
	if (!(eth_fd->ef_flags & EFF_OPTSET))
	{
		reply_thr_put(eth_fd, EBADMODE, FALSE);
		return NW_OK;
	}
	if (count < ETH_MAX_PACK_SIZE)
	{
		reply_thr_put(eth_fd, EPACKSIZE, FALSE);
		return NW_OK;
	}
	if (eth_fd->ef_rd_buf)
	{
		if (get_time() <= eth_fd->ef_exp_tim)
			return packet2user (eth_fd);
		for (acc= eth_fd->ef_rd_buf; acc;)
		{
			acc2= acc->acc_ext_link;
			bf_afree(acc);
			acc= acc2;
		}
		eth_fd->ef_rd_buf= 0;
	}
	eth_fd->ef_flags |= EFF_READ_IP;
#if DEBUG & 256
 { where(); printf("eth_fd_table[%d].ef_flags= 0x%x\n",
	eth_fd-eth_fd_table, eth_fd->ef_flags); }
#endif
	return NW_SUSPEND;
}

PUBLIC int eth_cancel(fd, which_operation)
int fd;
int which_operation;
{
	eth_fd_t *eth_fd;

#if DEBUG & 2
 { where(); printf("eth_cancel (%d)\n", fd); }
#endif
	eth_fd= &eth_fd_table[fd];

	switch (which_operation)
	{
	case SR_CANCEL_READ:
assert (eth_fd->ef_flags & EFF_READ_IP);
		eth_fd->ef_flags &= ~EFF_READ_IP;
		reply_thr_put(eth_fd, EINTR, FALSE);
		break;
	case SR_CANCEL_WRITE:
assert (eth_fd->ef_flags & EFF_WRITE_IP);
		eth_fd->ef_flags &= ~EFF_WRITE_IP;
		reply_thr_get(eth_fd, EINTR, FALSE);
		break;
	default:
		ip_panic(( "got unknown cancel request" ));
	}
	return NW_OK;
}

PUBLIC void eth_close(fd)
int fd;
{
	eth_fd_t *eth_fd;
	acc_t *acc, *acc2;

#if DEBUG
 { where(); printf("eth_close (%d)\n", fd); }
#endif
	eth_fd= &eth_fd_table[fd];

	assert (eth_fd->ef_flags & EFF_INUSE);

	eth_fd->ef_flags= EFF_EMPTY;
	for (acc= eth_fd->ef_rd_buf; acc;)
	{
		acc2= acc->acc_ext_link;
		bf_afree(acc);
		acc= acc2;
	}
	eth_fd->ef_rd_buf= 0;
}

PRIVATE int eth_checkopt (eth_fd)
eth_fd_t *eth_fd;
{
/* bug: we don't check access modes yet */

	unsigned long flags;
	unsigned int en_di_flags;
	eth_port_t *eth_port;
	acc_t *acc, *acc2;

	eth_port= eth_fd->ef_port;
	flags= eth_fd->ef_ethopt.nweo_flags;
#if DEBUG & 256
 { where(); printf("eth_fd_table[%d].ef_ethopt.nweo_flags= 0x%x\n",
	eth_fd-eth_fd_table, flags); }
#endif
	en_di_flags= (flags >>16) | (flags & 0xffff);

	if ((en_di_flags & NWEO_ACC_MASK) &&
		(en_di_flags & NWEO_LOC_MASK) &&
		(en_di_flags & NWEO_BROAD_MASK) &&
		(en_di_flags & NWEO_MULTI_MASK) &&
		(en_di_flags & NWEO_PROMISC_MASK) &&
		(en_di_flags & NWEO_REM_MASK) &&
		(en_di_flags & NWEO_TYPE_MASK) &&
		(en_di_flags & NWEO_RW_MASK))
	{
		eth_fd->ef_flags |= EFF_OPTSET;
		eth_fd->ef_pack_stat= EPS_EMPTY;
		if (flags & NWEO_EN_LOC)
			eth_fd->ef_pack_stat |= EPS_LOC;
		if (flags & NWEO_EN_BROAD)
			eth_fd->ef_pack_stat |= EPS_BROAD;
		if (flags & NWEO_EN_MULTI)
			eth_fd->ef_pack_stat |= EPS_MULTI;
		if (flags & NWEO_EN_PROMISC)
			eth_fd->ef_pack_stat |= (EPS_PROMISC|EPS_MULTI|
				EPS_BROAD);
	}
	else
		eth_fd->ef_flags &= ~EFF_OPTSET;
	
	for (acc= eth_fd->ef_rd_buf; acc;)
	{
		acc2= acc->acc_ext_link;
		bf_afree(acc);
		acc= acc2;
	}
	eth_fd->ef_rd_buf= 0;

	return NW_OK;
}

PUBLIC int eth_get_work(eth_port)
eth_port_t *eth_port;
{
	eth_fd_t *eth_fd;
	int i;

#if DEBUG & 256
 { where(); printf("eth_get_work called\n"); }
#endif
	if (eth_port->etp_wr_pack)
		return 0;
	if (!(eth_port->etp_flags & EPF_MORE2WRITE))
		return 0;

	for (i=0, eth_fd= eth_fd_table; i<ETH_FD_NR; i++, eth_fd++)
	{
		if ((eth_fd->ef_flags & (EFF_INUSE|EFF_WRITE_IP)) !=
			(EFF_INUSE|EFF_WRITE_IP))
			continue;
		if (eth_fd->ef_port != eth_port)
			continue;
#if DEBUG & 256
 { where(); printf("eth_get_work calling restart_write_fd\n"); }
#endif
		restart_write_fd(eth_fd);
		if (eth_port->etp_wr_pack)
			return 1;
	}
	eth_port->etp_flags &= ~EPF_MORE2WRITE;
	return 0;
}

PUBLIC void eth_arrive (eth_port, pack)
eth_port_t *eth_port;
acc_t *pack;
{
	time_t exp_tim;
	eth_hdr_t *eth_hdr;
	static ether_addr_t broadcast= {255, 255, 255, 255, 255, 255},
		multi_addr, rem_addr, packaddr;
	int pack_stat;
	ether_type_t type;
	eth_fd_t *eth_fd, *share_fd;
	acc_t *acc;
	int i;

#if DEBUG & 256
 { where(); printf("eth_arrive(0x%x, 0x%x) called\n", eth_port, pack); }
#endif
assert(pack->acc_linkC);
	exp_tim= get_time() + EXPIRE_TIME;

	pack= bf_packIffLess(pack, ETH_HDR_SIZE);
	eth_hdr= (eth_hdr_t*)ptr2acc_data(pack);
#if DEBUG & 256
 { where(); printf("src= "); writeEtherAddr(&eth_hdr->eh_src); printf(" dst= "); 	writeEtherAddr(&eth_hdr->eh_dst);
	printf(" proto= 0x%x\n", ntohs(eth_hdr->eh_proto));
	printf(" my addr= "); writeEtherAddr(&eth_port->etp_ethaddr);
	printf("\n"); }
#endif

	packaddr= eth_hdr->eh_dst;
	if (packaddr.ea_addr[0] & 0x01)
	{
		/* multi cast or broadcast */
		if (!eth_addrcmp(packaddr, broadcast))
			pack_stat= EPS_BROAD;
		else
		{
			pack_stat= EPS_MULTI;
#if DEBUG
 { where(); printf("Got a multicast packet\n"); }
#endif
		}
	}
	else
	{
		if (!eth_addrcmp (packaddr, eth_port->etp_ethaddr))
			pack_stat= EPS_LOC;
		else
			pack_stat= EPS_PROMISC;
	}
	type= eth_hdr->eh_proto;

#if DEBUG & 256
 { where(); printf("pack_stat= 0x%x\n", pack_stat); }
#endif

	share_fd= 0;
	for (i=0, eth_fd=eth_fd_table; i<ETH_FD_NR; i++, eth_fd++)
	{
		if (!(eth_fd->ef_flags & EFF_OPTSET))
		{
#if DEBUG & 256
 { where(); printf("fd %d doesn't have EFF_OPTSET\n", i); }
#endif
			continue;
		}
		if (eth_fd->ef_port != eth_port)
		{
#if DEBUG
 { where(); printf("fd %d uses port %d, packet is on port %d\n", i, 
	eth_fd->ef_port-eth_port_table, eth_port-eth_port_table); }
#endif
			continue;
		}
		if (!(eth_fd->ef_pack_stat & pack_stat))
		{
#if DEBUG & 256
 { where(); printf("fd %d has ef_pack_stat 0x%x, expecting 0x%x\n", i,
	eth_fd->ef_pack_stat, pack_stat); }
#endif
			continue;
		}
		if ((eth_fd->ef_ethopt.nweo_flags & NWEO_TYPESPEC) &&
			type != eth_fd->ef_ethopt.nweo_type)
		{
#if DEBUG & 256
 { where(); printf("fd %d uses type 0x%x, expecting 0x%x\n", i,
	eth_fd->ef_ethopt.nweo_type, type); }
#endif
			continue;
		}
#if DEBUG & 256
 { where(); printf("multi OK\n"); }
#endif
		if (eth_fd->ef_ethopt.nweo_flags & NWEO_REMSPEC)
		{
			rem_addr= eth_fd->ef_ethopt.nweo_rem;
			if (eth_addrcmp (eth_hdr->eh_src,
				rem_addr))
				continue;
		}
#if DEBUG & 256
 { where(); printf("rem NW_OK\n"); }
#endif
		if (eth_fd->ef_rd_buf)
		{
			if (eth_fd->ef_ethopt.nweo_flags == NWEO_SHARED)
			{
				share_fd= eth_fd;
				continue;
			}
		}
		acc= bf_dupacc(pack);
		acc->acc_ext_link= NULL;
		if (!eth_fd->ef_rd_buf)
		{
			eth_fd->ef_rd_buf= acc;
			eth_fd->ef_exp_tim= exp_tim;
		}
		else
			eth_fd->ef_rd_tail->acc_ext_link= acc;
		eth_fd->ef_rd_tail= acc;

		if (eth_fd->ef_flags & EFF_READ_IP)
			packet2user(eth_fd);
		if ((eth_fd->ef_ethopt.nweo_flags & NWEO_ACC_MASK) != NWEO_COPY)
		{
			bf_afree(pack);
			pack= 0;
			break;
		}
	}
	if (share_fd && pack)
	{
		acc= bf_dupacc(pack);
		acc->acc_ext_link= NULL;
		if (!share_fd->ef_rd_buf)
		{
			share_fd->ef_rd_buf= acc;
			share_fd->ef_exp_tim= exp_tim;
		}
		else
			share_fd->ef_rd_tail->acc_ext_link= acc;
		share_fd->ef_rd_tail= acc;
	}
	if (pack)
		bf_afree(pack);
}

PRIVATE int packet2user (eth_fd)
eth_fd_t *eth_fd;
{
	acc_t *pack, *header;
	int result;
	size_t size;

#if DEBUG & 256
 { where(); printf("packet2user() called\n"); }
#endif
	pack= eth_fd->ef_rd_buf;
	eth_fd->ef_rd_buf= pack->acc_ext_link;
	if (eth_fd->ef_ethopt.nweo_flags & NWEO_RWDATONLY)
	{
		pack= bf_packIffLess (pack, ETH_HDR_SIZE);

		assert (pack->acc_length >= ETH_HDR_SIZE);

		if (pack->acc_linkC >1)
		{
			header= bf_dupacc (pack);
			bf_afree(pack);
			pack= header;
		}

		assert (pack->acc_linkC == 1);

		pack->acc_offset += ETH_HDR_SIZE;
		pack->acc_length -= ETH_HDR_SIZE;
	}

	size= bf_bufsize (pack);

	eth_fd->ef_flags &= ~EFF_READ_IP;
	result= (*eth_fd->ef_put_userdata)(eth_fd->ef_srfd, (size_t)0, pack,
		FALSE);
	if (result >=0)
		reply_thr_put(eth_fd, size, FALSE);
	return result<0 ? result : NW_OK;
}

PRIVATE int ok_for_me (eth_fd, pack)
eth_fd_t *eth_fd;
acc_t *pack;
{
	eth_port_t *eth_port;
	eth_hdr_t *eth_hdr;
	ether_type_t type;
	static ether_addr_t broadcast= {255, 255, 255, 255, 255, 255},
		packaddr, portaddr, multi_addr, rem_addr;
	int pack_kind;

	assert (pack->acc_length >= ETH_HDR_SIZE);

	eth_port= eth_fd->ef_port;

	eth_hdr= (eth_hdr_t *)ptr2acc_data(pack);
	packaddr= eth_hdr->eh_dst;
	if (packaddr.ea_addr[0] & 0x01)
		/* multi cast or broadcast */
		if (!eth_addrcmp (packaddr, broadcast))
			pack_kind= EPS_BROAD;
		else
			pack_kind= EPS_MULTI;
	else
	{
		portaddr= eth_port->etp_ethaddr;
		if (!eth_addrcmp(packaddr, portaddr))
			pack_kind= EPS_LOC;
		else
			pack_kind= EPS_PROMISC;
	}

	pack_kind &= eth_fd->ef_pack_stat;

	if (!pack_kind)
		return FALSE;

	type= eth_hdr->eh_proto;

	if ((eth_fd->ef_ethopt.nweo_flags & NWEO_TYPESPEC) &&
		type != eth_fd->ef_ethopt.nweo_type)
		return FALSE;

	if (eth_fd->ef_ethopt.nweo_flags & NWEO_REMSPEC)
	{
		rem_addr= eth_fd->ef_ethopt.nweo_rem;
		if (eth_addrcmp(eth_hdr->eh_src, rem_addr))
			return FALSE;
	}
	return TRUE;
}

PRIVATE void eth_buffree (priority, reqsize)
int priority;
size_t reqsize;
{
	int i, once_more;
	time_t curr_tim;
	acc_t *acc;

	if (priority <ETH_PRI_EXP_FDBUFS)
		return;

#if DEBUG & 256
 { where(); printf("eth_buffree called\n"); }
#endif

	curr_tim= get_time();
	for (i=0; i<ETH_FD_NR; i++)
	{
		if (!(eth_fd_table[i].ef_flags & EFF_INUSE) )
			continue;
		acc= eth_fd_table[i].ef_rd_buf;
		if (acc && eth_fd_table[i].ef_exp_tim < curr_tim)
		{
			eth_fd_table[i].ef_rd_buf= acc->acc_ext_link;
			bf_afree(acc);
			if (bf_free_buffsize >= reqsize)
				return;
		}
	}

	if (priority <ETH_PRI_FDBUFS)
		return;

	once_more= 1;
	while (once_more)
	{
		once_more= 0;
		for (i=0; i<ETH_FD_NR; i++)
		{
			if (!(eth_fd_table[i].ef_flags & EFF_INUSE))
				continue;
			acc= eth_fd_table[i].ef_rd_buf;
			if (acc)
			{
				eth_fd_table[i].ef_rd_buf= acc->acc_ext_link;
				bf_afree(acc);
				if (bf_free_buffsize >= reqsize)
					return;
				once_more= 1;
			}
		}
	}
}

PRIVATE void restart_write_fd(eth_fd)
eth_fd_t *eth_fd;
{
	eth_port_t *eth_port;
	acc_t *user_data, *header;
	int size;
	unsigned long nweo_flags;
	eth_hdr_t *eth_hdr;

	eth_port= eth_fd->ef_port;

	if (eth_port->etp_wr_pack)
	{
		eth_port->etp_flags |= EPF_MORE2WRITE;
		return;
	}

assert (eth_fd->ef_flags & EFF_WRITE_IP);
	eth_fd->ef_flags &= ~EFF_WRITE_IP;

assert (!eth_port->etp_wr_pack);

#if DEBUG & 256
 { where(); printf("calling *get_userdata\n"); }
#endif
	user_data= (*eth_fd->ef_get_userdata)(eth_fd->ef_srfd, 0,
		eth_fd->ef_write_count, FALSE);
	if (!user_data)
	{
		eth_fd->ef_flags &= ~EFF_WRITE_IP;
		reply_thr_get (eth_fd, EFAULT, FALSE);
		return;
	}
	size= bf_bufsize (user_data);

	nweo_flags= eth_fd->ef_ethopt.nweo_flags;

	if (nweo_flags & NWEO_RWDATONLY)
	{
		header= bf_memreq(ETH_HDR_SIZE);
		header->acc_next= user_data;
		user_data= header;
	}

	user_data= bf_packIffLess (user_data, ETH_HDR_SIZE);

	eth_hdr= (eth_hdr_t *)ptr2acc_data(user_data);

	if (nweo_flags & NWEO_REMSPEC)
		eth_hdr->eh_dst= eth_fd->ef_ethopt.nweo_rem;

	eth_hdr->eh_src= eth_port->etp_ethaddr;

	if (nweo_flags & NWEO_TYPESPEC)
		eth_hdr->eh_proto= eth_fd->ef_ethopt.nweo_type;

assert (!eth_port->etp_wr_pack);
	eth_port->etp_wr_pack= user_data;

	if (!(eth_port->etp_flags & EPF_WRITE_IP))
	{
		eth_write_port(eth_port);
	}
	reply_thr_get (eth_fd, size, FALSE);
}

PRIVATE void reply_thr_get (eth_fd, result, for_ioctl)
eth_fd_t *eth_fd;
size_t result;
int for_ioctl;
{
	acc_t *data;

#if DEBUG & 256
 { where(); printf("calling *get_userdata(fd= %d, %d, 0)\n", eth_fd->
	ef_srfd, result, 0); }
#endif
	data= (*eth_fd->ef_get_userdata)(eth_fd->ef_srfd, result, 0, for_ioctl);
assert (!data);	
}

PRIVATE void reply_thr_put (eth_fd, result, for_ioctl)
eth_fd_t *eth_fd;
size_t result;
int for_ioctl;
{
	int error;

	error= (*eth_fd->ef_put_userdata)(eth_fd->ef_srfd, result, (acc_t *)0,
		for_ioctl);
assert(!error);
}
