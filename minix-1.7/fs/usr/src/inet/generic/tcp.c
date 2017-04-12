/*
tcp.c
*/

#include "inet.h"
#include "buf.h"
#include "clock.h"
#include "type.h"

#include "io.h"
#include "ip.h"
#include "sr.h"
#include "assert.h"
#include "tcp.h"
#include "tcp_int.h"

INIT_PANIC();

PUBLIC tcp_port_t tcp_port_table[TCP_PORT_NR];
PUBLIC tcp_fd_t tcp_fd_table[TCP_FD_NR];
PUBLIC tcp_conn_t tcp_conn_table[TCP_CONN_NR];

FORWARD void tcp_main ARGS(( tcp_port_t *port ));
FORWARD acc_t *tcp_get_data ARGS(( int fd, size_t offset,
	size_t count, int for_ioctl ));
FORWARD int tcp_put_data ARGS(( int fd, size_t offset,
	acc_t *data, int for_ioctl ));
FORWARD void read_ip_packets ARGS(( tcp_port_t *port ));
FORWARD int tcp_setconf ARGS(( tcp_fd_t *tcp_fd ));
FORWARD int tcp_connect ARGS(( tcp_fd_t *tcp_fd ));
FORWARD int tcp_listen ARGS(( tcp_fd_t *tcp_fd ));
FORWARD int tcp_attache ARGS(( tcp_fd_t *tcp_fd ));
FORWARD tcpport_t find_unused_port ARGS(( int fd ));
FORWARD int is_unused_port ARGS(( Tcpport_t port ));
FORWARD int reply_thr_put ARGS(( tcp_fd_t *tcp_fd, int reply,
	int for_ioctl ));
FORWARD void reply_thr_get ARGS(( tcp_fd_t *tcp_fd, int reply,
	int for_ioctl ));
FORWARD tcp_conn_t *find_conn_entry ARGS(( Tcpport_t locport,
	ipaddr_t locaddr, Tcpport_t remport, ipaddr_t readaddr ));
FORWARD tcp_conn_t *find_empty_conn ARGS(( void ));
FORWARD void process_inc_fragm ARGS(( tcp_port_t *tcp_port,
	acc_t *data ));
FORWARD tcp_conn_t *find_best_conn ARGS(( ip_hdr_t *ip_hdr, 
	tcp_hdr_t *tcp_hdr ));
FORWARD void close_mainuser ARGS(( tcp_conn_t *tcp_conn,
	tcp_fd_t *tcp_fd ));
FORWARD int conn_right4fd ARGS(( tcp_conn_t *tcp_conn, tcp_fd_t *tcp_fd ));
FORWARD int tcp_su4connect ARGS(( tcp_fd_t *tcp_fd ));
FORWARD void tcp_buffree ARGS(( int priority, size_t reqsize ));
FORWARD void tcp_notreach ARGS(( acc_t *pack ));
FORWARD void tcp_setup_conn ARGS(( tcp_conn_t *tcp_conn ));

PUBLIC void tcp_init()
{
	int i, result;
	tcp_fd_t *tcp_fd;
	tcp_port_t *tcp_port;
	tcp_conn_t *tcp_conn;

	assert (BUF_S >= sizeof(struct nwio_ipopt));
	assert (BUF_S >= sizeof(struct nwio_ipconf));
	assert (BUF_S >= sizeof(struct nwio_tcpconf));
	assert (BUF_S >= IP_MAX_HDR_SIZE);
	assert (BUF_S >= TCP_MAX_HDR_SIZE);

	tcp_port_table[0].tp_minor= TCP_DEV0;
	tcp_port_table[0].tp_ipdev= IP0;

	for (i=0, tcp_fd= tcp_fd_table; i<TCP_FD_NR; i++, tcp_fd++)
	{
		tcp_fd->tf_flags= TFF_EMPTY;
	}

	for (i=0, tcp_conn= tcp_conn_table; i<TCP_CONN_NR; i++,
		tcp_fd++)
	{
		tcp_conn->tc_flags= TCF_EMPTY;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
	}

	bf_logon(tcp_buffree);

	for (i=0, tcp_port= tcp_port_table; i<TCP_PORT_NR; i++,
		tcp_port++)
	{
		tcp_port->tp_flags= TPF_EMPTY;
		tcp_port->tp_state= TPS_EMPTY;

		result= sr_add_minor (tcp_port->tp_minor,
			tcp_port-tcp_port_table, tcp_open, tcp_close,
			tcp_read, tcp_write, tcp_ioctl, tcp_cancel);
		assert (result>=0);

		tcp_main(tcp_port);
	}
}

PRIVATE void tcp_main(tcp_port)
tcp_port_t *tcp_port;
{
	int result, i;
	tcp_conn_t *tcp_conn;
	tcp_fd_t *tcp_fd;

	switch (tcp_port->tp_state)
	{
	case TPS_EMPTY:
		tcp_port->tp_state= TPS_SETPROTO;
#if DEBUG & 256
 { where(); printf("doing ip_open\n"); }
#endif
		tcp_port->tp_ipfd= ip_open(tcp_port->tp_ipdev,
			tcp_port-tcp_port_table, tcp_get_data,
			tcp_put_data);
		if (tcp_port->tp_ipfd < 0)
		{
			tcp_port->tp_state= TPS_ERROR;
			printf("%s, %d: unable to open ip port\n",
				__FILE__, __LINE__);
			return;
		}

#if DEBUG & 256
 { where(); printf("doing ip_ioctl(.., NWIOSIPOPT)\n"); }
#endif
		result= ip_ioctl(tcp_port->tp_ipfd, NWIOSIPOPT);
		if (result == NW_SUSPEND)
			tcp_port->tp_flags |= TPF_SUSPEND;
		if (result < 0)
		{
#if DEBUG
 { where(); printf("ip_ioctl(..,%lx)=%d\n", NWIOSIPOPT, result); }
#endif
			return;
		}
		if (tcp_port->tp_state != TPS_GETCONF)
			return;
		/* drops through */
	case TPS_GETCONF:
		tcp_port->tp_flags &= ~TPF_SUSPEND;

#if DEBUG & 256
 { where(); printf("doing ip_ioctl(.., NWIOGIPCONF)\n"); }
#endif
		result= ip_ioctl(tcp_port->tp_ipfd, NWIOGIPCONF);
		if (result == NW_SUSPEND)
			tcp_port->tp_flags |= TPF_SUSPEND;
		if (result < 0)
		{
#if DEBUG & 256
 { where(); printf("ip_ioctl(..,%lx)=%d\n", NWIOGIPCONF, result); }
#endif
			return;
		}
		if (tcp_port->tp_state != TPS_MAIN)
			return;
		/* drops through */
	case TPS_MAIN:
		tcp_port->tp_flags &= ~TPF_SUSPEND;
		tcp_port->tp_pack= 0;

		tcp_conn= &tcp_conn_table[tcp_port-tcp_port_table];
		tcp_conn->tc_flags= TCF_INUSE;
#if DEBUG & 16
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
		tcp_conn->tc_locport= 0;
		tcp_conn->tc_locaddr= tcp_port->tp_ipaddr;
		tcp_conn->tc_remport= 0;
		tcp_conn->tc_remaddr= 0;
		tcp_conn->tc_state= TCS_CLOSED;
#if DEBUG & 2
 { where(); tcp_write_state(tcp_conn); }
#endif
		tcp_conn->tc_mainuser= 0;
		tcp_conn->tc_readuser= 0;
		tcp_conn->tc_writeuser= 0;
		tcp_conn->tc_connuser= 0;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_connuser= 0x%x\n", tcp_conn-
	tcp_conn_table, tcp_conn->tc_connuser); }
