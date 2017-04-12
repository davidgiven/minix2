/*
This file contains routines for buffer management.
*/

#include "inet.h"

#include <stdlib.h>
#include <string.h>

#include "generic/assert.h"
#include "generic/buf.h"
#include "generic/type.h"

INIT_PANIC();

#if TRACE_ENQUEUE_PROBLEM
extern enqueue_problem;
#endif

#define USE_MALLOCS	0

#ifndef BUF512_NR
#define BUF512_NR	(sizeof(int) == 2 ? 40 : 128)
#endif

#define ACC_NR		200
#define CLIENT_NR	5

typedef struct buf512
{
	buf_t buf_header;
	char buf_data[512];
} buf512_t;

#if USE_MALLOCS
PRIVATE buf512_t *buffers512;
PRIVATE acc_t *accessors;
#else
PRIVATE buf512_t buffers512[BUF512_NR];
PRIVATE acc_t accessors[ACC_NR];
#endif

PRIVATE buf512_t *buf512_free;

PRIVATE bf_freereq_t freereq[CLIENT_NR];
PRIVATE size_t bf_buf_gran;
PRIVATE acc_t *acc_free_list;

PUBLIC size_t bf_free_buffsize;
PUBLIC acc_t *bf_temporary_acc;


#ifdef bf_memreq
PUBLIC char *bf_memreq_file;
PUBLIC int bf_memreq_line;
#endif
#ifdef bf_cut
PUBLIC char *bf_cut_file;
PUBLIC int bf_cut_line;
#endif
#ifdef bf_packIffLess
PUBLIC char *bf_pack_file;
PUBLIC int bf_pack_line;
#endif
#ifdef bf_bufsize
PUBLIC char *bf_bufsize_file;
PUBLIC int bf_bufsize_line;
#endif

FORWARD acc_t *bf_small_memreq ARGS(( size_t size ));
FORWARD void bf_512free ARGS(( buf_t *buffer ));

PUBLIC void bf_init()
{
	int i;
	size_t size;
	size_t buf_s;

	bf_buf_gran= BUF_S;
	buf_s= 0;

#if USE_MALLOCS
	printf("buf.c: malloc %d 32K-buffers (%dK)\n", BUF32K_NR, 
		sizeof(*buffers32K) * BUF32K_NR / 1024);
	buffers32K= malloc(sizeof(*buffers32K) * BUF32K_NR);
	if (!buffers32K)
		ip_panic(( "unable to alloc 32K-buffers" ));
	printf("buf.c: malloc %d 2K-buffers (%dK)\n", BUF2K_NR, 
		sizeof(*buffers2K) * BUF2K_NR / 1024);
	buffers2K= malloc(sizeof(*buffers2K) * BUF2K_NR);
	if (!buffers2K)
		ip_panic(( "unable to alloc 2K-buffers" ));
	printf("buf.c: malloc %d 512-buffers (%dK)\n", BUF512_NR, 
		sizeof(*buffers512) * BUF512_NR / 1024);
	buffers512= malloc(sizeof(*buffers512) * BUF512_NR);
	if (!buffers512)
		ip_panic(( "unable to alloc 512-buffers" ));
	printf("buf.c: malloc %d accessors (%dK)\n", ACC_NR, 
		sizeof(*accessors) * ACC_NR / 1024);
	accessors= malloc(sizeof(*accessors) * ACC_NR);
	if (!accessors)
		ip_panic(( "unable to alloc accessors" ));
#endif

	for (i=0;i<BUF512_NR;i++)
	{
		buffers512[i].buf_header.buf_linkC= 0;
		buffers512[i].buf_header.buf_next= &buffers512[i+1];
		buffers512[i].buf_header.buf_free= bf_512free;
		buffers512[i].buf_header.buf_size= sizeof(buffers512[i].
			buf_data);
		buffers512[i].buf_header.buf_data_p= buffers512[i].buf_data;
	}
	buffers512[i-1].buf_header.buf_next= 0;
	buf512_free= &buffers512[0];
	if (sizeof(buffers512[0].buf_data) < bf_buf_gran)
		bf_buf_gran= sizeof(buffers512[0].buf_data);
	if (sizeof(buffers512[0].buf_data) > buf_s)
		buf_s= sizeof(buffers512[0].buf_data);

	for (i=0;i<ACC_NR;i++)
	{
		accessors[i].acc_linkC= 0;
		accessors[i].acc_next= &accessors[i+1];
	}
	acc_free_list= accessors;
	accessors[i-1].acc_next= 0;

	for (i=0;i<CLIENT_NR;i++)
		freereq[i]=0;

	assert (buf_s == BUF_S);
}

