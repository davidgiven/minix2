/*
inet/mq.h

Created:	Jan 3, 1992 by Philip Homburg
*/

#ifndef INET__MQ_H
#define INET__MQ_H

typedef struct mq
{
	message mq_mess;
	struct mq *mq_next;
} mq_t;

_PROTOTYPE( mq_t *mq_get, (void) );
_PROTOTYPE( void mq_free, (mq_t *mq) );
_PROTOTYPE( void mq_init, (void) );

#endif /* INET__MQ_H */
