/*
udp.c
*/

#include "inet.h"

#include "assert.h"
#include "buf.h"
#include "clock.h"
#include "io.h"
#include "ip.h"
#include "sr.h"
#include "type.h"
#include "udp.h"

INIT_PANIC();

#define UDP_PORT_NR	1
#define UDP_FD_NR	32

typedef struct udp_port
{
	int up_flags;
	int up_state;
	int up_ipfd;
	int up_minor;
	int up_ipdev;
	acc_t *up_wr_pack;
	ipaddr_t up_ipaddr;
	struct udp_fd *up_next_fd;
	struct udp_fd *up_write_fd;
} udp_port_t;

#define UPF_EMPTY	0x0
#define UPF_WRITE_IP	0x1
#define UPF_WRITE_SP	0x2
#define UPF_READ_IP	0x4
#define UPF_READ_SP	0x8
#define UPF_SUSPEND	0x10
#define UPF_MORE2WRITE	0x20

#define UPS_EMPTY	0
#define UPS_SETPROTO	1
#define UPS_GETCONF	2
#define UPS_MAIN	3
#define UPS_ERROR	4

typedef struct udp_fd
{
	int uf_flags;
	udp_port_t *uf_port;
	int uf_ioreq;
	int uf_srfd;
	nwio_udpopt_t uf_udpopt;
	get_userdata_t uf_get_userdata;
	put_userdata_t uf_put_userdata;
	acc_t *uf_pack;
	acc_t *uf_rd_buf;
	size_t uf_rd_count;
	size_t uf_wr_count;
	time_t uf_exp_tim;
} udp_fd_t;

#define UFF_EMPTY	0x0
#define UFF_INUSE	0x1
#define UFF_IOCTL_IP	0x2
#define UFF_READ_IP	0x4
#define UFF_WRITE_IP	0x8
#define UFF_OPTSET	0x10

FORWARD void read_ip_packets ARGS(( udp_port_t *udp_port ));
FORWARD void udp_buffree ARGS(( int priority, size_t reqsize ));
FORWARD void udp_main ARGS(( udp_port_t *udp_port ));
FORWARD acc_t *udp_get_data ARGS(( int fd, size_t offset, size_t count, 
	int for_ioctl ));
FORWARD int udp_put_data ARGS(( int fd, size_t offset, acc_t *data, 	
	int for_ioctl ));
FORWARD void udp_restart_write_port ARGS(( udp_port_t *udp_port ));
FORWARD void process_inc_fragm ARGS(( udp_port_t *udp_port, acc_t *data ));
FORWARD int reply_thr_put ARGS(( udp_fd_t *ucp_fd, int reply,
	int for_ioctl ));
FORWARD void reply_thr_get ARGS(( udp_fd_t *udp_fd, int reply,
	int for_ioctl ));
FORWARD int udp_setopt ARGS(( udp_fd_t *udp_fd ));
FORWARD udpport_t find_unused_port ARGS(( int fd ));
FORWARD int is_unused_port ARGS(( Udpport_t port ));
FORWARD int udp_packet2user ARGS(( udp_fd_t *udp_fd ));
FORWARD void restart_write_fd ARGS(( udp_fd_t *udp_fd ));
FORWARD u16_t pack_oneCsum ARGS(( acc_t *pack ));

PRIVATE udp_port_t udp_port_table[UDP_PORT_NR];
PRIVATE udp_fd_t udp_fd_table[UDP_FD_NR];

PUBLIC void udp_init()
{
	udp_fd_t *udp_fd;
	udp_port_t *udp_port;
	int i, result;

	assert (BUF_S >= sizeof(struct nwio_ipopt));
	assert (BUF_S >= sizeof(struct nwio_ipconf));
	assert (BUF_S >= sizeof(struct nwio_udpopt));
	assert (BUF_S >= sizeof(struct udp_io_hdr));
	assert (UDP_HDR_SIZE == sizeof(udp_hdr_t));
	assert (UDP_IO_HDR_SIZE == sizeof(udp_io_hdr_t));

	udp_port_table[0].up_minor= UDP_DEV0;
	udp_port_table[0].up_ipdev= IP0;

	for (i= 0, udp_fd= udp_fd_table; i<UDP_FD_NR; i++, udp_fd++)
	{
		udp_fd->uf_flags= UFF_EMPTY;
	}

	bf_logon(udp_buffree);

	for (i= 0, udp_port= udp_port_table; i<UDP_PORT_NR; i++, udp_port++)
	{
		udp_port->up_flags= UPF_EMPTY;
		udp_port->up_state= UPS_EMPTY;
		udp_port->up_next_fd= udp_fd_table;
		udp_port->up_write_fd= NULL;

		result= sr_add_minor (udp_port->up_minor,
			udp_port-udp_port_table, udp_open, udp_close, udp_read,
			udp_write, udp_ioctl, udp_cancel);
		assert (result >= 0);

		udp_main(udp_port);
	}
}

PRIVATE void udp_main(udp_port)
udp_port_t *udp_port;
{
	udp_fd_t *udp_fd;
	int result, i;

	switch (udp_port->up_state)
	{
	case UPS_EMPTY:
		udp_port->up_state= UPS_SETPROTO;

		udp_port->up_ipfd= ip_open(udp_port->up_ipdev, 
			udp_port-udp_port_table, udp_get_data, udp_put_data);
		if (udp_port->up_ipfd < 0)
		{
			udp_port->up_state= UPS_ERROR;
			printf("%s, %d: unable to open ip port\n", __FILE__,
				__LINE__);
			return;
		}

		result= ip_ioctl(udp_port->up_ipfd, NWIOSIPOPT);
		if (result == NW_SUSPEND)
			udp_port->up_flags |= UPF_SUSPEND;
		if (result<0)
		{
			return;
		}
		if (udp_port->up_state != UPS_GETCONF)
			return;
		/* drops through */
	case UPS_GETCONF:
		udp_port->up_flags &= ~UPF_SUSPEND;

		result= ip_ioctl(udp_port->up_ipfd, NWIOGIPCONF);
		if (result == NW_SUSPEND)
			udp_port->up_flags |= UPF_SUSPEND;
		if (result<0)
		{
			return;
		}
		if (udp_port->up_state != UPS_MAIN)
			return;
		/* drops through */
	case UPS_MAIN:
		udp_port->up_flags &= ~UPF_SUSPEND;

		for (i= 0, udp_fd= udp_fd_table; i<UDP_FD_NR; i++, udp_fd++)
		{
			if (!(udp_fd->uf_flags & UFF_INUSE))
				continue;
			if (udp_fd->uf_port != udp_port)
				continue;
			if (udp_fd->uf_flags & UFF_IOCTL_IP)
				udp_ioctl(i, udp_fd->uf_ioreq);
		}
		read_ip_packets(udp_port);
		return;
	default:
#if DEBUG
 { where(); printf("udp_port_table[%d].up_state= %d\n", udp_port-udp_port_table,
	udp_port->up_state); }
#endif
		ip_panic(( "unknown state" ));
		break;
	}
}

