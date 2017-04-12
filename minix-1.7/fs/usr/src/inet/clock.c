/* clock.c */

#include "inet.h"
#include "proto.h"
#include "generic/assert.h"
#include "generic/buf.h"
#include "generic/clock.h"
#include "generic/type.h"

INIT_PANIC();

FORWARD _PROTOTYPE( void clck_fast_release, (timer_t *timer) );
FORWARD _PROTOTYPE( void set_timer, (void) );

PRIVATE time_t curr_time;
PRIVATE timer_t *timer_chain;
PRIVATE time_t next_timeout;

PUBLIC time_t get_time()
{
	if (!curr_time)
	{
		static message mess;

		mess.m_type= GET_UPTIME;
		if (sendrec (CLOCK, &mess) < 0)
			ip_panic(("unable to sendrec"));
		if (mess.m_type != OK)
			ip_panic(("can't read clock"));
		curr_time= mess.NEW_TIME;
	}
	return curr_time;
}

PUBLIC void set_time (tim)
time_t tim;
{
	curr_time= tim;
}

PUBLIC void clck_init()
{
	curr_time= 0;
	next_timeout= 0;
	timer_chain= 0;
}

PUBLIC void reset_time()
{
	curr_time= 0;
}

PUBLIC void clck_timer(timer, timeout, func, fd)
timer_t *timer;
time_t timeout;
timer_func_t func;
int fd;
{
	timer_t *timer_index;

#if DEBUG & 256
 { time_t curr_tim= get_time(); where(); 
	printf("clck_timer(0x%x, now%c%d HZ, 0x%x, %d)\n", timer, 
	timeout >= curr_tim ? '+' : '-', 
	timeout >= curr_tim ? timeout - curr_tim : curr_tim - timeout,  
	func, fd); }
#endif
	clck_fast_release(timer);
	timer->tim_next= 0;
	timer->tim_func= func;
	timer->tim_ref= fd;
	timer->tim_time= timeout;

	if (!timer_chain)
		timer_chain= timer;
	else if (timeout < timer_chain->tim_time)
	{
		timer->tim_next= timer_chain;
		timer_chain= timer;
	}
	else
	{
		timer_index= timer_chain;
		while (timer_index->tim_next &&
			timer_index->tim_next->tim_time < timeout)
			timer_index= timer_index->tim_next;
		timer->tim_next= timer_index->tim_next;
		timer_index->tim_next= timer;
	}
	if (timer_chain->tim_time != next_timeout)
		set_timer();
}

PUBLIC void clck_tick (mess)
message *mess;
{
#if DEBUG & 256
 { where(); printf("in clck_tick()\n"); }
#endif
	next_timeout= 0;
	set_timer();
}

PRIVATE void clck_fast_release (timer)
timer_t *timer;
{
	timer_t *timer_index;

	if (timer == timer_chain)
		timer_chain= timer_chain->tim_next;
	else
	{
		timer_index= timer_chain;
		while (timer_index && timer_index->tim_next != timer)
			timer_index= timer_index->tim_next;
		if (timer_index)
			timer_index->tim_next= timer->tim_next;
	}
}

PRIVATE void set_timer()
{
	time_t new_time;
	time_t curr_time;
	timer_t *timer_index;

#if DEBUG & 256
 { where(); printf("in set_timer()\n"); }
#endif
	curr_time= get_time();

	while (timer_chain && timer_chain->tim_time<=curr_time)
	{
		timer_index= timer_chain;
		timer_chain= timer_chain->tim_next;
#if DEBUG & 256
 { where(); printf("calling tim_func: 0x%x(%d, ..)\n", 
	timer_index->tim_func, timer_index->tim_ref); }
#endif
		(*timer_index->tim_func)(timer_index->tim_ref, timer_index);
	}
	if (timer_chain)
		new_time= timer_chain->tim_time;
	else
		new_time= 0;
	if (new_time != next_timeout)
	{
		static message mess;

		next_timeout= new_time;
		assert (!new_time || new_time > curr_time);

		if (new_time)
			new_time -= curr_time;

		mess.m_type= SET_SYNC_AL;
		mess.CLOCK_PROC_NR= THIS_PROC;
		mess.DELTA_TICKS= new_time;
		if (sendrec (CLOCK, &mess) < 0)
			ip_panic(("unable to sendrec"));
		if (mess.m_type != OK)
			ip_panic(("can't set timer"));
	}
}

void clck_untimer (timer)
timer_t *timer;
{
	clck_fast_release (timer);
	set_timer();
}