#endif
		tcp_conn->tc_orglisten= FALSE;
		tcp_conn->tc_senddis= 0;
		tcp_conn->tc_ISS= 0;
		tcp_conn->tc_SND_UNA= tcp_conn->tc_ISS;
		tcp_conn->tc_SND_TRM= tcp_conn->tc_ISS;
		tcp_conn->tc_SND_NXT= tcp_conn->tc_ISS;
		tcp_conn->tc_SND_UP= tcp_conn->tc_ISS;
		tcp_conn->tc_SND_WL2= tcp_conn->tc_ISS;
		tcp_conn->tc_IRS= 0;
		tcp_conn->tc_SND_WL1= tcp_conn->tc_IRS;
		tcp_conn->tc_RCV_LO= tcp_conn->tc_IRS;
		tcp_conn->tc_RCV_NXT= tcp_conn->tc_IRS;
		tcp_conn->tc_RCV_HI= tcp_conn->tc_IRS;
		tcp_conn->tc_RCV_UP= tcp_conn->tc_IRS;
		tcp_conn->tc_port= tcp_port;
		tcp_conn->tc_rcvd_data= 0;
		tcp_conn->tc_rcv_queue= 0;
		tcp_conn->tc_send_data= 0;
		tcp_conn->tc_remipopt= 0;
		tcp_conn->tc_remtcpopt= 0;
		tcp_conn->tc_frag2send= 0;
		tcp_conn->tc_tos= TCP_DEF_TOS;
		tcp_conn->tc_ttl= IP_MAX_TTL;
		tcp_conn->tc_rcv_wnd= TCP_MAX_WND_SIZE;
		tcp_conn->tc_urg_wnd= TCP_DEF_URG_WND;
		tcp_conn->tc_max_no_retrans= TCP_DEF_MAX_NO_RETRANS;
		tcp_conn->tc_0wnd_to= 0;
		tcp_conn->tc_rtt= TCP_DEF_RTT;
		tcp_conn->tc_ett= 0;
		tcp_conn->tc_mss= TCP_DEF_MSS;
		tcp_conn->tc_error= NW_OK;
		tcp_conn->tc_snd_wnd= TCP_MAX_WND_SIZE;

		for (i=0, tcp_fd= tcp_fd_table; i<TCP_FD_NR; i++,
			tcp_fd++)
		{
			if (!(tcp_fd->tf_flags & TFF_INUSE))
				continue;
			if (tcp_fd->tf_port != tcp_port)
				continue;
			if (tcp_fd->tf_flags & TFF_IOC_INIT_SP)
			{
				tcp_fd->tf_flags &= ~TFF_IOC_INIT_SP;
#if DEBUG & 256
 { where(); printf("restarting tcp_ioctl\n"); }
#endif
				tcp_ioctl(i, tcp_fd->tf_ioreq);
			}
		}
		read_ip_packets(tcp_port);
		return;

	default:
		ip_panic(( "unknown state" ));
		break;
	}
}

PRIVATE acc_t *tcp_get_data (port, offset, count, for_ioctl)
int port;
size_t offset;
size_t count;
int for_ioctl;
{
	tcp_port_t *tcp_port;
	int result;

	tcp_port= &tcp_port_table[port];

	switch (tcp_port->tp_state)
	{
	case TPS_SETPROTO:
		if (!count)
		{
			result= (int)offset;
			if (result<0)
			{
				tcp_port->tp_state= TPS_ERROR;
				break;
			}
			tcp_port->tp_state= TPS_GETCONF;
			if (tcp_port->tp_flags & TPF_SUSPEND)
				tcp_main(tcp_port);
			return NW_OK;
		}
assert (!offset);
assert (count == sizeof(struct nwio_ipopt));
		{
			struct nwio_ipopt *ipopt;
			acc_t *acc;

			acc= bf_memreq(sizeof(*ipopt));
			ipopt= (struct nwio_ipopt *)ptr2acc_data(acc);
			ipopt->nwio_flags= NWIO_COPY |
				NWIO_EN_LOC | NWIO_DI_BROAD |
				NWIO_REMANY | NWIO_PROTOSPEC |
				NWIO_HDR_O_ANY | NWIO_RWDATALL;
			ipopt->nwio_proto= IPPROTO_TCP;
			return acc;
		}
	case TPS_MAIN:
		assert(tcp_port->tp_flags & TPF_WRITE_IP);
		if (!count)
		{
			result= (int)offset;
#if DEBUG & 256
 { where(); printf("tcp_get_data: got reply: %d\n", result); }
#endif
			if (result<0)
			{
				if (result == EDSTNOTRCH)
				{
					tcp_notreach(tcp_port->tp_pack);
				}
				else
				{
					ip_warning((
					"ip_write failed with error: %d\n", 
								result ));
				}
			}
			assert (tcp_port->tp_pack);
			bf_afree (tcp_port->tp_pack);
			tcp_port->tp_pack= 0;

			if (tcp_port->tp_flags & TPF_WRITE_SP)
			{
				tcp_port->tp_flags &= ~(TPF_WRITE_SP|
					TPF_WRITE_IP);
				if (tcp_port->tp_flags & TPF_MORE2WRITE)
				{
#if DEBUG & 256
 { where(); printf("calling tcp_restart_write_port(&tcp_port_table[%d])\n",
	tcp_port - tcp_port_table); }
#endif
					tcp_restart_write_port(
						tcp_port);
				}
			}
			else
				tcp_port->tp_flags &= ~TPF_WRITE_IP;
		}
		else
		{
#if DEBUG & 256
 { where(); printf("suplying data, count= %d, offset= %d, bufsize= %d\n",
	count, offset, bf_bufsize(tcp_port->tp_pack)); }
#endif
			return bf_cut (tcp_port->tp_pack, offset,
				count);
		}
		break;
	default:
		printf("tcp_get_data(%d, 0x%x, 0x%x) called but tp_state= 0x%x\n",
			port, offset, count, tcp_port->tp_state);
		break;
	}
	return NW_OK;
}

PRIVATE int tcp_put_data (fd, offset, data, for_ioctl)
int fd;
size_t offset;
acc_t *data;
int for_ioctl;
{
	tcp_port_t *tcp_port;
	int result;

	tcp_port= &tcp_port_table[fd];

	switch (tcp_port->tp_state)
	{
	case TPS_GETCONF:
		if (!data)
		{
			result= (int)offset;
			if (result<0)
			{
				tcp_port->tp_state= TPS_ERROR;
				return NW_OK;
			}
			tcp_port->tp_state= TPS_MAIN;
#if DEBUG & 256
 { where(); printf("get GETCONF reply\n"); }
#endif
			if (tcp_port->tp_flags & TPF_SUSPEND)
				tcp_main(tcp_port);
		}
		else
		{
			struct nwio_ipconf *ipconf;

			data= bf_packIffLess(data, sizeof(*ipconf));
			ipconf= (struct nwio_ipconf *)ptr2acc_data(data);
assert (ipconf->nwic_flags & NWIC_IPADDR_SET);
			tcp_port->tp_ipaddr= ipconf->nwic_ipaddr;
			bf_afree(data);
		}
		break;
	case TPS_MAIN:
		assert(tcp_port->tp_flags & TPF_READ_IP);
		if (!data)
		{
			result= (int)offset;
			if (result<0)
#if DEBUG
 { where(); printf("tcp_put_data got error %d (ignored)\n", result); }
#else
ip_panic(( "ip_read() failed" ));
#endif
			if (tcp_port->tp_flags & TPF_READ_SP)
			{
				tcp_port->tp_flags &= ~(TPF_READ_SP|
					TPF_READ_IP);
				read_ip_packets(tcp_port);
			}
			else
				tcp_port->tp_flags &= ~TPF_READ_IP;
		}
		else
		{
assert(!offset);	 /* this isn't a valid assertion but ip sends
			  * only whole datagrams up */
#if DEBUG & 256
 { where(); printf("got data from ip\n"); }
#endif
			process_inc_fragm(tcp_port, data);
		}
		break;
	default:
		printf("tcp_put_data(%d, 0x%x, 0x%x) called but tp_state= 0x%x\n",
	fd, offset, data, tcp_port->tp_state);
		break;
	}
	return NW_OK;
}

PUBLIC int tcp_open (port, srfd, get_userdata, put_userdata)
int port;
int srfd;
get_userdata_t get_userdata;
put_userdata_t put_userdata;
{
	int i;
	tcp_fd_t *tcp_fd;

	for (i=0; i<TCP_FD_NR && (tcp_fd_table[i].tf_flags & TFF_INUSE);
		i++);
	if (i>=TCP_FD_NR)
	{
#if DEBUG
 { where(); printf("out of fds\n"); }
#endif
		return EOUTOFBUFS;
	}

	tcp_fd= &tcp_fd_table[i];

	tcp_fd->tf_flags= TFF_INUSE;
	tcp_fd->tf_flags |= TFF_PUSH_DATA;	/* XXX */

	tcp_fd->tf_port= &tcp_port_table[port];
	tcp_fd->tf_srfd= srfd;
	tcp_fd->tf_tcpconf.nwtc_flags= TCP_DEF_OPT;
	tcp_fd->tf_tcpconf.nwtc_remaddr= 0;
	tcp_fd->tf_tcpconf.nwtc_remport= 0;
	tcp_fd->tf_get_userdata= get_userdata;
	tcp_fd->tf_put_userdata= put_userdata;
	tcp_fd->tf_conn= 0;
	return i;
}