int udp_open (port, srfd, get_userdata, put_userdata)
int port;
int srfd;
get_userdata_t get_userdata;
put_userdata_t put_userdata;
{
	int i;
	udp_fd_t *udp_fd;

	for (i= 0; i<UDP_FD_NR && (udp_fd_table[i].uf_flags & UFF_INUSE);
		i++);

	if (i>= UDP_FD_NR)
	{
#if DEBUG
 { where(); printf("out of fds\n"); }
#endif
		return EOUTOFBUFS;
	}

	udp_fd= &udp_fd_table[i];

	udp_fd->uf_flags= UFF_INUSE;
	udp_fd->uf_port= &udp_port_table[port];
	udp_fd->uf_srfd= srfd;
	udp_fd->uf_udpopt.nwuo_flags= UDP_DEF_OPT;
	udp_fd->uf_get_userdata= get_userdata;
	udp_fd->uf_put_userdata= put_userdata;
	udp_fd->uf_pack= 0;

	return i;

}

PRIVATE acc_t *udp_get_data (port, offset, count, for_ioctl)
int port;
size_t offset;
size_t count;
int for_ioctl;
{
	udp_port_t *udp_port;
	udp_fd_t *udp_fd;
	int result;

	udp_port= &udp_port_table[port];

	switch(udp_port->up_state)
	{
	case UPS_SETPROTO:
assert (for_ioctl);
		if (!count)
		{
			result= (int)offset;
			if (result<0)
			{
				udp_port->up_state= UPS_ERROR;
				break;
			}
			udp_port->up_state= UPS_GETCONF;
			if (udp_port->up_flags & UPF_SUSPEND)
				udp_main(udp_port);
			return NULL;
		}
		else
		{
			struct nwio_ipopt *ipopt;
			acc_t *acc;

assert (!offset);
assert (count == sizeof(*ipopt));

			acc= bf_memreq(sizeof(*ipopt));
			ipopt= (struct nwio_ipopt *)ptr2acc_data(acc);
			ipopt->nwio_flags= NWIO_COPY | NWIO_EN_LOC | 
				NWIO_EN_BROAD | NWIO_REMANY | NWIO_PROTOSPEC |
				NWIO_HDR_O_ANY | NWIO_RWDATALL;
			ipopt->nwio_proto= IPPROTO_UDP;
			return acc;
		}
	case UPS_MAIN:
assert (!for_ioctl);
assert (udp_port->up_flags & UPF_WRITE_IP);
		if (!count)
		{
			result= (int)offset;
#if DEBUG & 256
 { where(); printf("result of ip_write is %d\n", result); }
#endif
assert (udp_port->up_wr_pack);
			bf_afree(udp_port->up_wr_pack);
			udp_port->up_wr_pack= 0;
			if (udp_port->up_flags & UPF_WRITE_SP)
			{
				if (udp_port->up_write_fd)
				{
					udp_fd= udp_port->up_write_fd;
					udp_port->up_write_fd= NULL;
					udp_fd->uf_flags &= ~UFF_WRITE_IP;
					reply_thr_get(udp_fd, result, FALSE);
				}
				udp_port->up_flags &= ~(UPF_WRITE_SP | 
					UPF_WRITE_IP);
				if (udp_port->up_flags & UPF_MORE2WRITE)
				{
					udp_restart_write_port(udp_port);
				}
			}
			else
				udp_port->up_flags &= ~UPF_WRITE_IP;
		}
		else
		{
			return bf_cut (udp_port->up_wr_pack, offset, count);
		}
		break;
	default:
		printf("udp_get_data(%d, 0x%x, 0x%x) called but up_state= 0x%x\n",
			port, offset, count, udp_port->up_state);
		break;
	}
	return NULL;
}

PRIVATE int udp_put_data (fd, offset, data, for_ioctl)
int fd;
size_t offset;
acc_t *data;
int for_ioctl;
{
	udp_port_t *udp_port;
	int result;

	udp_port= &udp_port_table[fd];

	switch (udp_port->up_state)
	{
	case UPS_GETCONF:
		if (!data)
		{
			result= (int)offset;
			if (result<0)
			{
				udp_port->up_state= UPS_ERROR;
				return NW_OK;
			}
			udp_port->up_state= UPS_MAIN;
			if (udp_port->up_flags & UPF_SUSPEND)
				udp_main(udp_port);
		}
		else
		{
			struct nwio_ipconf *ipconf;

			data= bf_packIffLess(data, sizeof(*ipconf));
			ipconf= (struct nwio_ipconf *)ptr2acc_data(data);
assert (ipconf->nwic_flags & NWIC_IPADDR_SET);
			udp_port->up_ipaddr= ipconf->nwic_ipaddr;
			bf_afree(data);
		}
		break;
	case UPS_MAIN:
assert (udp_port->up_flags & UPF_READ_IP);
		if (!data)
		{
			result= (int)offset;
compare (result, >=, 0);
			if (udp_port->up_flags & UPF_READ_SP)
			{
				udp_port->up_flags &= ~(UPF_READ_SP|
					UPF_READ_IP);
				read_ip_packets(udp_port);
			}
			else
				udp_port->up_flags &= ~UPF_READ_IP;
		}
		else
		{
assert (!offset);	/* This isn't a valid assertion but ip sends only
			 * whole datagrams up */
			process_inc_fragm(udp_port, data);
		}
		break;
	default:
		ip_panic((
		"udp_put_data(%d, 0x%x, 0x%x) called but up_state= 0x%x\n",
					fd, offset, data, udp_port->up_state ));
	}
	return NW_OK;
}

