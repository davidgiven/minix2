/*	this file contains the interface of the network software with the file
	system.

The valid messages and their parameters are:

 __________________________________________________________________
|		|           |         |       |          |         |
| m_type	|   DEVICE  | PROC_NR |	COUNT |	REQUEST  | ADDRESS |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_OPEN 	| minor dev | proc nr | mode  |          |         |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_CLOSE 	| minor dev | proc nr |       |          |         |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_IOCTL	| minor dev | proc nr |       |	NWIO..	 | address |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_READ	| minor dev | proc nr |	count |          | address |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_WRITE	| minor dev | proc nr |	count |          | address |
|_______________|___________|_________|_______|__________|_________|
|		|           |         |       |          |         |
| NW_CANCEL	| minor dev | proc nr |       |          |         |
|_______________|___________|_________|_______|__________|_________|

*/

#include "inet.h"
#include <minix/callnr.h>
#include "mq.h"
#include "proto.h"
#include "generic/assert.h"
#include "generic/buf.h"
#include "generic/sr.h"
#include "generic/type.h"

INIT_PANIC();

#define FD_NR			32

typedef struct sr_fd
{
	int srf_flags;
	int srf_fd;
	int srf_port;
	sr_open_t srf_open;
	sr_close_t srf_close;
	sr_write_t srf_write;
	sr_read_t srf_read;
	sr_ioctl_t srf_ioctl;
	sr_cancel_t srf_cancel;
	mq_t *srf_ioctl_q, *srf_ioctl_q_tail;
	mq_t *srf_read_q, *srf_read_q_tail;
	mq_t *srf_write_q, *srf_write_q_tail;
} sr_fd_t;

#define SFF_FLAGS 	0x0F
#	define SFF_FREE		0x00
#	define SFF_MINOR	0x01
#	define SFF_INUSE	0x02
#	define SFF_BUSY		0x3C
#		define SFF_IOCTL_IP	0x04
#		define SFF_READ_IP	0x08
#		define SFF_WRITE_IP	0x10
#	define SFF_PENDING_REQ	0x30
#	define SFF_SUSPENDED	0x1C0
#		define SFF_IOCTL_SUSP	0x40
#		define SFF_READ_SUSP	0x80
#		define SFF_WRITE_SUSP	0x100

FORWARD _PROTOTYPE ( int sr_open, (message *m) );
FORWARD _PROTOTYPE ( void sr_close, (message *m) );
FORWARD _PROTOTYPE ( int sr_rwio, (mq_t *m) );
FORWARD _PROTOTYPE ( int sr_cancel, (message *m) );
FORWARD _PROTOTYPE ( void sr_reply, (message *mes_ptr, int reply) );
FORWARD _PROTOTYPE ( void sr_revive, (mq_t *mes_ptr, int reply) );
FORWARD _PROTOTYPE ( sr_fd_t *sr_getchannel, (int minor));
FORWARD _PROTOTYPE ( acc_t *sr_get_userdata, (int fd, vir_bytes offset,
					vir_bytes count, int for_ioctl) );
FORWARD _PROTOTYPE ( int sr_put_userdata, (int fd, vir_bytes offset,
						acc_t *data, int for_ioctl) );
FORWARD _PROTOTYPE ( int sr_repl_queue, (int proc) );
FORWARD _PROTOTYPE ( int walk_queue, (sr_fd_t *sr_fd, mq_t *q_head, 
				mq_t **q_tail_ptr, int type, int proc_n) );
FORWARD _PROTOTYPE ( void process_req_q, (mq_t *mq, mq_t *tail, 
							mq_t **tail_ptr) );
FORWARD _PROTOTYPE ( int cp_u2b, (int proc, char *src, acc_t **var_acc_ptr,
								 int size) );
FORWARD _PROTOTYPE ( int cp_b2u, (acc_t *acc_ptr, int proc, char *dest) );

PRIVATE sr_fd_t sr_fd_table[FD_NR];
PRIVATE mq_t *repl_queue, *repl_queue_tail;
PRIVATE cpvec_t cpvec[CPVEC_NR];

PUBLIC void sr_init()
{
	int i;

	for (i=0; i<FD_NR; i++)
		sr_fd_table[i].srf_flags= SFF_FREE;
	repl_queue= NULL;
}