PUBLIC int tcp_ioctl (fd, req)
int fd;
int req;
{
	tcp_fd_t *tcp_fd;
	tcp_port_t *tcp_port;
	tcp_conn_t *tcp_conn;
	nwio_tcpconf_t *tcp_conf;
	acc_t *conf_acc;
	int type;
	int result;

#if DEBUG & 256
 { where(); printf("tcp_ioctl called\n"); }
#endif
	tcp_fd= &tcp_fd_table[fd];
	type= req & IOCTYPE_MASK;

assert (tcp_fd->tf_flags & TFF_INUSE);

	tcp_port= tcp_fd->tf_port;
	tcp_fd->tf_flags |= TFF_IOCTL_IP;
	tcp_fd->tf_ioreq= req;

	if (tcp_port->tp_state != TPS_MAIN)
	{
		tcp_fd->tf_flags |= TFF_IOC_INIT_SP;
		return NW_SUSPEND;
	}

	switch (type)
	{
	case NWIOSTCPCONF & IOCTYPE_MASK:
		if (req != NWIOSTCPCONF)
		{
#if DEBUG
 { where(); printf("0x%x: bad ioctl\n", req); }
#endif
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get (tcp_fd, EBADIOCTL, TRUE);
			result= NW_OK;
			break;
		}
		if (tcp_fd->tf_flags & TFF_CONNECTED)
		{
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get (tcp_fd, EISCONN, TRUE);
			result= NW_OK;
			break;
		}
		result= tcp_setconf(tcp_fd);
		break;
	case NWIOGTCPCONF & IOCTYPE_MASK:
		if (req != NWIOGTCPCONF)
		{
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_put (tcp_fd, EBADIOCTL, TRUE);
			result= NW_OK;
			break;
		}
		conf_acc= bf_memreq(sizeof(*tcp_conf));
assert (conf_acc->acc_length == sizeof(*tcp_conf));
		tcp_conf= (nwio_tcpconf_t *)ptr2acc_data(conf_acc);

		*tcp_conf= tcp_fd->tf_tcpconf;
		if (tcp_fd->tf_flags & TFF_CONNECTED)
		{
			tcp_conn= tcp_fd->tf_conn;
			tcp_conf->nwtc_locport= tcp_conn->tc_locport;
			tcp_conf->nwtc_remaddr= tcp_conn->tc_remaddr;
			tcp_conf->nwtc_remport= tcp_conn->tc_remport;
		}
		tcp_conf->nwtc_locaddr= tcp_fd->tf_port->tp_ipaddr;
		result= (*tcp_fd->tf_put_userdata)(tcp_fd->tf_srfd,
			0, conf_acc, TRUE);
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_put(tcp_fd, result, TRUE);
		result= NW_OK;
		break;
	case NWIOTCPCONN & IOCTYPE_MASK:
		if (req != NWIOTCPCONN)
		{
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get (tcp_fd, EBADIOCTL, TRUE);
			result= NW_OK;
			break;
		}
		if (tcp_fd->tf_flags & TFF_CONNECTED)
		{
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get (tcp_fd, EISCONN, TRUE);
			result= NW_OK;
			break;
		}
		result= tcp_connect(tcp_fd);
		break;
	case NWIOTCPLISTEN & IOCTYPE_MASK:
		if (req != NWIOTCPLISTEN)
		{
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get (tcp_fd, EBADIOCTL, TRUE);
			result= NW_OK;
			break;
		}
		if (tcp_fd->tf_flags & TFF_CONNECTED)
		{
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get (tcp_fd, EISCONN, TRUE);
			result= NW_OK;
			break;
		}
		result= tcp_listen(tcp_fd);
#if DEBUG & 256
 { where(); printf("tcp_listen= %d\n", result); }
#endif
		break;
#if 0
	case NWIOTCPATTACH & IOCTYPE_MASK:
		if (req != NWIOTCPATTACH)
		{
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get (tcp_fd, EBADIOCTL, TRUE);
			result= NW_OK;
			break;
		}
		if (tcp_fd->tf_flags & TFF_CONNECTED)
		{
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get (tcp_fd, EISCONN, TRUE);
			result= NW_OK;
			break;
		}
		result= tcp_attache(tcp_fd);
		break;
#endif
	case NWIOTCPSHUTDOWN & IOCTYPE_MASK:
		if (req != NWIOTCPSHUTDOWN)
		{
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get (tcp_fd, EBADIOCTL, TRUE);
			result= NW_OK;
			break;
		}
		if (!(tcp_fd->tf_flags & TFF_CONNECTED))
		{
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get (tcp_fd, ENOTCONN, TRUE);
			result= NW_OK;
			break;
		}
		tcp_fd->tf_flags |= TFF_IOCTL_IP;
		tcp_fd->tf_ioreq= req;
		tcp_conn= tcp_fd->tf_conn;
		if (tcp_conn->tc_writeuser)
			return NW_SUSPEND;

		tcp_conn->tc_writeuser= tcp_fd;
		tcp_restart_fd_write (tcp_conn);
		if (!(tcp_fd->tf_flags & TFF_IOCTL_IP))
			return NW_OK;
		else
			return NW_SUSPEND;
		break;
	default:
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get(tcp_fd, EBADIOCTL, TRUE);
		result= NW_OK;
		break;
	}
	return result;
}

PRIVATE int tcp_setconf(tcp_fd)
tcp_fd_t *tcp_fd;
{
	nwio_tcpconf_t *tcpconf;
	nwio_tcpconf_t oldconf, newconf;
	acc_t *data;
	int result;
	tcpport_t port;
	tcp_fd_t *fd_ptr;
	unsigned int new_en_flags, new_di_flags,
		old_en_flags, old_di_flags, all_flags, flags;
	int i;

	data= (*tcp_fd->tf_get_userdata)
		(tcp_fd->tf_srfd, 0,
		sizeof(nwio_tcpconf_t), TRUE);

	if (!data)
		return EFAULT;

	data= bf_packIffLess(data, sizeof(nwio_tcpconf_t));
assert (data->acc_length == sizeof(nwio_tcpconf_t));

	tcpconf= (nwio_tcpconf_t *)ptr2acc_data(data);
	oldconf= tcp_fd->tf_tcpconf;
	newconf= *tcpconf;

	old_en_flags= oldconf.nwtc_flags & 0xffff;
	old_di_flags= (oldconf.nwtc_flags >> 16) &
		0xffff;
	new_en_flags= newconf.nwtc_flags & 0xffff;
	new_di_flags= (newconf.nwtc_flags >> 16) &
		0xffff;
	if (new_en_flags & new_di_flags)
	{
#if DEBUG
 { where(); printf("bad ioctl\n"); }
#endif
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get(tcp_fd, EBADMODE, TRUE);
		return NW_OK;
	}

	/* NWTC_ACC_MASK */
	if (new_di_flags & NWTC_ACC_MASK)
	{
#if DEBUG
 { where(); printf("bad ioctl\n"); }
#endif
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get(tcp_fd, EBADMODE, TRUE);
		return NW_OK;
		/* access modes can't be disabled */
	}

	if (!(new_en_flags & NWTC_ACC_MASK))
		new_en_flags |= (old_en_flags & NWTC_ACC_MASK);
	
	/* NWTC_LOCPORT_MASK */
	if (new_di_flags & NWTC_LOCPORT_MASK)
	{
#if DEBUG
 { where(); printf("bad ioctl\n"); }
#endif
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get(tcp_fd, EBADMODE, TRUE);
		return NW_OK;
		/* the loc ports can't be disabled */
	}
	if (!(new_en_flags & NWTC_LOCPORT_MASK))
	{
		new_en_flags |= (old_en_flags &
			NWTC_LOCPORT_MASK);
#if DEBUG
 { where(); printf("locport= old locport (=%u)\n",
	ntohs(newconf.nwtc_locport)); }
#endif
		newconf.nwtc_locport= oldconf.nwtc_locport;
	}
	else if ((new_en_flags & NWTC_LOCPORT_MASK) == NWTC_LP_SEL)
	{
		newconf.nwtc_locport= find_unused_port(tcp_fd-
			tcp_fd_table);
#if DEBUG & 256
 { where(); printf("locport selected (=%u)\n",
	ntohs(newconf.nwtc_locport)); }
#endif
	}
	else if ((new_en_flags & NWTC_LOCPORT_MASK) == NWTC_LP_SET)
	{
#if DEBUG & 256
 { where(); printf("locport set (=%u)\n",
   ntohs(newconf.nwtc_locport)); }
#endif
		if (!newconf.nwtc_locport)
		{
#if DEBUG
 { where(); printf("bad ioctl\n"); }
#endif
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get(tcp_fd, EBADMODE, TRUE);
			return NW_OK;
		}
	}
	
	/* NWTC_REMADDR_MASK */
	if (!((new_en_flags | new_di_flags) &
		NWTC_REMADDR_MASK))
	{
		new_en_flags |= (old_en_flags &
			NWTC_REMADDR_MASK);
		new_di_flags |= (old_di_flags &
			NWTC_REMADDR_MASK);
		newconf.nwtc_remaddr= oldconf.nwtc_remaddr;
	}
	else if (new_en_flags & NWTC_SET_RA)
	{
		if (!newconf.nwtc_remaddr)
		{
#if DEBUG
 { where(); printf("bad ioctl\n"); }
#endif
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get(tcp_fd, EBADMODE, TRUE);
			return NW_OK;
		}
	}
	else
	{
assert (new_di_flags & NWTC_REMADDR_MASK);
		newconf.nwtc_remaddr= 0;
	}

	/* NWTC_REMPORT_MASK */
	if (!((new_en_flags | new_di_flags) & NWTC_REMPORT_MASK))
	{
		new_en_flags |= (old_en_flags &
			NWTC_REMPORT_MASK);
		new_di_flags |= (old_di_flags &
			NWTC_REMPORT_MASK);
		newconf.nwtc_remport=
			oldconf.nwtc_remport;
	}
	else if (new_en_flags & NWTC_SET_RP)
	{
		if (!newconf.nwtc_remport)
		{
#if DEBUG
 { where(); printf("bad ioctl\n"); }
#endif
			tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
			reply_thr_get(tcp_fd, EBADMODE, TRUE);
			return NW_OK;
		}
	}
	else
	{
assert (new_di_flags & NWTC_REMPORT_MASK);
		newconf.nwtc_remport= 0;
	}

	newconf.nwtc_flags= ((unsigned long)new_di_flags
		<< 16) | new_en_flags;
	all_flags= new_en_flags | new_di_flags;

	/* Let's check the access modes */
	if ((all_flags & NWTC_LOCPORT_MASK) == NWTC_LP_SEL ||
		(all_flags & NWTC_LOCPORT_MASK) == NWTC_LP_SET)
	{
		for (i=0, fd_ptr= tcp_fd_table; i<TCP_FD_NR; i++, fd_ptr++)
		{
			if (fd_ptr == tcp_fd)
				continue;
			if (!(fd_ptr->tf_flags & TFF_INUSE))
				continue;
			flags= fd_ptr->tf_tcpconf.nwtc_flags;
			if ((flags & NWTC_LOCPORT_MASK) != NWTC_LP_SEL &&
				(flags &  NWTC_LOCPORT_MASK) != NWTC_LP_SET)
				continue;
			if (fd_ptr->tf_tcpconf.nwtc_locport !=
				newconf.nwtc_locport)
				continue;
			if ((flags & NWTC_ACC_MASK) != (all_flags  &
				NWTC_ACC_MASK))
			{
				tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
				reply_thr_get(tcp_fd, EADDRINUSE, TRUE);
				return NW_OK;
			}
		}
	}
				
	tcp_fd->tf_tcpconf= newconf;

	if ((all_flags & NWTC_ACC_MASK) &&
		((all_flags & NWTC_LOCPORT_MASK) == NWTC_LP_SET ||
		(all_flags & NWTC_LOCPORT_MASK) == NWTC_LP_SEL) &&
		(all_flags & NWTC_REMADDR_MASK) &&
		(all_flags & NWTC_REMPORT_MASK))
		tcp_fd->tf_flags |= TFF_OPTSET;
	else
	{
#if DEBUG
 { where();
   if (!(all_flags & NWTC_ACC_MASK)) printf("NWTC_ACC_MASK not set ");
   if (!(all_flags & (NWTC_LP_SET|NWTC_LP_SEL)))
    printf("local port not set ");
   if (!(all_flags & NWTC_REMADDR_MASK))
    printf("NWTC_REMADDR_MASK not set ");
   if (!(all_flags & NWTC_REMPORT_MASK))
    printf("NWTC_REMPORT_MASK not set "); }
#endif
		tcp_fd->tf_flags &= ~TFF_OPTSET;
	}
	bf_afree(data);
	tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
	reply_thr_get(tcp_fd, NW_OK, TRUE);
	return NW_OK;
}