int udp_ioctl (fd, req)
int fd;
int req;
{
	udp_fd_t *udp_fd;
	udp_port_t *udp_port;
	nwio_udpopt_t *udp_opt;
	acc_t *opt_acc;
	int type;
	int result;

	udp_fd= &udp_fd_table[fd];
	type= req & IOCTYPE_MASK;

assert (udp_fd->uf_flags & UFF_INUSE);

	udp_port= udp_fd->uf_port;
	udp_fd->uf_flags |= UFF_IOCTL_IP;
	udp_fd->uf_ioreq= req;

	if (udp_port->up_state != UPS_MAIN)
		return NW_SUSPEND;

	switch(type)
	{
	case NWIOSUDPOPT & IOCTYPE_MASK:
		if (req != NWIOSUDPOPT)
		{
			reply_thr_get (udp_fd, EBADIOCTL, TRUE);
			result= NW_OK;
			break;
		}
		result= udp_setopt(udp_fd);
		break;
	case NWIOGUDPOPT & IOCTYPE_MASK:
		if (req != NWIOGUDPOPT)
		{
			reply_thr_put(udp_fd, EBADIOCTL, TRUE);
			result= NW_OK;
			break;
		}
		opt_acc= bf_memreq(sizeof(*udp_opt));
assert (opt_acc->acc_length == sizeof(*udp_opt));
		udp_opt= (nwio_udpopt_t *)ptr2acc_data(opt_acc);

		*udp_opt= udp_fd->uf_udpopt;
		udp_opt->nwuo_locaddr= udp_fd->uf_port->up_ipaddr;
		result= (*udp_fd->uf_put_userdata)(udp_fd->uf_srfd, 0, opt_acc,
			TRUE);
		if (result == NW_OK)
			reply_thr_put(udp_fd, NW_OK, TRUE);
		break;
	default:
		reply_thr_get(udp_fd, EBADIOCTL, TRUE);
		result= NW_OK;
		break;
	}
	if (result != NW_SUSPEND)
		udp_fd->uf_flags &= ~UFF_IOCTL_IP;
	return result;
}

PRIVATE int udp_setopt(udp_fd)
udp_fd_t *udp_fd;
{
	udp_fd_t *fd_ptr;
	nwio_udpopt_t oldopt, newopt;
	acc_t *data;
	int result;
	udpport_t port;
	unsigned int new_en_flags, new_di_flags, old_en_flags, old_di_flags,
		all_flags, flags;
	unsigned long new_flags;
	int i;

#if DEBUG & 256
 { where(); printf("in udp_setopt\n"); }
#endif
	data= (*udp_fd->uf_get_userdata)(udp_fd->uf_srfd, 0, 
		sizeof(nwio_udpopt_t), TRUE);

	if (!data)
		return EFAULT;

	data= bf_packIffLess(data, sizeof(nwio_udpopt_t));
assert (data->acc_length == sizeof(nwio_udpopt_t));

	newopt= *(nwio_udpopt_t *)ptr2acc_data(data);
	bf_afree(data);
	oldopt= udp_fd->uf_udpopt;
#if DEBUG & 256
 { where(); printf("newopt.nwuo_flags= 0x%x, newopt.nwuo_locport= %d, newopt.nwuo_remport= %d\n",
	newopt.nwuo_flags, ntohs(newopt.nwuo_locport),
	ntohs(newopt.nwuo_remport)); }
#endif

	old_en_flags= oldopt.nwuo_flags & 0xffff;
	old_di_flags= (oldopt.nwuo_flags >> 16) & 0xffff;

	new_en_flags= newopt.nwuo_flags & 0xffff;
	new_di_flags= (newopt.nwuo_flags >> 16) & 0xffff;

	if (new_en_flags & new_di_flags)
	{
#if DEBUG
 { where(); printf("returning EBADMODE\n"); }
#endif
		reply_thr_get(udp_fd, EBADMODE, TRUE);
		return NW_OK;
	}

	/* NWUO_ACC_MASK */
	if (new_di_flags & NWUO_ACC_MASK)
	{
#if DEBUG
 { where(); printf("returning EBADMODE\n"); }
#endif
		reply_thr_get(udp_fd, EBADMODE, TRUE);
		return NW_OK;
		/* access modes can't be disabled */
	}

	if (!(new_en_flags & NWUO_ACC_MASK))
		new_en_flags |= (old_en_flags & NWUO_ACC_MASK);

	/* NWUO_LOCPORT_MASK */
	if (new_di_flags & NWUO_LOCPORT_MASK)
	{
#if DEBUG
 { where(); printf("returning EBADMODE\n"); }
#endif
		reply_thr_get(udp_fd, EBADMODE, TRUE);
		return NW_OK;
		/* the loc ports can't be disabled */
	}
	if (!(new_en_flags & NWUO_LOCPORT_MASK))
	{
		new_en_flags |= (old_en_flags & NWUO_LOCPORT_MASK);
		newopt.nwuo_locport= oldopt.nwuo_locport;
	}
	else if ((new_en_flags & NWUO_LOCPORT_MASK) == NWUO_LP_SEL)
	{
		newopt.nwuo_locport= find_unused_port(udp_fd-udp_fd_table);
	}
	else if ((new_en_flags & NWUO_LOCPORT_MASK) == NWUO_LP_SET)
	{
		if (!newopt.nwuo_locport)
		{
#if DEBUG
 { where(); printf("returning EBADMODE\n"); }
#endif
			reply_thr_get(udp_fd, EBADMODE, TRUE);
			return NW_OK;
		}
	}

	/* NWUO_LOCADDR_MASK */
	if (!((new_en_flags | new_di_flags) & NWUO_LOCADDR_MASK))
	{
		new_en_flags |= (old_en_flags & NWUO_LOCADDR_MASK);
		new_di_flags |= (old_di_flags & NWUO_LOCADDR_MASK);
	}

	/* NWUO_BROAD_MASK */
	if (!((new_en_flags | new_di_flags) & NWUO_BROAD_MASK))
	{
		new_en_flags |= (old_en_flags & NWUO_BROAD_MASK);
		new_di_flags |= (old_di_flags & NWUO_BROAD_MASK);
	}

	/* NWUO_REMPORT_MASK */
	if (!((new_en_flags | new_di_flags) & NWUO_REMPORT_MASK))
	{
		new_en_flags |= (old_en_flags & NWUO_REMPORT_MASK);
		new_di_flags |= (old_di_flags & NWUO_REMPORT_MASK);
		newopt.nwuo_remport= oldopt.nwuo_remport;
	}
#if DEBUG & 256
 { where(); printf("newopt.nwuo_remport= %d\n", ntohs(newopt.nwuo_remport)); }
#endif
	
	/* NWUO_REMADDR_MASK */
	if (!((new_en_flags | new_di_flags) & NWUO_REMADDR_MASK))
	{
		new_en_flags |= (old_en_flags & NWUO_REMADDR_MASK);
		new_di_flags |= (old_di_flags & NWUO_REMADDR_MASK);
		newopt.nwuo_remaddr= oldopt.nwuo_remaddr;
	}

	/* NWUO_RW_MASK */
	if (!((new_en_flags | new_di_flags) & NWUO_RW_MASK))
	{
		new_en_flags |= (old_en_flags & NWUO_RW_MASK);
		new_di_flags |= (old_di_flags & NWUO_RW_MASK);
	}

	/* NWUO_IPOPT_MASK */
	if (!((new_en_flags | new_di_flags) & NWUO_IPOPT_MASK))
	{
		new_en_flags |= (old_en_flags & NWUO_IPOPT_MASK);
		new_di_flags |= (old_di_flags & NWUO_IPOPT_MASK);
	}

	new_flags= ((unsigned long)new_di_flags << 16) | new_en_flags;
	if ((new_flags & NWUO_RWDATONLY) && 
		((new_flags & NWUO_LOCPORT_MASK) == NWUO_LP_ANY || 
		(new_flags & (NWUO_RP_ANY|NWUO_RA_ANY|NWUO_EN_IPOPT))))
	{
#if DEBUG
 { where(); printf("returning EBADMODE\n"); }
#endif
		reply_thr_get(udp_fd, EBADMODE, TRUE);
		return NW_OK;
	}

	/* Let's check the access modes */
	if ((new_flags & NWUO_LOCPORT_MASK) == NWUO_LP_SEL ||
		(new_flags & NWUO_LOCPORT_MASK) == NWUO_LP_SET)
	{
		for (i= 0, fd_ptr= udp_fd_table; i<UDP_FD_NR; i++, fd_ptr++)
		{
			if (fd_ptr == udp_fd)
				continue;
			if (!(fd_ptr->uf_flags & UFF_INUSE))
				continue;
			flags= fd_ptr->uf_udpopt.nwuo_flags;
			if ((flags & NWUO_LOCPORT_MASK) != NWUO_LP_SEL &&
				(flags & NWUO_LOCPORT_MASK) != NWUO_LP_SET)
				continue;
			if (fd_ptr->uf_udpopt.nwuo_locport !=
				newopt.nwuo_locport)
				continue;
			if ((flags & NWUO_ACC_MASK) != 
				(new_flags & NWUO_ACC_MASK))
			{
#if DEBUG
 { where(); printf("address inuse: new fd= %d, old_fd= %d, port= %u\n",
	udp_fd-udp_fd_table, fd_ptr-udp_fd_table, newopt.nwuo_locport); }
#endif
				reply_thr_get(udp_fd, EADDRINUSE, TRUE);
				return NW_OK;
			}
		}
	}

	newopt.nwuo_flags= new_flags;
	udp_fd->uf_udpopt= newopt;

	all_flags= new_en_flags | new_di_flags;
#if DEBUG & 256
 { where();
	printf("NWUO_ACC_MASK: %s set\n", all_flags & NWUO_ACC_MASK ? "" : "not"); 
	printf("NWUO_LOCADDR_MASK: %s set\n", all_flags & NWUO_LOCADDR_MASK ? "" : "not"); 
	printf("NWUO_BROAD_MASK: %s set\n", all_flags & NWUO_BROAD_MASK ? "" : "not"); 
	printf("NWUO_REMPORT_MASK: %s set\n", all_flags & NWUO_REMPORT_MASK ? "" : "not"); 
	printf("NWUO_REMADDR_MASK: %s set\n", all_flags & NWUO_REMADDR_MASK ? "" : "not"); 
	printf("NWUO_RW_MASK: %s set\n", all_flags & NWUO_RW_MASK ? "" : "not"); 
	printf("NWUO_IPOPT_MASK: %s set\n", all_flags & NWUO_IPOPT_MASK ? "" : "not"); 
 }
#endif
	if ((all_flags & NWUO_ACC_MASK) && (all_flags & NWUO_LOCPORT_MASK) &&
		(all_flags & NWUO_LOCADDR_MASK) &&
		(all_flags & NWUO_BROAD_MASK) &&
		(all_flags & NWUO_REMPORT_MASK) &&
		(all_flags & NWUO_REMADDR_MASK) &&
		(all_flags & NWUO_RW_MASK) &&
		(all_flags & NWUO_IPOPT_MASK))
		udp_fd->uf_flags |= UFF_OPTSET;
	else
	{
		udp_fd->uf_flags &= ~UFF_OPTSET;
	}

	reply_thr_get(udp_fd, NW_OK, TRUE);
	return NW_OK;
}