PUBLIC void bf_logon(func)
bf_freereq_t func;
{
	int i;

	for (i=0;i<CLIENT_NR;i++)
		if (!freereq[i])
		{
			freereq[i]=func;
			return;
		}

	ip_panic(( "buf.c: to many clients" ));
}

/*
bf_memreq
*/

#ifndef bf_memreq
PUBLIC acc_t *bf_memreq(size)
#else
PUBLIC acc_t *_bf_memreq(size)
#endif
size_t size;
{
	acc_t *head, *tail, *new_acc;
	int i,j;
	size_t count;

#if TRACE_ENQUEUE_PROBLEM
 { if (enqueue_problem)
  { where(); printf("bf_memreq(%d) called with enqueue_problem\n", size); } }
#endif
#ifdef bf_memreq
 { where(); printf("bf_memreq(%d) called by %s, %d\n", size, bf_memreq_file,
	bf_memreq_line); }
#endif
assert (size>0);

	head= NULL;
	while (size)
	{
		if (!acc_free_list)
		{
#if DEBUG
 { where(); printf("freeing accessors\n"); }
#endif
			for (i=0; !acc_free_list && i<MAX_BUFREQ_PRI; i++)
			{
				for (j=0; !acc_free_list && j<CLIENT_NR; j++)
				{
					bf_free_buffsize= 0;
					if (freereq[j])
						(*freereq[j])(i, BUF_S);
				}
			}
		}
		if (!acc_free_list)
			ip_panic(( "To few accessors" ));
		new_acc= acc_free_list;
		acc_free_list= acc_free_list->acc_next;
#if DEBUG & 256
 { where(); printf("got accessor %d\n", new_acc-accessors); }
#endif
		new_acc->acc_linkC= 1;
		new_acc->acc_buffer= 0;

#if DEBUG & 256
 { where(); printf("looking for 512 byte buffer\n"); }
#endif
		if (buf512_free)
		{
			buf512_t *buf512;

#if DEBUG & 256
 { where(); printf("found a 512 byte buffer\n"); }
#endif
			buf512= buf512_free;
			buf512_free= buf512->buf_header.buf_next;
assert (!buf512->buf_header.buf_linkC);
			buf512->buf_header.buf_linkC= 1;
assert (buf512->buf_header.buf_free == bf_512free);
assert (buf512->buf_header.buf_size == sizeof(buf512->buf_data));
assert (buf512->buf_header.buf_data_p == buf512->buf_data);
			new_acc->acc_buffer= &buf512->buf_header;
			buf512->buf_header.buf_next= buf512;
		}
#if DEBUG
		else
 { where(); printf("unable to find a 512 byte buffer\n"); }
#endif
		if (!new_acc->acc_buffer)
		{
#if DEBUG
 { where(); printf("freeing buffers\n"); }
#endif
			bf_free_buffsize= 0;
			for (i=0; bf_free_buffsize<size && i<MAX_BUFREQ_PRI;
				i++)
				for (j=0; bf_free_buffsize<size && j<CLIENT_NR;
					j++)
					if (freereq[j])
						(*freereq[j])(i, size);

			if (bf_free_buffsize<size)
				ip_panic(( "not enough buffers freed" ));

			continue;
		}


		if (!head)
			head= new_acc;
		else
			tail->acc_next= new_acc;
		tail= new_acc;

		count= tail->acc_buffer->buf_size;
		if (count > size)
			count= size;

		tail->acc_offset= 0;
		tail->acc_length=  count;
		size -= count;
	}
	tail->acc_next= 0;
#if DEBUG
bf_chkbuf(head);
#endif
#if DEBUG & 256
 { where(); printf("acc 0x%x has buffer 0x%x\n", head, head->acc_buffer); }
#endif
	return head;
}