PUBLIC void sr_rec(m)
mq_t *m;
{
	message mess, *mp;
	int result;
	int send_reply, free_mess;

	if (repl_queue)
	{
		if (m->mq_mess.m_type == NW_CANCEL)
		{
			result= sr_repl_queue(m->mq_mess.PROC_NR);
			if (result)
				return;	/* canceled request in queue */
		}
		else
			sr_repl_queue(ANY);
	}

#if DEBUG & 256
 { where(); printf("sr_rec: message from %d for %d type %d, minor %d\n",
	m->mq_mess.m_source, m->mq_mess.PROC_NR, m->mq_mess.m_type, 
	m->mq_mess.DEVICE); }
#endif
	switch (m->mq_mess.m_type)
	{
	case NW_OPEN:
		result= sr_open(&m->mq_mess);
		send_reply= 1;
		free_mess= 1;
		break;
	case NW_CLOSE:
		sr_close(&m->mq_mess);
		result= OK;
		send_reply= 1;
		free_mess= 1;
		break;
	case NW_READ:
	case NW_WRITE:
	case NW_IOCTL:
#if DEBUG & 256
 { where(); printf("calling rwio\n"); }
#endif
		result= sr_rwio(m);
#if DEBUG & 256
 { where(); printf("rwio()= %d\n", result); }
#endif
assert(result == OK || result == SUSPEND);
		send_reply= (result == SUSPEND);
		free_mess= 0;
		break;
	case NW_CANCEL:
		result= sr_cancel(&m->mq_mess);
assert(result == OK || result == EINTR);
		send_reply= (result == EINTR);
		free_mess= 1;
		break;
	default:
		ip_panic(("unknown message, type= %d", m->mq_mess.m_type));
	}
	if (send_reply)
	{
		if (free_mess)
			mp= &m->mq_mess;
		else
		{
			mess= m->mq_mess;
			mp= &mess;
		}
		sr_reply(mp, result);
	}
	if (free_mess)
		mq_free(m);
}

PUBLIC int sr_add_minor(minor, port, openf, closef, readf, writef,
	ioctlf, cancelf)
int minor;
int port;
sr_open_t openf;
sr_close_t closef;
sr_read_t readf;
sr_write_t writef;
sr_ioctl_t ioctlf;
sr_cancel_t cancelf;
{
	sr_fd_t *sr_fd;

	assert (minor>=0 && minor<FD_NR);

	sr_fd= &sr_fd_table[minor];

	if (sr_fd->srf_flags & SFF_INUSE)
		return EGENERIC;

	sr_fd->srf_flags= SFF_INUSE | SFF_MINOR;
	sr_fd->srf_port= port;
	sr_fd->srf_open= openf;
	sr_fd->srf_close= closef;
	sr_fd->srf_write= writef;
	sr_fd->srf_read= readf;
	sr_fd->srf_ioctl= ioctlf;
	sr_fd->srf_cancel= cancelf;

	return OK;
}

PRIVATE int sr_open(m)
message *m;
{
	sr_fd_t *sr_fd;

	int minor= m->DEVICE;
	int i, fd;

	if (minor<0 || minor>FD_NR)
	{
#if DEBUG
 { where(); printf("replying EINVAL\n"); }
#endif
		return EINVAL;
	}
	if (!(sr_fd_table[minor].srf_flags & SFF_MINOR))
	{
#if DEBUG
 { where(); printf("replying ENXIO\n"); }
#endif
		return ENXIO;
	}
	for (i=0; i<FD_NR && (sr_fd_table[i].srf_flags & SFF_INUSE); i++);

	if (i>=FD_NR)
	{
#if DEBUG
 { where(); printf("replying ENFILE\n"); }
#endif
		return ENFILE;
	}

	sr_fd= &sr_fd_table[i];
	*sr_fd= sr_fd_table[minor];
	sr_fd->srf_flags= SFF_INUSE;
	fd= (*sr_fd->srf_open)(sr_fd->srf_port, i, sr_get_userdata,
		sr_put_userdata);
#if DEBUG & 256
 { where(); printf("srf_open: 0x%x(%d, %d, .., ..)= %d\n", sr_fd->srf_open,
	sr_fd->srf_port, i, fd); }
#endif
	if (fd<0)
	{
		sr_fd->srf_flags= SFF_FREE;
#if DEBUG
 { where(); printf("replying %d\n", fd); }
#endif
		return fd;
	}
	sr_fd->srf_fd= fd;
#if DEBUG & 256
 { where(); printf("replying %d\n", i); }
#endif
	return i;
}