PRIVATE udpport_t find_unused_port(fd)
int fd;
{
	udpport_t port, nw_port;

	for (port= 0x8000; port < 0xffff-UDP_FD_NR; port+= UDP_FD_NR)
	{
		nw_port= htons(port);
		if (is_unused_port(nw_port))
			return nw_port;
	}
	for (port= 0x8000; port < 0xffff; port++)
	{
		nw_port= htons(port);
		if (is_unused_port(nw_port))
			return nw_port;
	}
	ip_panic(( "unable to find unused port (shouldn't occur)" ));
	return 0;
}

/*
reply_thr_put
*/

PRIVATE int reply_thr_put(udp_fd, reply, for_ioctl)
udp_fd_t *udp_fd;
int reply;
int for_ioctl;
{
#if DEBUG
 { where(); printf("reply_thr_put(&udp_fd_table[%d], %d, %d) called\n",
	udp_fd-udp_fd_table, reply, for_ioctl); }
#endif
#if DEBUG & 2
 { where(); printf("calling 0x%x\n", udp_fd->uf_put_userdata); }
#endif
assert (udp_fd);
	return (*udp_fd->uf_put_userdata)(udp_fd->uf_srfd, reply,
		(acc_t *)0, for_ioctl);
}

/*
reply_thr_get
*/

PRIVATE void reply_thr_get(udp_fd, reply, for_ioctl)
udp_fd_t *udp_fd;
int reply;
int for_ioctl;
{
	acc_t *result;
#if DEBUG & 256
 { where(); printf("reply_thr_get(&udp_fd_table[%d], %d, %d) called\n",
	udp_fd-udp_fd_table, reply, for_ioctl); }
#endif
	result= (*udp_fd->uf_get_userdata)(udp_fd->uf_srfd, reply,
		(size_t)0, for_ioctl);
	assert (!result);
}

PRIVATE int is_unused_port(port)
udpport_t port;
{
	int i;
	udp_fd_t *udp_fd;

	for (i= 0, udp_fd= udp_fd_table; i<UDP_FD_NR; i++,
		udp_fd++)
	{
		if (!(udp_fd->uf_flags & UFF_OPTSET))
			continue;
		if (udp_fd->uf_udpopt.nwuo_locport == port)
			return FALSE;
	}
	return TRUE;
}