/*
bf_small_memreq
*/

PRIVATE acc_t *bf_small_memreq(size)
size_t size;
{
	acc_t *head, *tail, *new_acc;
	int i,j;
	size_t count;

#if TRACE_ENQUEUE_PROBLEM
 { if (enqueue_problem)
  { where(); printf("bf_small_memreq(%d) called with enqueue_problem\n", size);
	} }
#endif
#if DEBUG & 256
 { where(); printf("bf_small_memreq(%d)\n", size); }
#endif

assert (size>0);

	head= NULL;
	while (size)
	{
		if (!acc_free_list)
		{
#if DEBUG
 { where(); printf("freeing accessors\n"); }
#endif
			for (i=0; !acc_free_list && i<MAX_BUFREQ_PRI; i++)
			{
				for (j=0; !acc_free_list && j<CLIENT_NR; j++)
				{
					bf_free_buffsize= 0;
					if (freereq[j])
						(*freereq[j])(i, BUF_S);
				}
			}
		}
		new_acc= acc_free_list;
		if (!new_acc)
			ip_panic(( "buf.c: out of accessors" ));
		acc_free_list= new_acc->acc_next;
#if DEBUG & 256
 { where(); printf("got accessor %d\n", new_acc-accessors); }
#endif
		new_acc->acc_linkC= 1;

		if (size >= sizeof(buf512_free->buf_data))
		{
			if (buf512_free)
			{
				buf512_t *buf512;

#if DEBUG & 256
 { where(); printf("found a 512 byte buffer\n"); }
#endif
				buf512= buf512_free;
				buf512_free= buf512->buf_header.buf_next;
assert (!buf512->buf_header.buf_linkC);
				buf512->buf_header.buf_linkC= 1;
assert (buf512->buf_header.buf_free == bf_512free);
assert (buf512->buf_header.buf_size == sizeof(buf512->buf_data));
assert (buf512->buf_header.buf_data_p == buf512->buf_data);
				new_acc->acc_buffer= &buf512->buf_header;
				buf512->buf_header.buf_next= buf512;
			}
			else
				break;
		}
		else
			break;

		if (!head)
			head= new_acc;
		else
			tail->acc_next= new_acc;
		tail= new_acc;

		count= tail->acc_buffer->buf_size;
		if (count > size)
			count= size;

		tail->acc_offset= 0;
		tail->acc_length=  count;
		size -= count;
	}
	if (size)
	{
		new_acc->acc_linkC= 0;
		new_acc->acc_next= acc_free_list;
		acc_free_list= new_acc;
		new_acc= bf_memreq(size);
		if (!head)
			head= new_acc;
		else
			tail->acc_next= new_acc;
	}
	else
		tail->acc_next= 0;
	return head;
}

PUBLIC void bf_afree(acc_ptr)
acc_t *acc_ptr;
{
	acc_t *tmp_acc;
	buf_t *tmp_buf;

	while (acc_ptr)
	{
assert (acc_ptr->acc_linkC);
		acc_ptr->acc_linkC--;
		if (!acc_ptr->acc_linkC)
		{
			tmp_buf= acc_ptr->acc_buffer;
assert (tmp_buf);
assert (tmp_buf->buf_linkC);
			if (!--tmp_buf->buf_linkC)
			{
				bf_free_buffsize += tmp_buf->buf_size;
				tmp_buf->buf_free(tmp_buf);
			}
			tmp_acc= acc_ptr;
			acc_ptr= acc_ptr->acc_next;
			tmp_acc->acc_next= acc_free_list;
			acc_free_list= tmp_acc;
		}
		else
			break;
	}
}