PRIVATE void sr_close(m)
message *m;
{
	sr_fd_t *sr_fd;

	sr_fd= sr_getchannel(m->DEVICE);
	assert (sr_fd);

	if (sr_fd->srf_flags & SFF_BUSY)
		ip_panic(("close on busy channel"));

	assert (!(sr_fd->srf_flags & SFF_MINOR));
	(*sr_fd->srf_close)(sr_fd->srf_fd);
#if DEBUG & 256
 { where(); printf("srf_close: 0x%x(%d)\n", sr_fd->srf_close, sr_fd->srf_fd); }
#endif
	sr_fd->srf_flags= SFF_FREE;
}

PRIVATE int sr_rwio(m)
mq_t *m;
{
	sr_fd_t *sr_fd;
	mq_t **q_head_ptr, **q_tail_ptr;
	int ip_flag, susp_flag;
	int result;
	unsigned long request;
	size_t size;

	sr_fd= sr_getchannel(m->mq_mess.DEVICE);
assert (sr_fd);

	switch(m->mq_mess.m_type)
	{
	case NW_READ:
		q_head_ptr= &sr_fd->srf_read_q;
		q_tail_ptr= &sr_fd->srf_read_q_tail;
		ip_flag= SFF_READ_IP;
		susp_flag= SFF_READ_SUSP;
		break;
	case NW_WRITE:
		q_head_ptr= &sr_fd->srf_write_q;
		q_tail_ptr= &sr_fd->srf_write_q_tail;
		ip_flag= SFF_WRITE_IP;
		susp_flag= SFF_WRITE_SUSP;
		break;
	case NW_IOCTL:
		q_head_ptr= &sr_fd->srf_ioctl_q;
		q_tail_ptr= &sr_fd->srf_ioctl_q_tail;
		ip_flag= SFF_IOCTL_IP;
		susp_flag= SFF_IOCTL_SUSP;
		break;
	default:
		ip_panic(("illegal case entry"));
	}

	if (sr_fd->srf_flags & ip_flag)
	{
assert(sr_fd->srf_flags & susp_flag);
assert(*q_head_ptr);
		(*q_tail_ptr)->mq_next= m;
		*q_tail_ptr= m;
		return SUSPEND;
	}
assert(!*q_head_ptr);

	*q_tail_ptr= *q_head_ptr= m;
	sr_fd->srf_flags |= ip_flag;

	switch(m->mq_mess.m_type)
	{
	case NW_READ:
#if DEBUG&256
 { where(); printf("calling 0x%x(%d, %d)\n", sr_fd->srf_read, sr_fd->srf_fd,
	m->mq_mess.COUNT); }
#endif
		result= (*sr_fd->srf_read)(sr_fd->srf_fd, m->mq_mess.COUNT);
		break;
	case NW_WRITE:
#if DEBUG&256
 { where(); printf("calling 0x%x(%d, %d)\n", sr_fd->srf_write, sr_fd->srf_fd,
	m->mq_mess.COUNT); }
#endif
		result= (*sr_fd->srf_write)(sr_fd->srf_fd, m->mq_mess.COUNT);
		break;
	case NW_IOCTL:
		request= m->mq_mess.REQUEST;
#ifdef IOCPARM_MASK
		size= (request >> 16) & IOCPARM_MASK;
		if (size>MAX_IOCTL_S)
		{
#if DEBUG
 { where(); printf("replying EINVAL\n"); }
#endif
			result= sr_put_userdata(sr_fd-sr_fd_table, EINVAL, 
								NULL, 1);
assert(result == OK);
			return OK;
		}
#endif /* IOCPARM_MASK */
#if DEBUG
 { where(); printf("calling 0x%x(%d, 0x%lx)\n", sr_fd->srf_ioctl, sr_fd->srf_fd,
	request); }
#endif
		result=(*sr_fd->srf_ioctl)(sr_fd->srf_fd, request);
		break;
	default:
		ip_panic(("illegal case entry"));
	}

#if DEBUG
 if (result != OK && result != SUSPEND)
 { where(); printf("result= %d\n", result); }
#endif
assert(result == OK || result == SUSPEND);
	if (result == SUSPEND)
		sr_fd->srf_flags |= susp_flag;
	return result;
}

