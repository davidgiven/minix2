/*
buf.h
*/

#ifndef BUF_H
#define BUF_H

#ifndef BUF_S
#define BUF_S		32768
#endif

#define MAX_BUFREQ_PRI	10

#define ETH_PRI_PORTBUFS	3
#define ETH_PRI_EXP_FDBUFS	4
#define ETH_PRI_FDBUFS		5

#define ICMP_PRI_QUEUE		1

#define TCP_PRI_FRAG2SEND	4
#define TCP_PRI_CONNwoUSER	6
#define TCP_PRI_CONN_INUSE	9

#define UDP_PRI_EXP_FDBUFS	5
#define UDP_PRI_FDBUFS		6

struct buf;
typedef void (*buffree_t) ARGS(( struct buf *buffer ));
typedef void (*bf_freereq_t) ARGS(( int priority, size_t reqsize ));

typedef struct buf
{
	int buf_linkC;
	void *buf_next;
	buffree_t buf_free;
	size_t buf_size;
	char *buf_data_p;
} buf_t;

typedef struct acc
{
	int acc_linkC;
	int acc_offset, acc_length;
	buf_t *acc_buffer;
	struct acc *acc_next, *acc_ext_link;
} acc_t;

extern size_t bf_free_buffsize;
extern acc_t *bf_temporary_acc;

/* For debugging... */

#if DEBUG & 256
#define bf_memreq(a) (bf_memreq_file= __FILE__, bf_memreq_line= __LINE__, \
	 _bf_memreq(a))
#endif

#if DEBUG & 256
#define bf_cut(a,b,c) (bf_cut_file= __FILE__, bf_cut_line= __LINE__, \
	 _bf_cut(a,b,c))
#endif

#if DEBUG & 256
#define bf_packIffLess(a,b) (bf_pack_file= __FILE__, bf_pack_line= __LINE__, \
	 _bf_packIffLess(a,b))
#endif

#if DEBUG & 256
#define bf_bufsize(a) (bf_bufsize_file= __FILE__, bf_bufsize_line= __LINE__, \
	 _bf_bufsize(a))
#endif

#ifdef bf_memreq
extern char *bf_memreq_file;
extern int bf_memreq_line;
#endif
#ifdef bf_cut
extern char *bf_cut_file;
extern int bf_cut_line;
#endif
#ifdef bf_packIffLess
extern char *bf_pack_file;
extern int bf_pack_line;
#endif
#ifdef bf_bufsize
extern char *bf_bufsize_file;
extern int bf_bufsize_line;
#endif

/* Prototypes */

void bf_init ARGS(( void ));
void bf_logon ARGS(( bf_freereq_t func ));

#ifndef bf_memreq
acc_t *bf_memreq ARGS(( unsigned size));
#else
acc_t *_bf_memreq ARGS(( unsigned size));
#endif
/* the result is an acc with linkC == 1 */

acc_t *bf_dupacc ARGS(( acc_t *acc ));
/* the result is an acc with linkC == 1 identical to the given one */

void bf_afree ARGS(( acc_t *acc));
/* this reduces the linkC off the given acc with one */

acc_t *bf_pack ARGS(( acc_t *pack));
/* this gives a packed copy of the given acc, the linkC of the given acc is
   reduced by one, the linkC of the result == 1 */

#ifndef bf_packIffLess
acc_t *bf_packIffLess ARGS(( acc_t *pack, int min_len ));
#else
acc_t *_bf_packIffLess ARGS(( acc_t *pack, int min_len ));
#endif
/* this performs a bf_pack iff pack->acc_length<min_len */

#ifndef bf_bufsize
size_t bf_bufsize ARGS(( acc_t *pack));
#else
size_t _bf_bufsize ARGS(( acc_t *pack));
#endif
/* this gives the length of the buffer specified by the given acc. The linkC
   of the given acc remains the same */

#ifndef bf_cut
acc_t *bf_cut ARGS(( acc_t *data, unsigned offset,
	unsigned length ));
#else
acc_t *_bf_cut ARGS(( acc_t *data, unsigned offset,
	unsigned length ));
#endif
/* the result is a cut of the buffer from offset with length length.
   The linkC of the result == 1, the linkC of the given acc remains the
   same. */

acc_t *bf_append ARGS(( acc_t *data_first, acc_t  *data_second ));
/* data_second is appended after data_first, a link is returned to the
	result and the linkCs of data_first and data_second are reduced.
	further more, if the contents of the last part of data_first and
	the first part of data_second fit in a buffer, both parts are
	copied into a (possibly fresh) buffer
*/

#define ptr2acc_data(/* acc_t * */ a) (bf_temporary_acc=(a), \
	(&bf_temporary_acc->acc_buffer->buf_data_p[bf_temporary_acc-> \
		acc_offset]))

#define bf_chkbuf(buf) ((buf)? (assert((buf)->acc_linkC>0), \
	assert((buf)->acc_buffer), \
	assert((buf)->acc_buffer->buf_linkC>0)) : 0)

void bf_check_all_bufs ARGS(( void ));
/* try to get all buffers back for debug purposes */


#endif /* BUF_H */
