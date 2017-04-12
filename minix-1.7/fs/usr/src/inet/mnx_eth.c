/*
inet/mnx_eth.c

Created:	Jan 2, 1992 by Philip Homburg
*/

#include "inet.h"
#include "proto.h"
#include "generic/assert.h"
#include "generic/buf.h"
#include "osdep_eth.h"
#include "generic/clock.h"
#include "generic/eth.h"
#include "generic/eth_int.h"
#include "generic/sr.h"

INIT_PANIC();

FORWARD _PROTOTYPE( void setup_read, (eth_port_t *eth_port) );
FORWARD _PROTOTYPE( int do_sendrec, (int task, message *m1, message *m2) );
FORWARD _PROTOTYPE( void read_int, (eth_port_t *eth_port, int count) );
FORWARD _PROTOTYPE( void write_int, (eth_port_t *eth_port) );

PUBLIC void eth_init0()
{
	int result;
	eth_port_t *eth_port;
	static message mess, repl_mess;

	eth_port= &eth_port_table[0];

	eth_port->etp_osdep.etp_port= 0;
	eth_port->etp_osdep.etp_task= DL_ETH;
	eth_port->etp_osdep.etp_minor= ETH_DEV;

#if XXX
	mess.m_type= DL_STOP;
	mess.DL_PORT= eth_port->etp_osdep.etp_port;
#if DEBUG & 256
 { where(); printf("sending DL_STOP\n"); }
#endif
assert (eth_port->etp_osdep.etp_task != MM_PROC_NR);
	result= send(eth_port->etp_osdep.etp_task, &mess);
	if (result < 0)
	{
		printf("send failed with error %d\n",result);
		printf("eth_init0: unable to stop ethernet task\n");
		return;
	}
#endif

#if DEBUG & 256
 { where(); printf("sending DL_INIT\n"); }
#endif
	mess.m_type= DL_INIT;
	mess.DL_PORT= eth_port->etp_osdep.etp_port;
	mess.DL_PROC= THIS_PROC;
	mess.DL_MODE= DL_NOMODE;
assert (eth_port->etp_osdep.etp_task != MM_PROC_NR);
	result= send(eth_port->etp_osdep.etp_task, &mess);
	if (result<0)
	{
		printf(
		"eth_init0: unable to send to ethernet task, error= %d\n",
			result);
		return;
	}

	if (receive(eth_port->etp_osdep.etp_task, &mess)<0)
		ip_panic(("unable to receive"));

	if (mess.m3_i1 != eth_port->etp_osdep.etp_port)
	{
		printf("eth_init0: got reply for wrong port\n");
		return;
	}

	eth_port->etp_ethaddr= *(ether_addr_t *)mess.m3_ca1;

	if (sr_add_minor (eth_port->etp_osdep.etp_minor, 
		eth_port- eth_port_table, eth_open, eth_close, eth_read, 
		eth_write, eth_ioctl, eth_cancel)<0)
		ip_panic(("can't sr_init"));

	eth_port->etp_flags |= EPF_ENABLED;
	eth_port->etp_wr_pack= 0;
	eth_port->etp_rd_pack= 0;
	setup_read (eth_port);
}