PRIVATE tcpport_t find_unused_port(fd)
int fd;
{
	tcpport_t port, nw_port;

	for (port= 0x8000; port < 0xffff-TCP_FD_NR; port+= TCP_FD_NR)
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

PRIVATE int is_unused_port(port)
tcpport_t port;
{
	int i;
	tcp_fd_t *tcp_fd;
	tcp_conn_t *tcp_conn;

	for (i= 0, tcp_fd= tcp_fd_table; i<TCP_FD_NR; i++,
		tcp_fd++)
	{
		if (!(tcp_fd->tf_flags & TFF_OPTSET))
			continue;
		if (tcp_fd->tf_tcpconf.nwtc_locport == port)
			return FALSE;
	}
	for (i= TCP_PORT_NR, tcp_conn= tcp_conn_table+i;
		i<TCP_CONN_NR; i++, tcp_conn++)
		/* the first TCP_PORT_NR ports are special */
	{
		if (!(tcp_conn->tc_flags & TCF_INUSE))
			continue;
		if (tcp_conn->tc_locport == port)
			return FALSE;
	}
	return TRUE;
}

PRIVATE int reply_thr_put(tcp_fd, reply, for_ioctl)
tcp_fd_t *tcp_fd;
int reply;
int for_ioctl;
{
#if DEBUG & 256
 { where(); printf("reply_thr_put(..) called\n"); }
#endif
#if DEBUG & 256
 { where(); printf("calling 0x%x\n", tcp_fd->tf_put_userdata); }
#endif
assert (tcp_fd);
	return (*tcp_fd->tf_put_userdata)(tcp_fd->tf_srfd, reply,
		(acc_t *)0, for_ioctl);
}

PRIVATE void reply_thr_get(tcp_fd, reply, for_ioctl)
tcp_fd_t *tcp_fd;
int reply;
int for_ioctl;
{
	acc_t *result;
#if DEBUG & 256
 { where(); printf("reply_thr_get(..) called\n"); }
#endif
	result= (*tcp_fd->tf_get_userdata)(tcp_fd->tf_srfd, reply,
		(size_t)0, for_ioctl);
	assert (!result);
}

PUBLIC int tcp_su4listen(tcp_fd)
tcp_fd_t *tcp_fd;
{
	tcp_conn_t *tcp_conn;
	acc_t *tmp_acc;

	tcp_conn= tcp_fd->tf_conn;

	tcp_conn->tc_locport= tcp_fd->tf_tcpconf.nwtc_locport;
	tcp_conn->tc_locaddr= tcp_fd->tf_port->tp_ipaddr;
	if (tcp_fd->tf_tcpconf.nwtc_flags & NWTC_SET_RP)
		tcp_conn->tc_remport= tcp_fd->tf_tcpconf.nwtc_remport;
	else
		tcp_conn->tc_remport= 0;
	if (tcp_fd->tf_tcpconf.nwtc_flags & NWTC_SET_RA)
		tcp_conn->tc_remaddr= tcp_fd->tf_tcpconf.nwtc_remaddr;
	else
		tcp_conn->tc_remaddr= 0;

	tcp_setup_conn(tcp_conn);
	tcp_conn->tc_port= tcp_fd->tf_port;
	tcp_conn->tc_mainuser= tcp_fd;
	tcp_conn->tc_connuser= tcp_fd;
	tcp_conn->tc_orglisten= TRUE;
	tcp_conn->tc_state= TCS_LISTEN;
#if DEBUG & 2
 { where(); tcp_write_state(tcp_conn); }
#endif
	return NW_SUSPEND;
}

/*
find_empty_conn

This function returns a connection that is not inuse.
This includes connections that are never used, and connection without a
user that are not used for a while.
*/

PRIVATE tcp_conn_t *find_empty_conn()
{
	int i;
	tcp_conn_t *tcp_conn;
	int state;

	for (i=TCP_PORT_NR, tcp_conn= tcp_conn_table+i;
		i<TCP_CONN_NR; i++, tcp_conn++)
		/* the first TCP_PORT_NR connection are reserved for
			RSTs */
	{
		if (tcp_conn->tc_flags == TCF_EMPTY)
		{
			tcp_conn->tc_connuser= NULL;
			tcp_conn->tc_mainuser= NULL;
			return tcp_conn;
		}
		if (tcp_conn->tc_mainuser)
			continue;
		if (tcp_conn->tc_senddis > get_time())
			continue;
		if (tcp_conn->tc_state != TCS_CLOSED)
		{
#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
			 tcp_close_connection (tcp_conn, ENOCONN);
		}
		return tcp_conn;
	}
	return NULL;
}


/*
find_conn_entry

This function return a connection matching locport, locaddr, remport, remaddr.
If no such connection exists NULL is returned.
If a connection exists without mainuser it is closed.
*/

PRIVATE tcp_conn_t *find_conn_entry(locport, locaddr, remport, remaddr)
tcpport_t locport;
ipaddr_t locaddr;
tcpport_t remport;
ipaddr_t remaddr;
{
	tcp_conn_t *tcp_conn;
	int i, state;

#if DEBUG & 256
 { where(); printf("find_conn_entry(locport= %u, locaddr= ", ntohs(locport)); 
	writeIpAddr(locaddr);
	printf("\nremport= %u, remaddr= ", ntohs(remport));
	writeIpAddr(remaddr); printf(")\n"); }
#endif
assert(remport);
assert(remaddr);
	for (i=TCP_PORT_NR, tcp_conn= tcp_conn_table+i; i<TCP_CONN_NR;
		i++, tcp_conn++)
		/* the first TCP_PORT_NR connection are reserved for
			RSTs */
	{
		if (tcp_conn->tc_flags == TCF_EMPTY)
			continue;
		if (tcp_conn->tc_locport != locport ||
			tcp_conn->tc_locaddr != locaddr ||
			tcp_conn->tc_remport != remport ||
			tcp_conn->tc_remaddr != remaddr)
			continue;
		if (tcp_conn->tc_mainuser)
			return tcp_conn;
		state= tcp_conn->tc_state;
		if (state != TCS_CLOSED)
		{
			tcp_close_connection(tcp_conn, ENOCONN);
		}
		return tcp_conn;
	}
	return NULL;
}

PRIVATE void read_ip_packets(tcp_port)
tcp_port_t *tcp_port;
{
	int result;

	do
	{
		tcp_port->tp_flags |= TPF_READ_IP;
#if DEBUG & 256
 { where(); printf("doing ip_read\n"); }
#endif
		result= ip_read(tcp_port->tp_ipfd, TCP_MAX_DATAGRAM);
		if (result == NW_SUSPEND)
		{
			tcp_port->tp_flags |= TPF_READ_SP;
			return;
		}
assert(result == NW_OK);
		tcp_port->tp_flags &= ~TPF_READ_IP;
	} while(!(tcp_port->tp_flags & TPF_READ_IP));
}

/*
process_inc_fragm
*/

PRIVATE void process_inc_fragm(tcp_port, data)
tcp_port_t *tcp_port;
acc_t *data;
{
	acc_t *ip_pack, *tcp_pack;
	ip_hdr_t *ip_hdr;
	tcp_hdr_t *tcp_hdr;
	tcp_conn_t *tcp_conn;
	int pack_len, ip_hdr_len;

	pack_len= bf_bufsize(data);
	if ((u16_t)~tcp_pack_oneCsum(data, pack_len))
	{
		data= bf_packIffLess(data, IP_MIN_HDR_SIZE);
		ip_hdr= (ip_hdr_t *)ptr2acc_data(data);
#if DEBUG
 { where(); printf("checksum error in tcp_pack\n");
   printf("tcp_pack_oneCsum(...)= 0x%x (%d) length= %d\n", 
    (u16_t)~tcp_pack_oneCsum(data, pack_len),
    (u16_t)~tcp_pack_oneCsum(data, pack_len), pack_len);
    printf("src ip_addr= "); writeIpAddr(ip_hdr->ih_src); printf("\n"); }
#endif
		bf_afree(data);
		return;
	}

	data= bf_packIffLess(data, IP_MIN_HDR_SIZE);
	ip_hdr= (ip_hdr_t *)ptr2acc_data(data);
	ip_hdr_len= (ip_hdr->ih_vers_ihl & IH_IHL_MASK) << 2;
	ip_pack= bf_cut(data, 0, ip_hdr_len);

	pack_len -= ip_hdr_len;

	tcp_pack= bf_cut(data, ip_hdr_len, pack_len);
	bf_afree(data);

	tcp_pack= bf_packIffLess(tcp_pack, TCP_MIN_HDR_SIZE);
	tcp_hdr= (tcp_hdr_t *)ptr2acc_data(tcp_pack);

	tcp_conn= find_best_conn(ip_hdr, tcp_hdr);
assert(tcp_conn);
#if DEBUG & 256
 { where(); tcp_print_pack(ip_hdr, tcp_hdr); printf("\n");
   tcp_print_conn(tcp_conn); printf("\n"); }
#endif

	tcp_hdr->th_chksum= pack_len;	/* tcp_pack size in chksum field */
#if DEBUG & 256
 { where(); printf("calling tcp_frag2conn(...)\n"); }
#endif
	tcp_frag2conn(tcp_conn, ip_pack, tcp_pack);
}

/*
find_best_conn
*/

PRIVATE tcp_conn_t *find_best_conn(ip_hdr, tcp_hdr)
ip_hdr_t *ip_hdr;
tcp_hdr_t *tcp_hdr;
{
	
	int best_level, new_level;
	tcp_conn_t *best_conn, *listen_conn, *tcp_conn;
	tcp_fd_t *tcp_fd;
	int i;
	ipaddr_t locaddr;
	ipaddr_t remaddr;
	tcpport_t locport;
	tcpport_t remport;

	locaddr= ip_hdr->ih_dst;
	remaddr= ip_hdr->ih_src;
	locport= tcp_hdr->th_dstport;
	remport= tcp_hdr->th_srcport;
	if (!remport)	/* This can interfere with a listen, so we reject it
			 * by clearing the requested port 
			 */
		locport= 0;
		
#if DEBUG & 256
 { where(); printf("find_best_conn(locport= %u, locaddr= ",
	ntohs(locport)); writeIpAddr(locaddr);
	printf("\nremport= %u, remaddr= ", ntohs(remport));
	writeIpAddr(remaddr); printf(")\n"); }
#endif
	best_level= 0;
	best_conn= NULL;
	listen_conn= NULL;
	for (i= TCP_PORT_NR, tcp_conn= tcp_conn_table+i;
		i<TCP_CONN_NR; i++, tcp_conn++)
		/* the first TCP_PORT_NR connection are reserved for
			RSTs */
	{
		if (!(tcp_conn->tc_flags & TCF_INUSE))
			continue;
		/* First fast check for open connections. */
		if (tcp_conn->tc_locaddr == locaddr && 
			tcp_conn->tc_locport == locport &&
			tcp_conn->tc_remport == remport &&
			tcp_conn->tc_remaddr == remaddr &&
			tcp_conn->tc_mainuser)
			return tcp_conn;

		/* Now check for listens and abandoned connections. */
		if (tcp_conn->tc_locaddr != locaddr)
		{
#if DEBUG
 { where(); printf("conn %d: wrong locaddr\n",i); }
#endif
			continue;
		}
		new_level= 0;
		if (tcp_conn->tc_locport)
		{
			if (locport != tcp_conn->tc_locport)
			{
#if DEBUG & 256
 { where(); printf("conn %d: wrong locport(%u)\n",i,
	ntohs(tcp_conn->tc_locport)); }
#endif
				continue;
			}
			new_level += 4;
		}
		if (tcp_conn->tc_remport)
		{
			if (remport != tcp_conn->tc_remport)
			{
#if DEBUG & 256
 { where(); printf("conn %d: wrong remport\n",i); }
#endif
				continue;
			}
			new_level += 1;
		}
		if (tcp_conn->tc_remaddr)
		{
			if (remaddr != tcp_conn->tc_remaddr)
			{
#if DEBUG & 256
 { where(); printf("conn %d: wrong remaddr\n",i); }
#endif
				continue;
			}
			new_level += 2;
		}
		if (new_level<best_level)
			continue;
		if (new_level != 7 && tcp_conn->tc_state != TCS_LISTEN)
			continue;
		if (new_level == 7 && !tcp_conn->tc_mainuser)
			/* We found an abandoned connection */
		{
assert (!best_conn);
			best_conn= tcp_conn;
			continue;
		}
		if (!(tcp_hdr->th_flags & THF_SYN))
			continue;
		best_level= new_level;
		listen_conn= tcp_conn;
	}
	if (!best_conn && !listen_conn)
	{
#if DEBUG & 256
 { where(); printf("refusing connection for locport= %u, locaddr= ",
	ntohs(locport)); writeIpAddr(locaddr);
	printf("\nremport= %u, remaddr= ", ntohs(remport));
	writeIpAddr(remaddr); printf("\n"); }
#endif
		for (i=0, tcp_conn= tcp_conn_table; i<TCP_PORT_NR;
			i++, tcp_conn++)
			/* find valid port to send RST */
			if ((tcp_conn->tc_flags & TCF_INUSE) &&
				tcp_conn->tc_locaddr==locaddr)
			{
				break;
			}
assert (tcp_conn);
assert (tcp_conn->tc_state == TCS_CLOSED);
		tcp_conn->tc_locport= locport;
		tcp_conn->tc_locaddr= locaddr;
		tcp_conn->tc_remport= remport;
		tcp_conn->tc_remaddr= remaddr;
assert (!tcp_conn->tc_mainuser);
assert (!tcp_conn->tc_readuser);
assert (!tcp_conn->tc_writeuser);
		return tcp_conn;

	}
	if (best_conn)
	{
assert(!best_conn->tc_mainuser);
		if (!listen_conn)
			return best_conn;
		tcp_fd= listen_conn->tc_mainuser;

assert(tcp_fd && tcp_fd == listen_conn->tc_connuser &&
	tcp_fd->tf_conn == listen_conn);

		if (best_conn->tc_state != TCS_CLOSED)
			tcp_close_connection(best_conn, ENOCONN);
		best_conn->tc_state= TCS_LISTEN;
#if DEBUG
 { where(); tcp_write_state(best_conn); }
#endif
		best_conn->tc_mainuser= best_conn->tc_connuser=
			tcp_fd;
		best_conn->tc_flags= listen_conn->tc_flags;
		tcp_fd->tf_conn= best_conn;
		listen_conn->tc_flags= TCF_EMPTY;
#if DEBUG & 16
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	listen_conn-tcp_conn_table, listen_conn->tc_flags); }
#endif
		return best_conn;
	}