PRIVATE int sr_cancel(m)
message *m;
{
	sr_fd_t *sr_fd;
	int i, result;
	mq_t *q_ptr, *q_ptr_prv;
	int proc_nr;

        result=EINTR;
	proc_nr=  m->PROC_NR;
	sr_fd= sr_getchannel(m->DEVICE);
assert (sr_fd);

	result= walk_queue(sr_fd, sr_fd->srf_ioctl_q, &sr_fd->srf_ioctl_q_tail, 
		SR_CANCEL_IOCTL, proc_nr);
	if (result != EAGAIN)
		return result;
	result= walk_queue(sr_fd, sr_fd->srf_read_q, &sr_fd->srf_read_q_tail, 
		SR_CANCEL_READ, proc_nr);
	if (result != EAGAIN)
		return result;
	result= walk_queue(sr_fd, sr_fd->srf_write_q, &sr_fd->srf_write_q_tail, 
		SR_CANCEL_WRITE, proc_nr);
	if (result != EAGAIN)
		return result;
	ip_panic(("request not found"));
}

PRIVATE int walk_queue(sr_fd, q_head, q_tail_ptr, type, proc_nr)
sr_fd_t *sr_fd;
mq_t *q_head, **q_tail_ptr;
int type;
int proc_nr;
{
	mq_t *q_ptr_prv, *q_ptr;
	int result;

	for(q_ptr_prv= NULL, q_ptr= q_head; q_ptr; 
		q_ptr_prv= q_ptr, q_ptr= q_ptr->mq_next)
	{
		if (q_ptr->mq_mess.PROC_NR != proc_nr)
			continue;
		if (!q_ptr_prv)
		{
#if DEBUG & 256
 { where(); printf("calling 0x%x(%d, %d)\n", sr_fd->srf_cancel, sr_fd->srf_fd,
	type); }
#endif

			result= (*sr_fd->srf_cancel)(sr_fd->srf_fd, type);
assert(result == OK);
			return OK;
		}
		q_ptr_prv->mq_next= q_ptr->mq_next;
		mq_free(q_ptr);
		if (!q_ptr_prv->mq_next)
			*q_tail_ptr= q_ptr_prv;
		return EINTR;
	}
	return EAGAIN;
}

PRIVATE sr_fd_t *sr_getchannel(minor)
int minor;
{
	sr_fd_t *loc_fd;

compare(minor, >=, 0);
compare(minor, <, FD_NR);

	loc_fd= &sr_fd_table[minor];

#if DEBUG
 if ((loc_fd->srf_flags & SFF_MINOR) || !(loc_fd->srf_flags & SFF_INUSE))
 { where(); printf("got req for ill minor (= %d)\n", minor); }
#endif
assert (!(loc_fd->srf_flags & SFF_MINOR) && (loc_fd->srf_flags & SFF_INUSE));

	return loc_fd;
}

PRIVATE void sr_reply (mess_ptr, status)
message *mess_ptr;
int status;
{
	static message reply;
	int result;

#if DEBUG & 256
 { where(); printf("replying %d to %d for proc %d\n", status, 
	mess_ptr->m_source, mess_ptr->PROC_NR); }
#endif
	reply.m_type= REVIVE;	/* There no use for TASK_REPLY */
	reply.REP_PROC_NR= mess_ptr->PROC_NR;
	reply.REP_STATUS= status;
#if DEBUG & 256
 { where(); printf("sending %d to %d for %d\n", reply.m_type,
	mess_ptr->m_source, reply.REP_PROC_NR); }
#endif
assert(mess_ptr->m_source != MM_PROC_NR);
	result= send (mess_ptr->m_source, &reply);
	if (result != OK)
		ip_panic(("unable to send"));
}

PRIVATE acc_t *sr_get_userdata (fd, offset, count, for_ioctl)
int fd;
vir_bytes offset;
vir_bytes count;
int for_ioctl;
{
	sr_fd_t *loc_fd;
	mq_t **head_ptr, **tail_ptr, *m, *tail, *mq;
	int ip_flag, susp_flag;
	int result;
	int suspended;
	char *src;
	acc_t *acc;

#if DEBUG & 256
 { where(); printf("sr_get_userdata(%d, %u, %u, %d)\n",
	fd, offset, count, for_ioctl); }
#endif
	loc_fd= &sr_fd_table[fd];

	if (for_ioctl)
	{
		head_ptr= &loc_fd->srf_ioctl_q;
		tail_ptr= &loc_fd->srf_ioctl_q_tail;
		ip_flag= SFF_IOCTL_IP;
		susp_flag= SFF_IOCTL_SUSP;
	}
	else
	{
		head_ptr= &loc_fd->srf_write_q;
		tail_ptr= &loc_fd->srf_write_q_tail;
		ip_flag= SFF_WRITE_IP;
		susp_flag= SFF_WRITE_SUSP;
	}
		
assert (loc_fd->srf_flags & ip_flag);

	if (!count)
	{
		m= *head_ptr;
		*head_ptr= NULL;
		tail= *tail_ptr;
assert(m);
		mq= m->mq_next;
		result= (int)offset;
		sr_revive (m, result);
		suspended= (loc_fd->srf_flags & susp_flag);
		loc_fd->srf_flags &= ~(ip_flag|susp_flag);
		if (suspended)
		{
			process_req_q(mq, tail, tail_ptr);
		}
		else
		{
assert(!mq);
		}
		return NULL;
	}

	src= (*head_ptr)->mq_mess.ADDRESS + offset;
	result= cp_u2b ((*head_ptr)->mq_mess.PROC_NR, src, &acc, count);

	return result<0 ? NULL : acc;
}