PUBLIC void eth_write_port(eth_port)
eth_port_t *eth_port;
{
	static message mess1, mess2;
	int i, pack_size, result;
	acc_t *pack, *pack_ptr;
	iovec_t *iovec;

#if DEBUG & 256
 { where(); bf_check_all_bufs(); }
#endif
#if DEBUG & 256
 { where(); printf("send_packet(&eth_port_table[%d], ..) called\n",
	eth_port-eth_port_table); }
#endif
assert (!(eth_port->etp_flags & EPF_WRITE_IP));

	eth_port->etp_flags |= EPF_WRITE_IP;

	pack= eth_port->etp_wr_pack;
	eth_port->etp_wr_pack= 0;

	iovec= eth_port->etp_osdep.etp_wr_iovec;
	pack_size= 0;
#if DEBUG & 256
 { where(); printf("bf_bufsize= %d\n", bf_bufsize(pack)); }
#endif
	for (i=0, pack_ptr= pack; i<IOVEC_NR && pack_ptr; i++,
		pack_ptr= pack_ptr->acc_next)
	{
		iovec[i].iov_addr= (vir_bytes)ptr2acc_data(
			pack_ptr);
		pack_size += iovec[i].iov_size=
			pack_ptr->acc_length;
	}
	if (i>= IOVEC_NR)
	{
#if DEBUG
 { where(); printf("compacting fragment\n"); }
#endif
		pack= bf_pack(pack);		/* packet is too fragmented */
		pack_size= 0;
		for (i=0, pack_ptr= pack; i<IOVEC_NR &&
			pack_ptr; i++, pack_ptr= pack_ptr->
			acc_next)
		{
			iovec[i].iov_addr= (vir_bytes)
				ptr2acc_data(pack_ptr);
			pack_size += iovec[i].iov_size=
				pack_ptr->acc_length;
		}
	}
#if DEBUG & 256
 { where(); printf("bf_bufsize= %d\n", bf_bufsize(pack));
	where(); printf("i= %d\n", i); }
#endif
assert (i< IOVEC_NR);
assert (pack_size >= ETH_MIN_PACK_SIZE);

	if (i==1)
	/* simple packets can be sent using DL_WRITE instead of DL_WRITEV */
	{
		mess1.DL_COUNT= iovec[0].iov_size;
		mess1.DL_ADDR= (char *)iovec[0].iov_addr;
		mess1.m_type= DL_WRITE;
	}
	else
	{
		mess1.DL_COUNT= i;
		mess1.DL_ADDR= (char *)iovec;
		mess1.m_type= DL_WRITEV;
	}
	mess1.DL_PORT= eth_port->etp_osdep.etp_port;
	mess1.DL_PROC= THIS_PROC;
	mess1.DL_MODE= DL_NOMODE;

#if DEBUG & 256
 { where(); printf("calling do_sendrec\n"); }
#endif
assert (eth_port->etp_osdep.etp_task != MM_PROC_NR);
	result= do_sendrec (eth_port->etp_osdep.etp_task, &mess1, &mess2);
#if DEBUG & 256
 { where(); printf("do_sendrec done\n"); }
#endif

#if DEBUG & 256
 { where(); printf("got reply from DLL\n"); }
#endif
#if DEBUG
 if (mess1.m_type != DL_TASK_REPLY)
 { where(); printf("wrong m_type (=%d)\n", mess1.m_type); }
 if (mess1.DL_PORT != eth_port->etp_osdep.etp_port)
 { where(); printf("wrong DL_PORT (=%d)\n", mess1.DL_PORT); }
 if (mess1.DL_PROC != THIS_PROC)
 { where(); printf("wrong DL_PROC (=%d)\n", mess1.DL_PROC); }
#endif

assert (mess1.m_type == DL_TASK_REPLY && mess1.DL_PORT == mess1.DL_PORT &&
	mess1.DL_PROC == THIS_PROC);

assert((mess1.DL_STAT >> 16) == OK);

	if (!(mess1.DL_STAT & DL_PACK_SEND))
	/* packet is not sent, suspend */
	{
#if DEBUG & 256
 { where(); printf("setting EPF_WRITE_SP\n"); }
#endif
		eth_port->etp_flags |= EPF_WRITE_SP;
		eth_port->etp_wr_pack= pack;
	}
	else
	/* packet is sent */
	{
		eth_port->etp_flags &= ~EPF_WRITE_IP;
		eth_arrive(eth_port, pack);
#if DEBUG & 256
 { where(); printf("write done\n"); }
#endif
	}

	if (result == 1)	/* got an INT_TASK */
	{
assert(mess2.DL_STAT == DL_PACK_RECV);
assert(!(mess1.DL_STAT & DL_PACK_RECV));
compare(mess2.DL_PORT, ==, eth_port->etp_osdep.etp_port);
compare(mess2.DL_PROC, ==, THIS_PROC);
		read_int(eth_port, mess2.DL_COUNT);
	}
	else if (mess1.DL_STAT & DL_PACK_RECV)
	{
		read_int(eth_port, mess1.DL_COUNT);
	}
}

PUBLIC void eth_rec(m)
message *m;
{
	int i;
	eth_port_t *loc_port;
	int stat;

assert (m->m_source == DL_ETH);

	set_time (m->DL_CLCK);

	for (i=0, loc_port= eth_port_table; i<ETH_PORT_NR; i++, loc_port++)
	{
		if (loc_port->etp_osdep.etp_port == m->DL_PORT &&
			loc_port->etp_osdep.etp_task == m->m_source)
			break;
	}

assert (i<ETH_PORT_NR);

	stat= m->DL_STAT & 0xffff;

assert(stat & (DL_PACK_SEND|DL_PACK_RECV));
	if (stat & DL_PACK_SEND)
	{
		write_int(loc_port);
	}
	if (stat & DL_PACK_RECV)
	{
		read_int(loc_port, m->DL_COUNT);
	}
}