PRIVATE void read_ip_packets(udp_port)
udp_port_t *udp_port;
{
	int result;

	do
	{
		udp_port->up_flags |= UPF_READ_IP;
#if DEBUG & 256
 { where(); printf("doing ip_read\n"); }
#endif
		result= ip_read(udp_port->up_ipfd, UDP_MAX_DATAGRAM);
		if (result == NW_SUSPEND)
		{
			udp_port->up_flags |= UPF_READ_SP;
			return;
		}
assert(result == NW_OK);
		udp_port->up_flags &= ~UPF_READ_IP;
	} while(!(udp_port->up_flags & UPF_READ_IP));
}


PUBLIC int udp_read (fd, count)
int fd;
size_t count;
{
	udp_fd_t *udp_fd;

	udp_fd= &udp_fd_table[fd];
	if (!(udp_fd->uf_flags & UFF_OPTSET))
		return reply_thr_put(udp_fd, EBADMODE, FALSE);

	udp_fd->uf_rd_count= count;

	if (udp_fd->uf_rd_buf)
	{
		if (get_time() <= udp_fd->uf_exp_tim)
			return udp_packet2user (udp_fd);
		bf_afree(udp_fd->uf_rd_buf);
		udp_fd->uf_rd_buf= 0;
	}
	udp_fd->uf_flags |= UFF_READ_IP;
#if DEBUG & 256
 { where(); printf("udp_fd_table[%d].uf_flags= 0x%x\n",
	udp_fd-udp_fd_table, udp_fd->uf_flags); }
#endif
	return NW_SUSPEND;
}

PRIVATE int udp_packet2user (udp_fd)
udp_fd_t *udp_fd;
{
	acc_t *pack, *tmp_pack;
	udp_io_hdr_t *hdr;
	int result, hdr_len;
	size_t size, transf_size;

	pack= udp_fd->uf_rd_buf;
	udp_fd->uf_rd_buf= 0;

	size= bf_bufsize (pack);

	if (udp_fd->uf_udpopt.nwuo_flags & NWUO_RWDATONLY)
	{

		pack= bf_packIffLess (pack, UDP_IO_HDR_SIZE);
assert (pack->acc_length >= UDP_IO_HDR_SIZE);

		hdr= (udp_io_hdr_t *)ptr2acc_data(pack);
		hdr_len= UDP_IO_HDR_SIZE+hdr->uih_ip_opt_len;

assert (size>= hdr_len);
		size -= hdr_len;
		tmp_pack= bf_cut(pack, hdr_len, size);
		bf_afree(pack);
		pack= tmp_pack;
	}

	if (size>udp_fd->uf_rd_count)
	{
		tmp_pack= bf_cut (pack, 0, udp_fd->uf_rd_count);
		bf_afree(pack);
		pack= tmp_pack;
		transf_size= udp_fd->uf_rd_count;
	}
	else
		transf_size= size;

	result= (*udp_fd->uf_put_userdata)(udp_fd->uf_srfd,
		(size_t)0, pack, FALSE);

	if (result >= 0)
		if (size > transf_size)
			result= EPACKSIZE;
		else
			result= transf_size;

	udp_fd->uf_flags &= ~UFF_READ_IP;
	result= (*udp_fd->uf_put_userdata)(udp_fd->uf_srfd, result,
			(acc_t *)0, FALSE);
assert (result == 0);

	return result;
}