PRIVATE int sr_put_userdata (fd, offset, data, for_ioctl)
int fd;
vir_bytes offset;
acc_t *data;
int for_ioctl;
{
	sr_fd_t *loc_fd;
	mq_t **head_ptr, **tail_ptr, *m, *tail, *mq;
	int ip_flag, susp_flag;
	int result;
	int suspended;
	char *dst;

#if DEBUG & 256
 { where(); printf("sr_put_userdata(%d, %u, 0x%x, %d)\n",
	fd, offset, data, for_ioctl); }
#endif

	loc_fd= &sr_fd_table[fd];

	if (for_ioctl)
	{
		head_ptr= &loc_fd->srf_ioctl_q;
		tail_ptr= &loc_fd->srf_ioctl_q_tail;
		ip_flag= SFF_IOCTL_IP;
		susp_flag= SFF_IOCTL_SUSP;
	}
	else
	{
		head_ptr= &loc_fd->srf_read_q;
		tail_ptr= &loc_fd->srf_read_q_tail;
		ip_flag= SFF_READ_IP;
		susp_flag= SFF_READ_SUSP;
	}
		
assert (loc_fd->srf_flags & ip_flag);

	if (!data)
	{
		m= *head_ptr;
		*head_ptr= NULL;
		tail= *tail_ptr;
assert(m);
		mq= m->mq_next;
		result= (int)offset;
		sr_revive (m, result);
		suspended= (loc_fd->srf_flags & susp_flag);
		loc_fd->srf_flags &= ~(ip_flag|susp_flag);
		if (suspended)
		{
			process_req_q(mq, tail, tail_ptr);
		}
		else
		{
assert(!mq);
		}
		return OK;
	}

	dst= (*head_ptr)->mq_mess.ADDRESS + offset;
	return cp_b2u (data, (*head_ptr)->mq_mess.PROC_NR, dst);
}

PRIVATE void sr_revive (m, status)
mq_t *m;
int status;
{
	static message reply;
	int result;

#if DEBUG & 256
 { where(); printf("sr_revive: replying %d to %d for proc %d\n", status,
	m->mq_mess.m_source, m->mq_mess.PROC_NR); }
#endif
	reply.m_type= REVIVE;
	reply.REP_PROC_NR= m->mq_mess.PROC_NR;
	reply.REP_STATUS= status;
#if DEBUG & 256
 { where(); printf("sending %d to %d for %d\n", reply.m_type,
   m->mq_mess.m_source, reply.REP_PROC_NR); }
#endif
assert(m->mq_mess.m_source != MM_PROC_NR);
	result= send (m->mq_mess.m_source, &reply);
	if (result<0)
	{
		if (result == ELOCKED)
		{
#if DEBUG
 { where(); printf("send locked\n"); }
#endif
			reply.m_source= m->mq_mess.m_source;
			m->mq_mess= reply;
			if (repl_queue)
				repl_queue_tail->mq_next= m;
			else
				repl_queue= m;
			repl_queue_tail= m;
			return;
		}
	else
		ip_panic(("unable to send"));
	}
	mq_free(m);
}

PRIVATE void process_req_q(mq, tail, tail_ptr)
mq_t *mq, *tail, **tail_ptr;
{
	mq_t *m;
	int result;

	for(;mq;)
	{
		m= mq;
		mq= mq->mq_next;

#if DEBUG
 { where(); printf("calling rwio\n"); }
#endif
		result= sr_rwio(m);
		if (result == SUSPEND)
		{
			if (mq)
			{
				(*tail_ptr)->mq_next= mq;
				*tail_ptr= tail;
			}
			return;
		}
	}
	return;
}