PUBLIC int eth_get_stat(eth_port, eth_stat)
eth_port_t *eth_port;
eth_stat_t *eth_stat;
{
	acc_t *acc;
	int result;
	static message mess, mlocked;

#if DEBUG
 { where(); printf("eth_get_stat called\n"); }
#endif
	mess.m_type= DL_GETSTAT;
	mess.DL_PORT= eth_port->etp_osdep.etp_port;
	mess.DL_PROC= THIS_PROC;
	mess.DL_ADDR= (char *)eth_stat;

	for (;;)
	{
assert (eth_port->etp_osdep.etp_task != MM_PROC_NR);
		result= send(eth_port->etp_osdep.etp_task, &mess);
		if (result != ELOCKED)
			break;
		result= receive(eth_port->etp_osdep.etp_task, &mlocked);
assert(result == OK);
#if DEBUG
 { where(); printf("calling eth_rec()\n"); }
#endif
		eth_rec(&mlocked);
	}
assert(result == OK);

	result= receive(eth_port->etp_osdep.etp_task, &mess);
assert(result == OK);
assert(mess.m_type == DL_TASK_REPLY);

	result= mess.DL_STAT >> 16;
assert (result == 0);

	if (mess.DL_STAT)
	{
#if DEBUG
 { where(); printf("calling eth_rec()\n"); }
#endif
		eth_rec(&mess);
	}
	return OK;
}

PUBLIC void eth_set_rec_conf (eth_port, flags)
eth_port_t *eth_port;
u32_t flags;
{
	int result;
	unsigned dl_flags;
	static message mess, repl_mess;

#if DEBUG
 { where(); printf("eth_chk_rec_conf(&eth_port_table[%d])\n",
	eth_port-eth_port_table); }
#endif
	dl_flags= DL_NOMODE;
	if (flags & NWEO_EN_BROAD)
		dl_flags |= DL_BROAD_REQ;
	if (flags & NWEO_EN_MULTI)
		dl_flags |= DL_MULTI_REQ;
	if (flags & NWEO_EN_PROMISC)
		dl_flags |= DL_PROMISC_REQ;

	mess.m_type= DL_INIT;
	mess.DL_PORT= eth_port->etp_osdep.etp_port;
	mess.DL_PROC= THIS_PROC;
	mess.DL_MODE= dl_flags;

	do
	{
assert (eth_port->etp_osdep.etp_task != MM_PROC_NR);
		result= send (eth_port->etp_osdep.etp_task, &mess);
		if (result == ELOCKED)	/* etp_task is sending to this task,
					   I hope */
		{
			if (receive (eth_port->etp_osdep.etp_task, 
				&repl_mess)< 0)
				ip_panic(("unable to receive"));
#if DEBUG
 { where(); printf("calling eth_rec\n"); }
#endif
			eth_rec(&repl_mess);
		}
	} while (result == ELOCKED);
	
	if (result < 0)
		ip_panic(("unable to send(%d)", result));

	if (receive (eth_port->etp_osdep.etp_task, &repl_mess) < 0)
		ip_panic(("unable to receive"));

assert (repl_mess.m_type == DL_INIT_REPLY);
	if (repl_mess.m3_i1 != eth_port->etp_osdep.etp_port)
	{
		ip_panic(("got reply for wrong port"));
		return;
	}
}

PRIVATE int do_sendrec (tofrom, mptr1, mptr2)
int tofrom;
message *mptr1;
message *mptr2;
{
	int result;
	int extra;

assert (tofrom != MM_PROC_NR);
	result= send (tofrom, mptr1);
	if (result == ELOCKED)
	{
		/* ethernet task is sending to this task, I hope */
		result= receive(tofrom, mptr2);

		if (result < 0)
			ip_panic(("unable to receive"));
		extra= 1;
assert (tofrom != MM_PROC_NR);
		result= send (tofrom, mptr1);
	}
	else
		extra= 0;

	if (result < 0)
		ip_panic(("unable to send"));

	result= receive (tofrom, mptr1);
	if (result < 0)
		ip_panic(("unable to receive"));

assert (mptr1->m_type == DL_TASK_REPLY);
	return extra;
}