PUBLIC acc_t *bf_dupacc(acc_ptr)
register acc_t *acc_ptr;
{
	register acc_t *new_acc;
	int i, j;

#if TRACE_ENQUEUE_PROBLEM
 { if (enqueue_problem)
  { where(); printf("bf_dupacc(0x%x) called with enqueue_problem\n", acc_ptr);
	} }
#endif


	if (!acc_free_list)
	{
#if DEBUG
 { where(); printf("freeing accessors\n"); }
#endif
		for (i=0; !acc_free_list && i<MAX_BUFREQ_PRI; i++)
		{
			for (j=0; !acc_free_list && j<CLIENT_NR; j++)
			{
				bf_free_buffsize= 0;
				if (freereq[j])
					(*freereq[j])(i, BUF_S);
			}
		}
	}
	new_acc= acc_free_list;
	if (!new_acc)
		ip_panic(( "buf.c: out of accessors" ));
	acc_free_list= new_acc->acc_next;
#if DEBUG & 256
 { where(); printf("got accessor %d\n", new_acc-accessors); }
#endif

	*new_acc= *acc_ptr;
	if (acc_ptr->acc_next)
		acc_ptr->acc_next->acc_linkC++;
	if (acc_ptr->acc_buffer)
		acc_ptr->acc_buffer->buf_linkC++;
	new_acc->acc_linkC= 1;
	return new_acc;
}

#ifdef bf_bufsize
PUBLIC size_t _bf_bufsize(acc_ptr)
#else
PUBLIC size_t bf_bufsize(acc_ptr)
#endif
register acc_t *acc_ptr;
{
	register size_t size;

#ifdef bf_bufsize
 { where(); printf("bf_bufsize(0x%x) called by %s, %d\n", acc_ptr,
	bf_bufsize_file, bf_bufsize_line); }
#endif

assert(acc_ptr);

	size=0;

	while (acc_ptr)
	{
assert(acc_ptr >= accessors && acc_ptr <= &accessors[ACC_NR-1]);
		size += acc_ptr->acc_length;
		acc_ptr= acc_ptr->acc_next;
	}
#if DEBUG & 256
 { where(); printf("bf_bufsize(...)= %d\n", size); }
#endif
	return size;
}

#ifndef bf_packIffLess
PUBLIC acc_t *bf_packIffLess(pack, min_len)
#else
PUBLIC acc_t *_bf_packIffLess(pack, min_len)
#endif
acc_t *pack;
int min_len;
{
	if (!pack || pack->acc_length >= min_len)
		return pack;

#ifdef bf_packIffLess
 { where(); printf("calling bf_pack because of %s %d: %d\n", bf_pack_file,
	bf_pack_line, min_len); }
#endif
	return bf_pack(pack);
}

PUBLIC acc_t *bf_pack(old_acc)
acc_t *old_acc;
{
	acc_t *new_acc, *acc_ptr_old, *acc_ptr_new;
	size_t size, offset_old, offset_new, block_size, block_size_old;

	/* Check if old acc is good enough. */
	if (!old_acc || !old_acc->acc_next && old_acc->acc_linkC == 1 && 
		(!old_acc->acc_buffer || old_acc->acc_buffer->buf_linkC == 1))
		return old_acc;

	size= bf_bufsize(old_acc);
	new_acc= bf_memreq(size);
	acc_ptr_old= old_acc;
	acc_ptr_new= new_acc;
	offset_old= 0;
	offset_new= 0;
	while (size)
	{
assert (acc_ptr_old);
		if (offset_old == acc_ptr_old->acc_length)
		{
			offset_old= 0;
			acc_ptr_old= acc_ptr_old->acc_next;
			continue;
		}
assert (offset_old < acc_ptr_old->acc_length);
		block_size_old= acc_ptr_old->acc_length - offset_old;
assert (acc_ptr_new);
		if (offset_new == acc_ptr_new->acc_length)
		{
			offset_new= 0;
			acc_ptr_new= acc_ptr_new->acc_next;
			continue;
		}
assert (offset_new < acc_ptr_new->acc_length);
		block_size= acc_ptr_new->acc_length - offset_new;
		if (block_size > block_size_old)
			block_size= block_size_old;
		memcpy(ptr2acc_data(acc_ptr_new)+offset_new,
			ptr2acc_data(acc_ptr_old)+offset_old, block_size);
		offset_new += block_size;
		offset_old += block_size;
		size -= block_size;
	}
	bf_afree(old_acc);
	return new_acc;
}

