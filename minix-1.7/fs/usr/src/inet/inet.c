/*	this file contains the interface of the network software with rest of
	minix. Furthermore it contains the main loop of the network task.

The valid messages and their parameters are:

from FS:
 __________________________________________________________________
|		|           |         |       |          |         |
| m_type	|   DEVICE  | PROC_NR |	COUNT |	POSITION | ADDRESS |
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

from DL_ETH:
 _______________________________________________________________________
|		|           |         |          |            |         |
| m_type	|  DL_PORT  | DL_PROC |	DL_COUNT |  DL_STAT   | DL_TIME |
|_______________|___________|_________|__________|____________|_________|
|		|           |         |          |            |         |
| DL_TASK_INT 	| minor dev | proc nr | rd_count |  0  | stat |  time   |
|_______________|___________|_________|__________|____________|_________|
|		|           |         |          |            |         |
| DL_TASK_REPLY	| minor dev | proc nr | rd_count | err | stat |  time   |         |
|_______________|___________|_________|__________|____________|_________|
*/

#include "inet.h"
#include <unistd.h>
#include "mq.h"
#include "proto.h"
#include "generic/assert.h"
#include "generic/arp.h"
#include "generic/buf.h"
#include "generic/clock.h"
#include "generic/eth.h"
#include "generic/ip.h"
#include "generic/sr.h"
#include "generic/tcp.h"
#include "generic/type.h"
#include "generic/udp.h"
#include "config.h"

INIT_PANIC();

_PROTOTYPE( void main, (void) );

FORWARD _PROTOTYPE( void nw_init, (void) );

PUBLIC void main()
{
	mq_t *mq;
	int result;

	nw_init();
	while (TRUE)
	{
		mq= mq_get();
		if (!mq)
			ip_panic(("out of messages"));

		result= receive (ANY, &mq->mq_mess);
		if (result<0)
		{
			ip_panic(("unable to receive: %d", result));
		}
		reset_time();
#if DEBUG & 256
 { where(); printf("got message from %d, type %d\n",
	mq->mq_mess.m_source, mq->mq_mess.m_type); }
#endif
		switch (mq->mq_mess.m_source)
		{
		case FS_PROC_NR:
#if DEBUG & 256
 { where(); printf("got message from fs, type %d\n", mq->mq_mess.m_type); }
#endif
			sr_rec(mq);
			break;
		case DL_ETH:
#if DEBUG & 256
 { where(); printf("calling eth_rec\n"); }
#endif
			eth_rec(&mq->mq_mess);
			mq_free(mq);
			break;
		case SYN_ALRM_TASK:
			clck_tick (&mq->mq_mess);
			mq_free(mq);
			break;		
		default:
			ip_panic(("message from unknown source: %d",
				mq->mq_mess.m_source));
		}
	}
	ip_panic(("task is not allowed to terminate"));
}

PRIVATE void nw_init()
{
#if DEBUG & 256
 { where(); printf("starting mq_init()\n"); }
#endif
	mq_init();
#if DEBUG & 256
 { where(); printf("starting bf_init()\n"); }
#endif
	bf_init();
#if DEBUG & 256
 { where(); printf("starting clck_init()\n"); }
#endif
	clck_init();
#if DEBUG & 256
 { where(); printf("starting sr_init()\n"); }
#endif
	sr_init();
#if DEBUG & 256
 { where(); printf("starting eth_init()\n"); }
#endif
	eth_init();
#if DEBUG & 256
 { where(); printf("starting arp_init()\n"); }
#endif
#if ENABLE_ARP
	arp_init();
#endif
#if DEBUG & 256
 { where(); printf("starting ip_init()\n"); }
#endif
#if ENABLE_IP
	ip_init();
#endif
#if DEBUG & 256
 { where(); printf("starting tcp_init()\n"); }
#endif
#if ENABLE_TCP
	tcp_init();
#endif
#if DEBUG & 256
 { where(); printf("starting udp_init()\n"); }
#endif
#if ENABLE_UDP
	udp_init();
#endif
}

void abort()
{
	sys_abort(RBT_PANIC);
}
