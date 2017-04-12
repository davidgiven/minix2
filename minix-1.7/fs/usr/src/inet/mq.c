/*
inet/mq.c

Created:	Jan 3, 1992 by Philip Homburg
*/

#include "inet.h"
#include "mq.h"

#define MQ_SIZE		64

PRIVATE mq_t mq_list[MQ_SIZE];
PRIVATE mq_t *mq_freelist;

void mq_init()
{
	int i;

	mq_freelist= NULL;
	for (i= 0; i<MQ_SIZE; i++)
	{
		mq_list[i].mq_next= mq_freelist;
		mq_freelist= &mq_list[i];
	}
}

mq_t *mq_get()
{
	mq_t *mq;

	mq= mq_freelist;
	if (mq)
	{
		mq_freelist= mq->mq_next;
		mq->mq_next= NULL;
	}
	return mq;
}

void mq_free(mq)
mq_t *mq;
{
	mq->mq_next= mq_freelist;
	mq_freelist= mq;
}