#ifndef bf_cut
PUBLIC acc_t *bf_cut (data, offset, length)
#else
PUBLIC acc_t *_bf_cut (data, offset, length)
#endif
register acc_t *data;
register unsigned offset;
register unsigned length;
{
	register acc_t *head, *tail;

#if DEBUG & 256
 { where(); printf("bf_cut(.., %u, %u) called\n", offset, length); }
#ifdef bf_cut
 { where(); printf("bf_cut_file= %s, bf_cut_line= %d\n", bf_cut_file,
	bf_cut_line); }
#endif
#endif
	if (!data && !offset && !length)
		return 0;
#ifdef bf_cut
if (!data)
 { where(); printf("bf_cut_file= %s, bf_cut_line= %d\n", bf_cut_file,
	bf_cut_line); }
#endif
assert(data);
#if DEBUG
bf_chkbuf(data);
#endif
	if (!length)
	{
		head= bf_dupacc(data);
		bf_afree(head->acc_next);
		head->acc_next= 0;
		head->acc_length= 0;
#if DEBUG
bf_chkbuf(data);
#endif
		return head;
	}
	while (data && offset>=data->acc_length)
	{
		offset -= data->acc_length;
		data= data->acc_next;
	}
#ifdef bf_cut
if (!data)
 { where(); printf("bf_cut_file= %s, bf_cut_line= %d\n", bf_cut_file,
	bf_cut_line); }
#endif
assert (data);
	head= bf_dupacc(data);
	bf_afree(head->acc_next);
	head->acc_next= 0;
	head->acc_offset += offset;
	head->acc_length -= offset;
	if (length >= head->acc_length)
		length -= head->acc_length;
	else
	{
		head->acc_length= length;
		length= 0;
	}
	tail= head;
	data= data->acc_next;
	while (data && length && length>=data->acc_length)
	{
		tail->acc_next= bf_dupacc(data);
		tail= tail->acc_next;
		bf_afree(tail->acc_next);
		tail->acc_next= 0;
		data= data->acc_next;
		length -= tail->acc_length;
	}
	if (length)
	{
#ifdef bf_cut
if (!data)
 { where(); printf("bf_cut_file= %s, bf_cut_line= %d\n", bf_cut_file,
	bf_cut_line); }
#endif
assert (data);
		tail->acc_next= bf_dupacc(data);
		tail= tail->acc_next;
		bf_afree(tail->acc_next);
		tail->acc_next= 0;
		tail->acc_length= length;
	}
#if DEBUG
bf_chkbuf(data);
#endif
	return head;
}

/*
bf_append
*/