PRIVATE void write_int(eth_port)
eth_port_t *eth_port;
{
	acc_t *pack;

#if DEBUG & 256
 { where(); printf("write_int called\n"); }
#endif

assert(eth_port->etp_flags & (EPF_WRITE_IP|EPF_WRITE_SP) ==
	(EPF_WRITE_IP|EPF_WRITE_SP));

	pack= eth_port->etp_wr_pack;
	eth_port->etp_wr_pack= NULL;
	eth_arrive(eth_port, pack);
	eth_port->etp_flags &= ~(EPF_WRITE_IP|EPF_WRITE_SP);
	while (eth_get_work(eth_port))
		;
}

PRIVATE void read_int(eth_port, count)
eth_port_t *eth_port;
int count;
{
	acc_t *pack, *cut_pack;

	pack= eth_port->etp_rd_pack;
	eth_port->etp_rd_pack= NULL;

	cut_pack= bf_cut(pack, 0, count);
	bf_afree(pack);

	eth_arrive(eth_port, cut_pack);
	
	if (!(eth_port->etp_flags & EPF_READ_SP))
	{
		eth_port->etp_flags &= ~EPF_READ_IP;
		return;
	}
	eth_port->etp_flags &= ~(EPF_READ_IP|EPF_READ_SP);
	setup_read(eth_port);
}

PRIVATE void setup_read(eth_port)
eth_port_t *eth_port;
{
	acc_t *pack, *pack_ptr;
	static message mess1, mess2;
	iovec_t *iovec;
	int i, result;

assert(!(eth_port->etp_flags & (EPF_READ_IP|EPF_READ_SP)));

	do
	{

assert (!eth_port->etp_rd_pack);

		iovec= eth_port->etp_osdep.etp_rd_iovec;
		pack= bf_memreq (ETH_MAX_PACK_SIZE);

		for (i=0, pack_ptr= pack; i<RD_IOVEC && pack_ptr;
			i++, pack_ptr= pack_ptr->acc_next)
		{
			iovec[i].iov_addr= (vir_bytes)ptr2acc_data(pack_ptr);
			iovec[i].iov_size= (vir_bytes)pack_ptr->acc_length;
#if DEBUG & 256
 { where(); printf("filling iovec[%d] with iov_addr= %x, iov_size= %x\n",
	i, iovec[i].iov_addr, iovec[i].iov_size); }
#endif
		}

assert (!pack_ptr);

		mess1.m_type= DL_READV;
		mess1.DL_PORT= eth_port->etp_osdep.etp_port;
		mess1.DL_PROC= THIS_PROC;
		mess1.DL_COUNT= i;
		mess1.DL_ADDR= (char *)iovec;

		result= do_sendrec (eth_port->etp_osdep.etp_task, &mess1, 
			&mess2);

#if DEBUG
 if (mess1.m_type != DL_TASK_REPLY)
 { where(); printf("wrong m_type (=%d)\n", mess1.m_type); }
 if (mess1.DL_PORT != mess1.DL_PORT)
 { where(); printf("wrong DL_PORT (=%d)\n", mess1.DL_PORT); }
 if (mess1.DL_PROC != THIS_PROC)
 { where(); printf("wrong DL_PROC (=%d)\n", mess1.DL_PROC); }
#endif

assert (mess1.m_type == DL_TASK_REPLY && mess1.DL_PORT == mess1.DL_PORT &&
	mess1.DL_PROC == THIS_PROC);
compare((mess1.DL_STAT >> 16), ==, OK);

		if (mess1.DL_STAT & DL_PACK_RECV)
		/* packet received */
		{
			pack_ptr= bf_cut(pack, 0, mess1.DL_COUNT);
			bf_afree(pack);

assert(!(eth_port->etp_flags & EPF_READ_IP));
			eth_arrive(eth_port, pack_ptr);
assert(!(eth_port->etp_flags & EPF_READ_IP));
		}
		else
		/* no packet received */
		{
			eth_port->etp_rd_pack= pack;
			eth_port->etp_flags |= EPF_READ_IP;
		}

		if (result == 1)	/* got an INT_TASK */
		{
assert(mess2.DL_STAT == DL_PACK_SEND);
assert(!(mess1.DL_STAT & DL_PACK_SEND));
assert (mess2.DL_PORT == mess2.DL_PORT &&
	mess2.DL_PROC == THIS_PROC);
			write_int(eth_port);
		}
		else if (mess1.DL_STAT & DL_PACK_SEND)
		{
			write_int(eth_port);
		}
	} while (!(eth_port->etp_flags & EPF_READ_IP));
	eth_port->etp_flags |= EPF_READ_SP;
}