PRIVATE void process_inc_fragm(udp_port, pack)
udp_port_t *udp_port;
acc_t *pack;
{
	udp_fd_t *udp_fd, *share_fd;
	acc_t *ip_acc, *udp_acc, *ipopt_pack, *no_ipopt_pack, *tmp_acc;
	ip_hdr_t *ip_hdr;
	udp_hdr_t *udp_hdr;
	udp_io_hdr_t *udp_io_hdr;
	size_t pack_size, ip_hdr_size;
	size_t udp_size;
	ipaddr_t src_addr, dst_addr;
	u8_t u16[2];
	u16_t chksum;
	unsigned long dst_type, flags;
	time_t  exp_tim;
	udpport_t src_port, dst_port;
	int i;

#if DEBUG & 256
 { where(); printf("in process_inc_fragm\n"); }
#endif
	pack_size= bf_bufsize(pack);

	pack= bf_packIffLess(pack, IP_MIN_HDR_SIZE);
assert (pack->acc_length >= IP_MIN_HDR_SIZE);
	ip_hdr= (ip_hdr_t *)ptr2acc_data(pack);
	ip_hdr_size= (ip_hdr->ih_vers_ihl & IH_IHL_MASK) << 2;
	ip_acc= bf_cut(pack, (size_t)0, ip_hdr_size);

	src_addr= ip_hdr->ih_src;
	dst_addr= ip_hdr->ih_dst;

	udp_acc= bf_cut(pack, ip_hdr_size, pack_size-ip_hdr_size);
	bf_afree(pack);
	pack_size -= ip_hdr_size;
	if (pack_size < UDP_HDR_SIZE)
	{
#if DEBUG
 { where(); printf("packet too small\n"); }
#endif
		bf_afree(ip_acc);
		bf_afree(udp_acc);
		return;
	}
	udp_acc= bf_packIffLess(udp_acc, UDP_HDR_SIZE);
	udp_hdr= (udp_hdr_t *)ptr2acc_data(udp_acc);

	udp_size= ntohs(udp_hdr->uh_length);
	if (udp_size > pack_size)
	{
#if DEBUG
 { where(); printf("packet too large\n"); }
#endif
		bf_afree(ip_acc);
		bf_afree(udp_acc);
		return;
	}
	if (udp_hdr->uh_chksum)
	{
		u16[0]= 0;
		u16[1]= ip_hdr->ih_proto;
		chksum= pack_oneCsum(udp_acc);
		chksum= oneC_sum(chksum, (u16_t *)&src_addr, sizeof(ipaddr_t));
		chksum= oneC_sum(chksum, (u16_t *)&dst_addr, sizeof(ipaddr_t));
		chksum= oneC_sum(chksum, (u16_t *)u16, sizeof(u16));
		chksum= oneC_sum(chksum, (u16_t *)&udp_hdr->uh_length, 
			sizeof(udp_hdr->uh_length));
		if (~chksum & 0xffff)
		{
#if DEBUG 
 { where(); printf("udp chksum error\n"); }
#endif
			bf_afree(ip_acc);
			bf_afree(udp_acc);
			return;
		}
	}
	exp_tim= get_time() + UDP_READ_EXP_TIME;
	src_port= udp_hdr->uh_src_port;
	dst_port= udp_hdr->uh_dst_port;

	if (dst_addr == udp_port->up_ipaddr)
		dst_type= NWUO_EN_LOC;
	else
		dst_type= NWUO_EN_BROAD;

	share_fd= 0;
	ipopt_pack= 0;
	no_ipopt_pack= 0;

	for (i=0, udp_fd=udp_fd_table; i<UDP_FD_NR; i++, udp_fd++)
	{
		if (!(udp_fd->uf_flags & UFF_INUSE))
		{
#if DEBUG & 256
 { where(); printf("%d: not inuse\n", i); }
#endif
			continue;
		}
		if (!(udp_fd->uf_flags & UFF_OPTSET))
		{
#if DEBUG
 { where(); printf("%d: options not set\n", i); }
#endif
			continue;
		}
		flags= udp_fd->uf_udpopt.nwuo_flags;
		if (!(flags & dst_type))
		{
#if DEBUG & 256
 { where(); printf("%d: wrong type\n", i); }
#endif
			continue;
		}
		if ((flags & (NWUO_LP_SEL|NWUO_LP_SET)) &&
			udp_fd->uf_udpopt.nwuo_locport != dst_port)
		{
#if DEBUG & 256
 { where(); printf("%d: wrong loc port, got %d, expected %d\n", i,
	dst_port, udp_fd->uf_udpopt.nwuo_locport); }
#endif
			continue;
		}
		if ((flags & NWUO_RP_SET) && 
			udp_fd->uf_udpopt.nwuo_remport != src_port)
		{
#if DEBUG
 { where(); printf("%d: wrong rem port, I got %d, expected %d\n", i,
	ntohs(src_port), ntohs(udp_fd->uf_udpopt.nwuo_remport)); }
#endif
			continue;
		}
		if ((flags & NWUO_RA_SET) && 
			udp_fd->uf_udpopt.nwuo_remaddr != src_addr)
		{
#if DEBUG
 { where(); printf("%d: wrong rem addr\n", i); }
#endif
			continue;
		}

		if (!no_ipopt_pack)
		{
			no_ipopt_pack= bf_memreq(UDP_IO_HDR_SIZE);
			udp_io_hdr= (udp_io_hdr_t *)ptr2acc_data(no_ipopt_pack);
			udp_io_hdr->uih_src_addr= src_addr;
			udp_io_hdr->uih_dst_addr= dst_addr;
			udp_io_hdr->uih_src_port= src_port;
			udp_io_hdr->uih_dst_port= dst_port;
			udp_io_hdr->uih_ip_opt_len= 0;
			udp_io_hdr->uih_data_len= udp_size-UDP_HDR_SIZE;
			no_ipopt_pack->acc_next= bf_cut(udp_acc, 
				UDP_HDR_SIZE, udp_io_hdr->uih_data_len);
			if (ip_hdr_size == IP_MIN_HDR_SIZE)
			{
				ipopt_pack= no_ipopt_pack;
				ipopt_pack->acc_linkC++;
			}
			else
				ipopt_pack= 0;
		}
		if (flags & NWUO_EN_IPOPT)
		{
			if (!ipopt_pack)
			{
				ipopt_pack= bf_memreq(UDP_IO_HDR_SIZE);
				*(udp_io_hdr_t *)ptr2acc_data(ipopt_pack)=
					*udp_io_hdr;
				udp_io_hdr= (udp_io_hdr_t *)
					ptr2acc_data(ipopt_pack);
				udp_io_hdr->uih_ip_opt_len= ip_hdr_size - 
					IP_MIN_HDR_SIZE;
				ipopt_pack->acc_next= bf_cut(ip_acc,
					(size_t)IP_MIN_HDR_SIZE,
					(size_t)udp_io_hdr->uih_ip_opt_len);
				for (tmp_acc= ipopt_pack; tmp_acc->acc_next;
					tmp_acc= tmp_acc->acc_next);
assert (tmp_acc->acc_linkC == 1);
				tmp_acc->acc_next= no_ipopt_pack->acc_next;
				if (tmp_acc->acc_next)
					tmp_acc->acc_next->acc_linkC++;
			}
			pack= ipopt_pack;
		}
		else
			pack= no_ipopt_pack;

		if (udp_fd->uf_rd_buf)
		{
			if ((flags & NWUO_ACC_MASK) == NWUO_SHARED)
			{
				share_fd= udp_fd;
				continue;
			}
#if DEBUG
 { where(); printf("throwing away packet\n"); }
#endif
			bf_afree(udp_fd->uf_rd_buf);
		}

		udp_fd->uf_rd_buf= pack;
		pack->acc_linkC++;
		udp_fd->uf_exp_tim= exp_tim;

		if ((flags & NWUO_ACC_MASK) == NWUO_SHARED ||
			(flags & NWUO_ACC_MASK) ==  NWUO_EXCL)
		{
			if (ipopt_pack)
			{
				bf_afree(ipopt_pack);
				ipopt_pack= 0;
			}
assert(no_ipopt_pack);
			bf_afree(no_ipopt_pack);
			no_ipopt_pack= 0;
		}

		if (udp_fd->uf_flags & UFF_READ_IP)
		{
#if DEBUG & 256
 { where(); printf("%d calling packet2user\n", i); }
#endif
			udp_packet2user(udp_fd);
		}
		else
		{
#if DEBUG & 256
 { where(); printf("%d not READ_IP\n", i); }
#endif
		}
		if ((flags & NWUO_ACC_MASK) == NWUO_SHARED ||
			(flags & NWUO_ACC_MASK) ==  NWUO_EXCL)
		{
			break;
		}
	}
	if (share_fd && no_ipopt_pack)
	{
		bf_afree(share_fd->uf_rd_buf);
		if (share_fd->uf_udpopt.nwuo_flags & NWUO_EN_IPOPT)
			pack= ipopt_pack;
		else
			pack= no_ipopt_pack;
		pack->acc_linkC++;
		share_fd->uf_rd_buf= pack;
		share_fd->uf_exp_tim= exp_tim;
		if (ipopt_pack)
		{
			bf_afree(ipopt_pack);
			ipopt_pack= 0;
		}
assert (no_ipopt_pack);
		bf_afree(no_ipopt_pack);
		no_ipopt_pack= 0;
	}
	else
	{
		if (ipopt_pack)
			bf_afree(ipopt_pack);
		if (no_ipopt_pack)
			bf_afree(no_ipopt_pack);
	}
assert (ip_acc);
	bf_afree(ip_acc);
assert (udp_acc);
	bf_afree(udp_acc);
}

PUBLIC void udp_close(fd)
int fd;
{
	udp_fd_t *udp_fd;

#if DEBUG
 { where(); printf("udp_close (%d)\n", fd); }
#endif
	udp_fd= &udp_fd_table[fd];

	assert (udp_fd->uf_flags & UFF_INUSE);

	udp_fd->uf_flags= UFF_EMPTY;
	if (udp_fd->uf_rd_buf)
	{
		bf_afree(udp_fd->uf_rd_buf);
		udp_fd->uf_rd_buf= 0;
	}
}