PUBLIC acc_t *bf_append(data_first, data_second)
acc_t *data_first;
acc_t  *data_second;
{
	acc_t *head, *tail, *new_acc, *acc_ptr_new, tmp_acc, *curr;
	char *src_ptr, *dst_ptr;
	size_t size, offset_old, offset_new, block_size_old, block_size;

#if TRACE_ENQUEUE_PROBLEM
 { if (enqueue_problem)
  { where(); printf("bf_append(0x%x, 0x%x) called with enqueue_problem\n",
	data_first, data_second); } }
#endif
#if DEBUG & 256
 { where(); printf("BF_Append(0x%x, 0x%x) called\n", data_first, data_second); }
#endif
	if (!data_first)
		return data_second;
	if (!data_second)
		return data_first;

	head= 0;
	while (data_first)
	{
		if (data_first->acc_linkC == 1)
			curr= data_first;
		else
		{
			curr= bf_dupacc(data_first);
			assert (curr->acc_linkC == 1);
			bf_afree(data_first);
		}
		data_first= curr->acc_next;
		if (!curr->acc_length)
		{
			curr->acc_next= 0;
			bf_afree(curr);
			continue;
		}
		if (!head)
			head= curr;
		else
			tail->acc_next= curr;
		tail= curr;
	}
	if (!head)
		return data_second;
	tail->acc_next= 0;

	while (data_second && !data_second->acc_length)
	{
		curr= data_second;
		data_second= data_second->acc_next;
		if (data_second)
			data_second->acc_linkC++;
		bf_afree(curr);
	}
	if (!data_second)
		return head;

	if (tail->acc_length + data_second->acc_length >
		tail->acc_buffer->buf_size)
	{
		tail->acc_next= data_second;
		return head;
	}

	if (tail->acc_buffer->buf_size == bf_buf_gran && 
		tail->acc_buffer->buf_linkC == 1)
	{
		if (tail->acc_offset)
		{
			memmove(tail->acc_buffer->buf_data_p,
				ptr2acc_data(tail), tail->acc_length);
			tail->acc_offset= 0;
		}
		dst_ptr= ptr2acc_data(tail) + tail->acc_length;
		src_ptr= ptr2acc_data(data_second);
		memcpy(dst_ptr, src_ptr, data_second->acc_length);
		tail->acc_length += data_second->acc_length;
		tail->acc_next= data_second->acc_next;
		if (data_second->acc_next)
			data_second->acc_next->acc_linkC++;
		bf_afree(data_second);
		return head;
	}

	new_acc= bf_small_memreq(tail->acc_length+data_second->acc_length);
	acc_ptr_new= new_acc;
	offset_old= 0;
	offset_new= 0;
	size= tail->acc_length;
	while (size)
	{
assert (acc_ptr_new);
		if (offset_new == acc_ptr_new->acc_length)
		{
			offset_new= 0;
			acc_ptr_new= acc_ptr_new->acc_next;
			continue;
		}
assert (offset_new < acc_ptr_new->acc_length);
assert (offset_old < tail->acc_length);
		block_size_old= tail->acc_length - offset_old;
		block_size= acc_ptr_new->acc_length - offset_new;
		if (block_size > block_size_old)
			block_size= block_size_old;
		memcpy(ptr2acc_data(acc_ptr_new)+offset_new,
			ptr2acc_data(tail)+offset_old, block_size);
		offset_new += block_size;
		offset_old += block_size;
		size -= block_size;
	}
	offset_old= 0;
	size= data_second->acc_length;
	while (size)
	{
assert (acc_ptr_new);
		if (offset_new == acc_ptr_new->acc_length)
		{
			offset_new= 0;
			acc_ptr_new= acc_ptr_new->acc_next;
			continue;
		}
assert (offset_new < acc_ptr_new->acc_length);
assert (offset_old < data_second->acc_length);
		block_size_old= data_second->acc_length - offset_old;
		block_size= acc_ptr_new->acc_length - offset_new;
		if (block_size > block_size_old)
			block_size= block_size_old;
		memcpy(ptr2acc_data(acc_ptr_new)+offset_new,
			ptr2acc_data(data_second)+offset_old, block_size);
		offset_new += block_size;
		offset_old += block_size;
		size -= block_size;
	}
	tmp_acc= *tail;
	*tail= *new_acc;
	*new_acc= tmp_acc;

	bf_afree(new_acc);
	while (tail->acc_next)
		tail= tail->acc_next;

	tail->acc_next= data_second->acc_next;
	if (data_second->acc_next)
		data_second->acc_next->acc_linkC++;
	bf_afree(data_second);
	return head;
}

PRIVATE void bf_512free(buffer)
buf_t *buffer;
{
	buf512_t *buf512;

	buf512= buffer->buf_next;
	buf512->buf_header.buf_next= buf512_free;
	buf512_free= buf512;
}

PUBLIC void bf_check_all_bufs()
{
	int j;
	int accs;
	acc_t *acc;
	int bufs;
	buf512_t *buf512;

	for (j=0; j<CLIENT_NR; j++)
	{
		if (freereq[j])
			(*freereq[j])(-1, 0);
	}

	/* Check the number of accessors */
	accs= 0;
	for(acc= acc_free_list; acc; acc= acc->acc_next)
		accs++;
	printf("number of free accs is %d, expected %d\n", accs, ACC_NR);

	/* Check the number of 512 byte buffers */
	bufs= 0;
	for(buf512= buf512_free; buf512; buf512= 
					(buf512_t *)buf512->buf_header.buf_next)
		bufs++;

	printf("number of free 512 byte buffers is %d, expected %d\n", bufs, 
		BUF512_NR);
}