assert (listen_conn);
	return listen_conn;
}


PUBLIC void tcp_reply_ioctl(tcp_fd, reply)
tcp_fd_t *tcp_fd;
int reply;
{
#if DEBUG & 256
 { where(); printf("tcp_reply_ioctl called\n"); }
#endif
#if DEBUG
 if (!(tcp_fd->tf_flags & TFF_IOCTL_IP))
 { where(); printf("not TFF_IOCTL_IP\n"); }
#endif
assert (tcp_fd->tf_flags & TFF_IOCTL_IP);
#if DEBUG
if (tcp_fd->tf_ioreq != NWIOTCPSHUTDOWN && tcp_fd->tf_ioreq != NWIOTCPLISTEN &&
	tcp_fd->tf_ioreq != NWIOTCPCONN)
 { where(); printf("wrong value in ioreq (0x%lx)\n", tcp_fd->tf_ioreq); }
#endif
assert (tcp_fd->tf_ioreq == NWIOTCPSHUTDOWN ||
	tcp_fd->tf_ioreq == NWIOTCPLISTEN || tcp_fd->tf_ioreq == NWIOTCPCONN);
	
	tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
	reply_thr_get (tcp_fd, reply, TRUE);
}

PUBLIC void tcp_reply_write(tcp_fd, reply)
tcp_fd_t *tcp_fd;
size_t reply;
{
	assert (tcp_fd->tf_flags & TFF_WRITE_IP);

	tcp_fd->tf_flags &= ~TFF_WRITE_IP;
	reply_thr_get (tcp_fd, reply, FALSE);
}