PUBLIC int udp_write(fd, count)
int fd;
size_t count;
{
	udp_fd_t *udp_fd;
	udp_port_t *udp_port;

	udp_fd= &udp_fd_table[fd];
	udp_port= udp_fd->uf_port;

	if (!(udp_fd->uf_flags & UFF_OPTSET))
	{
		reply_thr_get (udp_fd, EBADMODE, FALSE);
		return NW_OK;
	}

assert (!(udp_fd->uf_flags & UFF_WRITE_IP));

	udp_fd->uf_wr_count= count;

	udp_fd->uf_flags |= UFF_WRITE_IP;

	restart_write_fd(udp_fd);

	if (udp_fd->uf_flags & UFF_WRITE_IP)
	{
#if DEBUG 
 { where(); printf("replying NW_SUSPEND\n"); }
#endif
		return NW_SUSPEND;
	}
	else
	{
#if DEBUG & 256
 { where(); printf("replying NW_OK\n"); }
#endif
		return NW_OK;
	}
}

PRIVATE void restart_write_fd(udp_fd)
udp_fd_t *udp_fd;
{
	udp_port_t *udp_port;
	acc_t *pack, *ip_hdr_pack, *udp_hdr_pack, *ip_opt_pack, *user_data;
	udp_hdr_t *udp_hdr;
	udp_io_hdr_t *udp_io_hdr;
	ip_hdr_t *ip_hdr;
	size_t ip_opt_size, user_data_size;
	unsigned long flags;
	u16_t chksum;
	u8_t u16[2];
	int result;

	udp_port= udp_fd->uf_port;

	if (udp_port->up_flags & UPF_WRITE_IP)
	{
		udp_port->up_flags |= UPF_MORE2WRITE;
#if DEBUG
 { where(); printf("\n"); }
#endif
		return;
	}

assert (udp_fd->uf_flags & UFF_WRITE_IP);
	udp_fd->uf_flags &= ~UFF_WRITE_IP;

assert (!udp_port->up_wr_pack);

	pack= (*udp_fd->uf_get_userdata)(udp_fd->uf_srfd, 0,
		udp_fd->uf_wr_count, FALSE);
	if (!pack)
	{
		udp_fd->uf_flags &= ~UFF_WRITE_IP;
		reply_thr_get (udp_fd, EFAULT, FALSE);
#if DEBUG
 { where(); printf("\n"); }
#endif
		return;
	}

	flags= udp_fd->uf_udpopt.nwuo_flags;

	ip_hdr_pack= bf_memreq(IP_MIN_HDR_SIZE);
	ip_hdr= (ip_hdr_t *)ptr2acc_data(ip_hdr_pack);

	udp_hdr_pack= bf_memreq(UDP_HDR_SIZE);
	udp_hdr= (udp_hdr_t *)ptr2acc_data(udp_hdr_pack);

	if (flags & NWUO_RWDATALL)
	{
		pack= bf_packIffLess(pack, UDP_IO_HDR_SIZE);
		udp_io_hdr= (udp_io_hdr_t *)ptr2acc_data(pack);
		ip_opt_size= udp_io_hdr->uih_ip_opt_len;
		user_data_size= udp_io_hdr->uih_data_len;
		if (UDP_IO_HDR_SIZE+ip_opt_size>udp_fd->uf_wr_count)
		{
			bf_afree(ip_hdr_pack);
			bf_afree(udp_hdr_pack);
			bf_afree(pack);
			reply_thr_get (udp_fd, EINVAL, FALSE);
#if DEBUG
 { where(); printf("\n"); }
#endif
			return;
		}
		if (ip_opt_size & 3)
		{
			bf_afree(ip_hdr_pack);
			bf_afree(udp_hdr_pack);
			bf_afree(pack);
			reply_thr_get (udp_fd, EFAULT, FALSE);
#if DEBUG
 { where(); printf("\n"); }
#endif
			return;
		}
		if (ip_opt_size)
			ip_opt_pack= bf_cut(pack, UDP_IO_HDR_SIZE, ip_opt_size);
		else
			ip_opt_pack= 0;
		user_data_size= udp_fd->uf_wr_count-UDP_IO_HDR_SIZE-
			ip_opt_size;
		user_data= bf_cut(pack, UDP_IO_HDR_SIZE+ip_opt_size, 
			user_data_size);
		bf_afree(pack);
	}
	else
	{
		udp_io_hdr= 0;
		ip_opt_size= 0;
		user_data_size= udp_fd->uf_wr_count;
		ip_opt_pack= 0;
		user_data= pack;
	}

	ip_hdr->ih_vers_ihl= (IP_MIN_HDR_SIZE+ip_opt_size) >> 2;
	ip_hdr->ih_tos= UDP_TOS;
	ip_hdr->ih_flags_fragoff= HTONS(UDP_IP_FLAGS);
	ip_hdr->ih_ttl= UDP_TTL;
	ip_hdr->ih_proto= IPPROTO_UDP;
	if (flags & NWUO_RA_SET)
	{
#if DEBUG
 { where(); printf("NWUO_RA_SET\n"); }
#endif
		ip_hdr->ih_dst= udp_fd->uf_udpopt.nwuo_remaddr;
	}
	else
	{
assert (udp_io_hdr);
		ip_hdr->ih_dst= udp_io_hdr->uih_dst_addr;
	}
#if DEBUG & 256
 { where(); printf("ih_dst= "); writeIpAddr(ip_hdr->ih_dst); printf("\n"); }
#endif

	if ((flags & NWUO_LOCPORT_MASK) != NWUO_LP_ANY)
		udp_hdr->uh_src_port= udp_fd->uf_udpopt.nwuo_locport;
	else
	{
assert (udp_io_hdr);
		udp_hdr->uh_src_port= udp_io_hdr->uih_src_port;
	}

	if (flags & NWUO_RP_SET)
		udp_hdr->uh_dst_port= udp_fd->uf_udpopt.nwuo_remport;
	else
	{
assert (udp_io_hdr);
		udp_hdr->uh_dst_port= udp_io_hdr->uih_dst_port;
	}

	udp_hdr->uh_length= htons(UDP_HDR_SIZE+user_data_size);
	udp_hdr->uh_chksum= 0;

	udp_hdr_pack->acc_next= user_data;
	chksum= pack_oneCsum(udp_hdr_pack);
	chksum= oneC_sum(chksum, (u16_t *)&udp_fd->uf_port->up_ipaddr,
		sizeof(ipaddr_t));
	chksum= oneC_sum(chksum, (u16_t *)&ip_hdr->ih_dst, sizeof(ipaddr_t));
	u16[0]= 0;
	u16[1]= IPPROTO_UDP;
	chksum= oneC_sum(chksum, (u16_t *)u16, sizeof(u16));
	chksum= oneC_sum(chksum, (u16_t *)&udp_hdr->uh_length, sizeof(u16_t));
	if (~chksum)
		chksum= ~chksum;
	udp_hdr->uh_chksum= chksum;
	
	if (ip_opt_pack)
	{
		ip_opt_pack= bf_packIffLess(ip_opt_pack, ip_opt_size);
		ip_opt_pack->acc_next= udp_hdr_pack;
		udp_hdr_pack= ip_opt_pack;
	}
	ip_hdr_pack->acc_next= udp_hdr_pack;

assert (!udp_port->up_wr_pack);
assert (!(udp_port->up_flags & UPF_WRITE_IP));

	udp_port->up_wr_pack= ip_hdr_pack;
	udp_port->up_flags |= UPF_WRITE_IP;
#if DEBUG & 256
 { where(); printf("calling ip_write(%d, %d)\n", udp_port->up_ipfd,
	bf_bufsize(ip_hdr_pack)); }
#endif
	result= ip_write(udp_port->up_ipfd, bf_bufsize(ip_hdr_pack));
#if DEBUG & 256
 { where(); printf("ip_write done\n"); }
#endif
	if (result == NW_SUSPEND)
	{
		udp_port->up_flags |= UPF_WRITE_SP;
		udp_fd->uf_flags |= UFF_WRITE_IP;
		udp_port->up_write_fd= udp_fd;
	}
	else if (result<0)
		reply_thr_get(udp_fd, result, FALSE);
	else
		reply_thr_get (udp_fd, udp_fd->uf_wr_count, FALSE);
#if DEBUG & 256
 { where(); printf("\n"); }
#endif
}