PRIVATE int cp_u2b (proc, src, var_acc_ptr, size)
int proc;
char *src;
acc_t **var_acc_ptr;
int size;
{
	static message mess;
	acc_t *acc;
	int i;

	acc= bf_memreq(size);
	*var_acc_ptr= acc;
	i=0;

	while (acc)
	{
		size= (vir_bytes)acc->acc_length;

		cpvec[i].cpv_src= (vir_bytes)src;
		cpvec[i].cpv_dst= (vir_bytes)ptr2acc_data(acc);
		cpvec[i].cpv_size= size;

		src += size;
		acc= acc->acc_next;
		i++;

		if (i == CPVEC_NR)
		{
			mess.m_type= SYS_VCOPY;
			mess.m1_i1= proc;
			mess.m1_i2= THIS_PROC;
			mess.m1_i3= i;
			mess.m1_p1= (char *)cpvec;
			if (sendrec(SYSTASK, &mess) <0)
				ip_panic(("unable to sendrec"));
			if (mess.m_type <0)
			{
				bf_afree(*var_acc_ptr);
				*var_acc_ptr= 0;
				return mess.m_type;
			}
			i= 0;
		}
	}
	if (i)
	{
		mess.m_type= SYS_VCOPY;
		mess.m1_i1= proc;
		mess.m1_i2= THIS_PROC;
		mess.m1_i3= i;
		mess.m1_p1= (char *)cpvec;
		if (sendrec(SYSTASK, &mess) <0)
			ip_panic(("unable to sendrec"));
		if (mess.m_type <0)
		{
			bf_afree(*var_acc_ptr);
			*var_acc_ptr= 0;
			return mess.m_type;
		}
	}
	return OK;
}

PRIVATE int cp_b2u (acc_ptr, proc, dest)
acc_t *acc_ptr;
int proc;
char *dest;
{
	static message mess;
	acc_t *acc;
	int i, size;

	acc= acc_ptr;
	i=0;

	while (acc)
	{
		size= (vir_bytes)acc->acc_length;

		if (size)
		{
			cpvec[i].cpv_src= (vir_bytes)ptr2acc_data(acc);
			cpvec[i].cpv_dst= (vir_bytes)dest;
			cpvec[i].cpv_size= size;
			i++;
		}

		dest += size;
		acc= acc->acc_next;

		if (i == CPVEC_NR)
		{
			mess.m_type= SYS_VCOPY;
			mess.m1_i1= THIS_PROC;
			mess.m1_i2= proc;
			mess.m1_i3= i;
			mess.m1_p1= (char *)cpvec;
			if (sendrec(SYSTASK, &mess) <0)
				ip_panic(("unable to sendrec"));
			if (mess.m_type <0)
			{
				bf_afree(acc_ptr);
				return mess.m_type;
			}
			i= 0;
		}
	}
	if (i)
	{
		mess.m_type= SYS_VCOPY;
		mess.m1_i1= THIS_PROC;
		mess.m1_i2= proc;
		mess.m1_i3= i;
		mess.m1_p1= (char *)cpvec;
		if (sendrec(SYSTASK, &mess) <0)
			ip_panic(("unable to sendrec"));
		if (mess.m_type <0)
		{
			bf_afree(acc_ptr);
			return mess.m_type;
		}
	}
	bf_afree(acc_ptr);
	return OK;
}

PRIVATE int sr_repl_queue(proc)
int proc;
{
	mq_t *m, *m_cancel, *tmp;
	int result;

	m_cancel= NULL;

	for (m= repl_queue; m;)
	{
		if (m->mq_mess.REP_PROC_NR == proc)
		{
assert(!m_cancel);
			m_cancel= m;
			m= m->mq_next;
			continue;
		}
assert(m->mq_mess.m_source != MM_PROC_NR);
		result= send(m->mq_mess.m_source, &m->mq_mess);
		if (result != OK)
			ip_panic(("unable to send: %d", result));
		tmp= m;
		m= m->mq_next;
		mq_free(tmp);
	}
	repl_queue= NULL;
	if (m_cancel)
	{
assert(m_cancel->mq_mess.m_source != MM_PROC_NR);
		result= send(m_cancel->mq_mess.m_source, &m_cancel->mq_mess);
		if (result != OK)
			ip_panic(("unable to send: %d", result));
		mq_free(m_cancel);
		return 1;
	}
	return 0;
}