PUBLIC void tcp_reply_read(tcp_fd, reply)
tcp_fd_t *tcp_fd;
size_t reply;
{
	assert (tcp_fd->tf_flags & TFF_READ_IP);

#if DEBUG & 256
 { where(); printf("tcp_reply_read(.., %d)\n", reply); }
#endif
	tcp_fd->tf_flags &= ~TFF_READ_IP;
	reply_thr_put (tcp_fd, reply, FALSE);
}

PUBLIC int tcp_write(fd, count)
int fd;
size_t count;
{
	tcp_fd_t *tcp_fd;
	tcp_conn_t *tcp_conn;

	tcp_fd= &tcp_fd_table[fd];

	assert (tcp_fd->tf_flags & TFF_INUSE);

	if (!(tcp_fd->tf_flags & TFF_CONNECTED))
	{
		reply_thr_get (tcp_fd, ENOTCONN, FALSE);
		return NW_OK;
	}
	tcp_conn= tcp_fd->tf_conn;
	if (tcp_conn->tc_flags & TCF_FIN_SENT)
	{
#if DEBUG & 16
 { where(); printf("replying ESHUTDOWN for connection %d and fd %d\n",
	tcp_conn-tcp_conn_table, tcp_fd-tcp_fd_table); }
#endif
		reply_thr_get (tcp_fd, ESHUTDOWN, FALSE);
		return NW_OK;
	}

	tcp_fd->tf_flags |= TFF_WRITE_IP;
	tcp_fd->tf_write_offset= 0;
	tcp_fd->tf_write_count= count;

	if (tcp_conn->tc_writeuser)
		return NW_SUSPEND;

	tcp_conn->tc_writeuser= tcp_fd;
	tcp_restart_fd_write (tcp_conn);
	if (!(tcp_fd->tf_flags & TFF_WRITE_IP))
		return NW_OK;
	else
		return NW_SUSPEND;
}

PUBLIC int tcp_read(fd, count)
int fd;
size_t count;
{
	tcp_fd_t *tcp_fd;
	tcp_conn_t *tcp_conn;

#if DEBUG & 256
 { where(); printf("tcp_read(%d, %u)\n", fd, count); }
#endif
	tcp_fd= &tcp_fd_table[fd];

	assert (tcp_fd->tf_flags & TFF_INUSE);

	if (!(tcp_fd->tf_flags & TFF_CONNECTED))
	{
		reply_thr_put (tcp_fd, ENOTCONN, FALSE);
		return NW_OK;
	}
	tcp_conn= tcp_fd->tf_conn;

	tcp_fd->tf_flags |= TFF_READ_IP;
	tcp_fd->tf_read_offset= 0;
	tcp_fd->tf_read_count= count;

	if (tcp_conn->tc_readuser)
		return NW_SUSPEND;

	tcp_conn->tc_readuser= tcp_fd;
	tcp_restart_fd_read (tcp_conn);
	if (!(tcp_fd->tf_flags & TFF_READ_IP))
		return NW_OK;
	else
		return NW_SUSPEND;
}

/*
tcp_restart_connect

reply the success or failure of a connect to the user.
*/


PUBLIC void tcp_restart_connect(tcp_fd)
tcp_fd_t *tcp_fd;
{
	tcp_conn_t *tcp_conn;
	int reply;

#if DEBUG & 256
 { where(); printf("tcp_restart_connect called\n"); }
#endif
assert (tcp_fd->tf_flags & TFF_IOCTL_IP);
assert (tcp_fd->tf_ioreq == NWIOTCPLISTEN || tcp_fd->tf_ioreq == NWIOTCPCONN);

	tcp_conn= tcp_fd->tf_conn;

assert (tcp_conn->tc_connuser == tcp_fd);

	if (tcp_conn->tc_state == TCS_CLOSED)
		reply= tcp_conn->tc_error;
	else
	{
		tcp_fd->tf_flags |= TFF_CONNECTED;
		reply= NW_OK;
	}
	tcp_conn->tc_connuser= 0;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_connuser= 0x%x\n", tcp_conn-
	tcp_conn_table, tcp_conn->tc_connuser); }
#endif
	tcp_reply_ioctl (tcp_fd, reply);
}

PUBLIC void tcp_close(fd)
int fd;
{
	tcp_fd_t *tcp_fd;
	tcp_conn_t *tcp_conn;

	tcp_fd= &tcp_fd_table[fd];

#if DEBUG
 { if (!(tcp_fd->tf_flags & TFF_INUSE)) { where(); printf("not inuse\n"); }
   if (tcp_fd->tf_flags & TFF_IOCTL_IP) { where(); printf("ioctl ip\n"); }
   if (tcp_fd->tf_flags & TFF_READ_IP) { where(); printf("read ip\n"); }
   if (tcp_fd->tf_flags & TFF_WRITE_IP) { where(); printf("write ip\n"); }
 }
#endif
assert (tcp_fd->tf_flags & TFF_INUSE);
assert (!(tcp_fd->tf_flags & (TFF_IOCTL_IP|TFF_READ_IP|TFF_WRITE_IP)));

	tcp_fd->tf_flags &= ~TFF_INUSE;
	if (!tcp_fd->tf_conn)
		return;

	tcp_conn= tcp_fd->tf_conn;
	close_mainuser(tcp_conn, tcp_fd);
	tcp_shutdown (tcp_conn);
}

PUBLIC int tcp_cancel(fd, which_operation)
int fd;
int which_operation;
{
	tcp_fd_t *tcp_fd;
	tcp_conn_t *tcp_conn;
	int i;
	int type;

#if DEBUG & 256
 { where(); printf("tcp_cancel(%d, %d)\n", fd, which_operation); }
#endif
	tcp_fd= &tcp_fd_table[fd];

	assert (tcp_fd->tf_flags & TFF_INUSE);

	tcp_conn= tcp_fd->tf_conn;

	switch (which_operation)
	{
	case SR_CANCEL_WRITE:
assert (tcp_fd->tf_flags & TFF_WRITE_IP);
		tcp_fd->tf_flags &= ~TFF_WRITE_IP;
		if (tcp_conn->tc_writeuser == tcp_fd)
		{
			tcp_conn->tc_writeuser= 0;
			tcp_restart_fd_write (tcp_conn);
		}
		if (tcp_fd->tf_write_count)
			reply_thr_get (tcp_fd, tcp_fd->tf_write_count, FALSE);
		else
			reply_thr_get (tcp_fd, EINTR, FALSE);
		break;
	case SR_CANCEL_READ:
assert (tcp_fd->tf_flags & TFF_READ_IP);
		tcp_fd->tf_flags &= ~TFF_READ_IP;
		if (tcp_conn->tc_readuser == tcp_fd)
		{
			tcp_conn->tc_readuser= 0;
			tcp_restart_fd_read (tcp_conn);
		}
		if (tcp_fd->tf_read_count)
			reply_thr_put (tcp_fd, tcp_fd->tf_read_count, FALSE);
		else
			reply_thr_put (tcp_fd, EINTR, FALSE);
		break;
	case SR_CANCEL_IOCTL:
assert (tcp_fd->tf_flags & TFF_IOCTL_IP);
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;

		type= tcp_fd->tf_ioreq & IOCTYPE_MASK;
		switch (type)
		{
		case NWIOGTCPCONF & IOCTYPE_MASK:
			reply_thr_put (tcp_fd, EINTR, TRUE);
			break;
		case NWIOSTCPCONF & IOCTYPE_MASK:
		case NWIOTCPSHUTDOWN & IOCTYPE_MASK:
		case NWIOTCPATTACH & IOCTYPE_MASK:
			reply_thr_get (tcp_fd, EINTR, TRUE);
			break;
		case NWIOTCPCONN & IOCTYPE_MASK:
		case NWIOTCPLISTEN & IOCTYPE_MASK:
assert (tcp_conn->tc_connuser == tcp_fd);
			tcp_conn->tc_connuser= 0;
			tcp_conn->tc_mainuser= 0;
			tcp_close_connection(tcp_conn, ENOCONN);
			reply_thr_get (tcp_fd, EINTR, TRUE);
			break;
		default:
			ip_warning(( "unknown ioctl inprogress: 0x%x",
				tcp_fd->tf_ioreq ));
			reply_thr_get (tcp_fd, EINTR, TRUE);
			break;
		}
		break;
	default:
		ip_panic(( "unknown cancel request" ));
		break;
	}
	return NW_OK;
}