PRIVATE u16_t pack_oneCsum(pack)
acc_t *pack;
{
	u16_t prev;
	int odd_byte;
	char *data_ptr;
	int length;
	char byte_buf[2];

	assert (pack);

	prev= 0;

	odd_byte= FALSE;
	for (; pack; pack= pack->acc_next)
	{
		
		data_ptr= ptr2acc_data(pack);
		length= pack->acc_length;

		if (!length)
			continue;
		if (odd_byte)
		{
			byte_buf[1]= *data_ptr;
			prev= oneC_sum(prev, (u16_t *)byte_buf, 2);
			data_ptr++;
			length--;
			odd_byte= FALSE;
		}
		if (length & 1)
		{
			odd_byte= TRUE;
			length--;
			byte_buf[0]= data_ptr[length];
		}
		if (!length)
			continue;
		prev= oneC_sum (prev, (u16_t *)data_ptr, length);
	}
	if (odd_byte)
		prev= oneC_sum (prev, (u16_t *)byte_buf, 1);
	return prev;
}

PRIVATE void udp_restart_write_port(udp_port )
udp_port_t *udp_port;
{
	udp_fd_t *udp_fd;
	int i;

assert (!udp_port->up_wr_pack);
assert (!(udp_port->up_flags & (UPF_WRITE_IP|UPF_WRITE_SP)));

	while (udp_port->up_flags & UPF_MORE2WRITE)
	{
		udp_port->up_flags &= ~UPF_MORE2WRITE;

		for (i= 0, udp_fd= udp_port->up_next_fd; i<UDP_FD_NR;
			i++, udp_fd++)
		{
			if (udp_fd == &udp_fd_table[UDP_FD_NR])
				udp_fd= udp_fd_table;

			if (!(udp_fd->uf_flags & UFF_INUSE))
				continue;
			if (!(udp_fd->uf_flags & UFF_WRITE_IP))
				continue;
			if (udp_fd->uf_port != udp_port)
				continue;
			restart_write_fd(udp_fd);
			if (udp_port->up_flags & UPF_WRITE_IP)
			{
				udp_port->up_next_fd= udp_fd+1;
				udp_port->up_flags |= UPF_MORE2WRITE;
				return;
			}
		}
	}
}

PUBLIC int udp_cancel(fd, which_operation)
int fd;
int which_operation;
{
	udp_fd_t *udp_fd;

#if DEBUG
 { where(); printf("udp_cancel(%d, %d)\n", fd, which_operation); }
#endif
	udp_fd= &udp_fd_table[fd];

	switch (which_operation)
	{
	case SR_CANCEL_READ:
assert (udp_fd->uf_flags & UFF_READ_IP);
		udp_fd->uf_flags &= ~UFF_READ_IP;
		reply_thr_put(udp_fd, EINTR, FALSE);
		break;
	case SR_CANCEL_WRITE:
assert (udp_fd->uf_flags & UFF_WRITE_IP);
		udp_fd->uf_flags &= ~UFF_WRITE_IP;
		if (udp_fd->uf_port->up_write_fd == udp_fd)
			udp_fd->uf_port->up_write_fd= NULL;
		reply_thr_get(udp_fd, EINTR, FALSE);
		break;
	case SR_CANCEL_IOCTL:
assert (udp_fd->uf_flags & UFF_IOCTL_IP);
		udp_fd->uf_flags &= ~UFF_IOCTL_IP;
		reply_thr_get(udp_fd, EINTR, TRUE);
		break;
	default:
		ip_panic(( "got unknown cancel request" ));
	}
	return NW_OK;
}

PRIVATE void udp_buffree (priority, reqsize)
int priority;
size_t reqsize;
{
	int i;
	time_t curr_tim;


	if (priority <UDP_PRI_EXP_FDBUFS)
		return;

	curr_tim= get_time();
	for (i=0; i<UDP_FD_NR; i++)
	{
		if (!(udp_fd_table[i].uf_flags & UFF_INUSE) )
			continue;
		if (udp_fd_table[i].uf_rd_buf &&
			udp_fd_table[i].uf_exp_tim < curr_tim)
		{
			bf_afree(udp_fd_table[i].uf_rd_buf);
			udp_fd_table[i].uf_rd_buf= 0;
			if (bf_free_buffsize >= reqsize)
				return;
		}
	}

	if (priority <UDP_PRI_FDBUFS)
		return;

	for (i=0; i<UDP_FD_NR; i++)
	{
		if (!(udp_fd_table[i].uf_flags & UFF_INUSE))
			continue;
		if (udp_fd_table[i].uf_rd_buf)
		{
			bf_afree(udp_fd_table[i].uf_rd_buf);
			udp_fd_table[i].uf_rd_buf= 0;
			if (bf_free_buffsize >= reqsize)
				return;
		}
	}
}