/*
tcp_connect
*/

PRIVATE int tcp_connect(tcp_fd)
tcp_fd_t *tcp_fd;
{
	tcp_conn_t *tcp_conn;
	int state;

	if (!(tcp_fd->tf_flags & TFF_OPTSET))
	{
#if DEBUG
 { where(); }
#endif
		tcp_reply_ioctl(tcp_fd, EBADMODE);
		return NW_OK;
	}
	if (tcp_fd->tf_flags & TFF_CONNECT)
	{
		tcp_reply_ioctl(tcp_fd, EISCONN);
		return NW_OK;
	}
	if ((tcp_fd->tf_tcpconf.nwtc_flags & (NWTC_SET_RA|NWTC_SET_RP))
		!= (NWTC_SET_RA|NWTC_SET_RP))
	{
#if DEBUG
 { where(); printf("tcp_fd_table[%d].tf_tcpconf.nwtc_flags= 0x%x\n",
	tcp_fd-tcp_fd_table, tcp_fd->tf_tcpconf.nwtc_flags); }
#endif
		tcp_reply_ioctl(tcp_fd, EBADMODE);
		return NW_OK;
	}

assert(!tcp_fd->tf_conn);
	tcp_conn= find_conn_entry(tcp_fd->tf_tcpconf.nwtc_locport,
		tcp_fd->tf_port->tp_ipaddr,
		tcp_fd->tf_tcpconf.nwtc_remport,
		tcp_fd->tf_tcpconf.nwtc_remaddr);
	if (tcp_conn)
	{
		if (tcp_conn->tc_mainuser)
		{
			tcp_reply_ioctl(tcp_fd, EADDRINUSE);
			return NW_OK;
		}
	}
	else
	{
		tcp_conn= find_empty_conn();
		if (!tcp_conn)
		{
			tcp_reply_ioctl(tcp_fd, EAGAIN);
			return NW_OK;
		}
	}
	tcp_fd->tf_conn= tcp_conn;
	return tcp_su4connect(tcp_fd);
}

/*
tcp_su4connect
*/

PRIVATE int tcp_su4connect(tcp_fd)
tcp_fd_t *tcp_fd;
{
	tcp_conn_t *tcp_conn;
	acc_t *tmp_acc;

	tcp_conn= tcp_fd->tf_conn;

	tcp_conn->tc_locport= tcp_fd->tf_tcpconf.nwtc_locport;
	tcp_conn->tc_locaddr= tcp_fd->tf_port->tp_ipaddr;

assert (tcp_fd->tf_tcpconf.nwtc_flags & NWTC_SET_RP);
assert (tcp_fd->tf_tcpconf.nwtc_flags & NWTC_SET_RA);

	tcp_conn->tc_remport= tcp_fd->tf_tcpconf.nwtc_remport;
	tcp_conn->tc_remaddr= tcp_fd->tf_tcpconf.nwtc_remaddr;
	tcp_conn->tc_mainuser= tcp_fd;

	tcp_setup_conn(tcp_conn);
	tcp_conn->tc_port= tcp_fd->tf_port;
	tcp_conn->tc_connuser= tcp_fd;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_connuser= 0x%x\n", tcp_conn-
	tcp_conn_table, tcp_conn->tc_connuser); }
#endif
	tcp_conn->tc_orglisten= FALSE;
	tcp_conn->tc_state= TCS_SYN_SENT;
#if DEBUG & 16
 { where(); tcp_write_state(tcp_conn); }
#endif
	tcp_restart_write (tcp_conn);

	if (tcp_conn->tc_connuser)
		return NW_SUSPEND;
	else
		return NW_OK;
}

PRIVATE void close_mainuser(tcp_conn, tcp_fd)
tcp_conn_t *tcp_conn;
tcp_fd_t *tcp_fd;
{
	int i;

	if (tcp_conn->tc_mainuser != tcp_fd)
		return;

	tcp_conn->tc_mainuser= 0;
	assert (tcp_conn->tc_writeuser != tcp_fd);
	assert (tcp_conn->tc_readuser != tcp_fd);
	assert (tcp_conn->tc_connuser != tcp_fd);

	for (i= 0, tcp_fd= tcp_fd_table; i<TCP_FD_NR; i++, tcp_fd++)
	{
		if (!(tcp_fd->tf_flags & TFF_INUSE))
			continue;
		if (tcp_fd->tf_conn != tcp_conn)
			continue;
		tcp_conn->tc_mainuser= tcp_fd;
		return;
	}
}


PRIVATE int conn_right4fd(tcp_conn, tcp_fd)
tcp_fd_t *tcp_fd;
tcp_conn_t *tcp_conn;
{
	unsigned long flags;

	flags= tcp_fd->tf_tcpconf.nwtc_flags;

	if (tcp_fd->tf_tcpconf.nwtc_locport != tcp_conn->tc_locport)
		return FALSE;

	if ((flags & NWTC_SET_RA) && tcp_fd->tf_tcpconf.nwtc_remaddr !=
		tcp_conn->tc_remaddr)
		return FALSE;

	if ((flags & NWTC_SET_RP) && tcp_fd->tf_tcpconf.nwtc_remport !=
		tcp_conn->tc_remport)
		return FALSE;

	if (tcp_fd->tf_port != tcp_conn->tc_port)
		return FALSE;

	return TRUE;
}

PRIVATE int tcp_attache(tcp_fd)
tcp_fd_t *tcp_fd;
{
	tcp_conn_t *tcp_conn;
	int state;

	if (!(tcp_fd->tf_flags & TFF_OPTSET))
	{
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get(tcp_fd, EBADMODE, TRUE);
		return NW_OK;
	}
	if (tcp_fd->tf_flags & TFF_CONNECT)
	{
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get(tcp_fd, EISCONN, TRUE);
		return NW_OK;
	}
	if ((tcp_fd->tf_tcpconf.nwtc_flags & (NWTC_SET_RA|NWTC_SET_RP))
		!= (NWTC_SET_RA|NWTC_SET_RP))
	{
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get(tcp_fd, EBADMODE, TRUE);
		return NW_OK;
	}

	tcp_conn= find_conn_entry(tcp_fd->tf_tcpconf.nwtc_locport,
		tcp_fd->tf_port->tp_ipaddr,
		tcp_fd->tf_tcpconf.nwtc_remport,
		tcp_fd->tf_tcpconf.nwtc_remaddr);
	if (!tcp_conn)
	{
#if DEBUG
 { where(); printf("conn_entry not found\n"); }
#endif
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get (tcp_fd, ENOCONN, TRUE);
		return NW_OK;
	}
	assert (tcp_conn->tc_flags & TCF_INUSE);
	state= tcp_conn->tc_state;
	if (state == TCS_CLOSED || state == TCS_LISTEN || state ==
		TCS_SYN_SENT || state == TCS_SYN_RECEIVED)
	{
#if DEBUG
 { where(); printf("conn_entry in wrong state: ");
	tcp_write_state(tcp_conn); printf("\n"); }
#endif
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get (tcp_fd, ENOCONN, TRUE);
		return NW_OK;
	}
	tcp_fd->tf_conn= tcp_conn;
	tcp_fd->tf_flags |= TFF_CONNECTED;
	tcp_fd->tf_flags |= TFF_PUSH_DATA;	/* XXX */
	return NW_OK;
}

/*
tcp_listen
*/

PRIVATE int tcp_listen(tcp_fd)
tcp_fd_t *tcp_fd;
{
	tcp_conn_t *tcp_conn;
	int state;

#if DEBUG & 256
 { where(); printf("tcp_listen(&tcp_fd_table[%d]) called\n", tcp_fd-
	tcp_fd_table); }
#endif
	if (!(tcp_fd->tf_flags & TFF_OPTSET))
	{
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get(tcp_fd, EBADMODE, TRUE);
		return NW_OK;
	}
	if (tcp_fd->tf_flags & TFF_CONNECT)
	{
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get(tcp_fd, EISCONN, TRUE);
		return NW_OK;
	}
	tcp_conn= tcp_fd->tf_conn;
assert(!tcp_conn);
	if ((tcp_fd->tf_tcpconf.nwtc_flags & (NWTC_SET_RA|NWTC_SET_RP))
		== (NWTC_SET_RA|NWTC_SET_RP))
	{
		tcp_conn= find_conn_entry(
			tcp_fd->tf_tcpconf.nwtc_locport,
			tcp_fd->tf_port->tp_ipaddr,
			tcp_fd->tf_tcpconf.nwtc_remport,
			tcp_fd->tf_tcpconf.nwtc_remaddr);
		if (tcp_conn)
		{
			if (tcp_conn->tc_mainuser)
			{
				tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
				reply_thr_get (tcp_fd, EADDRINUSE, TRUE);
				return NW_OK;
			}
			tcp_fd->tf_conn= tcp_conn;
			return tcp_su4listen(tcp_fd);
		}
	}
	tcp_conn= find_empty_conn();
	if (!tcp_conn)
	{
		tcp_fd->tf_flags &= ~TFF_IOCTL_IP;
		reply_thr_get (tcp_fd, EAGAIN, TRUE);
		return NW_OK;
	}
	tcp_fd->tf_conn= tcp_conn;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_connuser= 0x%x\n", tcp_conn-
	tcp_conn_table, tcp_conn->tc_connuser); }
#endif
	return tcp_su4listen(tcp_fd);
}

PRIVATE void tcp_buffree (priority, reqsize)
int priority;
size_t reqsize;
{
	int i;
	tcp_conn_t *tcp_conn;

	if (priority <TCP_PRI_FRAG2SEND)
		return;

#if DEBUG & 256
 { where(); printf("tcp_buffree called\n"); }
#endif

	for (i=0, tcp_conn= tcp_conn_table; i<TCP_CONN_NR; i++,
		tcp_conn++)
	{
		if (!(tcp_conn->tc_flags & TCF_INUSE))
			continue;
		if (!tcp_conn->tc_frag2send)
			continue;
		bf_afree(tcp_conn->tc_frag2send);
		tcp_conn->tc_frag2send= 0;
		if (bf_free_buffsize >= reqsize)
			return;
	}

	if (priority <TCP_PRI_CONNwoUSER)
		return;

	for (i=0, tcp_conn= tcp_conn_table; i<TCP_CONN_NR; i++,
		tcp_conn++)
	{
		if (!(tcp_conn->tc_flags & TCF_INUSE))
			continue;
		if (tcp_conn->tc_mainuser)
			continue;
		if (tcp_conn->tc_state == TCS_CLOSED)
			continue;
#if DEBUG
 { where(); printf("calling tcp_close_connection (out of memory)\n"); }
#endif
		tcp_close_connection (tcp_conn, EOUTOFBUFS);
		if (bf_free_buffsize >= reqsize)
			return;
	}

	if (priority <TCP_PRI_CONN_INUSE)
		return;

	for (i=0, tcp_conn= tcp_conn_table; i<TCP_CONN_NR; i++,
		tcp_conn++)
	{
		if (!(tcp_conn->tc_flags & TCF_INUSE))
			continue;
		if (tcp_conn->tc_state == TCS_CLOSED)
			continue;
#if DEBUG
 { where(); printf("calling tcp_close_connection (out of memory)\n"); }
#endif
		tcp_close_connection (tcp_conn, EOUTOFBUFS);
		if (bf_free_buffsize >= reqsize)
			return;
	}
}

PRIVATE void tcp_notreach(pack)
acc_t *pack;
{
	acc_t *tcp_pack;
	ip_hdr_t *ip_hdr;
	tcp_hdr_t *tcp_hdr;
	tcp_conn_t *tcp_conn;
	int ip_hdr_size;
	int new_ttl;

	pack->acc_linkC++;
	pack= bf_packIffLess(pack, IP_MIN_HDR_SIZE);
	ip_hdr= (ip_hdr_t *)ptr2acc_data(pack);
	ip_hdr_size= (ip_hdr->ih_vers_ihl & IH_IHL_MASK) << 2;

	tcp_pack= bf_cut(pack, ip_hdr_size, TCP_MIN_HDR_SIZE);
	tcp_pack= bf_packIffLess(tcp_pack, TCP_MIN_HDR_SIZE);
	tcp_hdr= (tcp_hdr_t *)ptr2acc_data(tcp_pack);

	tcp_conn= find_conn_entry( tcp_hdr->th_srcport, ip_hdr->ih_src,
		tcp_hdr->th_dstport, ip_hdr->ih_dst);
	if (tcp_conn)
	{
		new_ttl= tcp_conn->tc_ttl;
		if (new_ttl == IP_MAX_TTL)
		{
#if DEBUG
 { where(); printf("calling tcp_close_connection\n"); }
#endif
			tcp_close_connection(tcp_conn, EDSTNOTRCH);
			bf_afree(pack);
			bf_afree(tcp_pack);
			return;
		}
		new_ttl *= 2;
		if (new_ttl> IP_MAX_TTL)
			new_ttl= IP_MAX_TTL;
		tcp_conn->tc_ttl= new_ttl;
		tcp_conn->tc_no_retrans= 0;
	}
	else
	{
#if DEBUG
 { where(); printf("got a EDSTNOTRCH for non existing connection\n"); }
#endif
	}
	bf_afree(pack);
	bf_afree(tcp_pack);
}

/*
tcp_setup_conn
*/

PRIVATE void tcp_setup_conn(tcp_conn)
tcp_conn_t *tcp_conn;
{
assert(!tcp_conn->tc_readuser);
assert(!tcp_conn->tc_writeuser);
assert(!tcp_conn->tc_connuser);
	if (tcp_conn->tc_flags & TCF_INUSE)
	{
assert (tcp_conn->tc_state == TCS_CLOSED || 
	tcp_conn->tc_state == TCS_TIME_WAIT);
assert (!tcp_conn->tc_send_data);
		if (tcp_conn->tc_senddis < get_time())
			tcp_conn->tc_ISS= 0;
	}
	else
	{
		tcp_conn->tc_senddis= 0;
		tcp_conn->tc_ISS= 0;
		tcp_conn->tc_tos= TCP_DEF_TOS;
		tcp_conn->tc_ttl= TCP_DEF_TTL;
		tcp_conn->tc_rcv_wnd= TCP_MAX_WND_SIZE;
		tcp_conn->tc_urg_wnd= TCP_DEF_URG_WND;
	}
	if (!tcp_conn->tc_ISS)
	{
		tcp_conn->tc_ISS= (get_time()/HZ)*ISS_INC_FREQ;
	}
	tcp_conn->tc_SND_UNA= tcp_conn->tc_ISS;
	tcp_conn->tc_SND_TRM= tcp_conn->tc_ISS;
	tcp_conn->tc_SND_NXT= tcp_conn->tc_ISS+1;
	tcp_conn->tc_SND_UP= tcp_conn->tc_ISS;
	tcp_conn->tc_SND_PSH= tcp_conn->tc_ISS;
	tcp_conn->tc_SND_WL2= tcp_conn->tc_ISS;
	tcp_conn->tc_IRS= 0;
	tcp_conn->tc_SND_WL1= tcp_conn->tc_IRS;
	tcp_conn->tc_RCV_LO= tcp_conn->tc_IRS;
	tcp_conn->tc_RCV_NXT= tcp_conn->tc_IRS;
	tcp_conn->tc_RCV_HI= tcp_conn->tc_IRS;
	tcp_conn->tc_RCV_UP= tcp_conn->tc_IRS;
	tcp_conn->tc_rcvd_data= 0;
	tcp_conn->tc_rcv_queue= 0;
	tcp_conn->tc_send_data= 0;
	tcp_conn->tc_remipopt= 0;
	tcp_conn->tc_remtcpopt= 0;
	tcp_conn->tc_frag2send= 0;
	tcp_conn->tc_no_retrans= 0;
	tcp_conn->tc_max_no_retrans= TCP_DEF_MAX_NO_RETRANS;
	tcp_conn->tc_0wnd_to= 0;
	tcp_conn->tc_rtt= TCP_DEF_RTT;
	tcp_conn->tc_ett= 0;
	tcp_conn->tc_mss= TCP_DEF_MSS;
	tcp_conn->tc_error= NW_OK;
	tcp_conn->tc_snd_cwnd= tcp_conn->tc_SND_UNA + tcp_conn->tc_mss;
	tcp_conn->tc_snd_cthresh= TCP_MAX_WND_SIZE;
	tcp_conn->tc_snd_cinc= 0;
	tcp_conn->tc_snd_wnd= TCP_MAX_WND_SIZE;
	tcp_conn->tc_flags= TCF_INUSE;
#if DEBUG & 256
 { where(); printf("tcp_conn_table[%d].tc_flags= 0x%x\n", 
	tcp_conn-tcp_conn_table, tcp_conn->tc_flags); }
#endif
}
