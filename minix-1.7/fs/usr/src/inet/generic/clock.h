/*
clock.h
*/

#ifndef CLOCK_H
#define CLOCK_H

struct timer;

typedef void (*timer_func_t) ARGS(( int fd, struct timer *timer ));

typedef struct timer
{
	struct timer *tim_next;
	timer_func_t tim_func;
	int tim_ref;
	time_t tim_time;
} timer_t;

void clck_init ARGS(( void ));
void set_time ARGS(( time_t time ));
time_t get_time ARGS(( void ));
void reset_time ARGS(( void ));
void clck_timer ARGS(( struct timer *timer, time_t timeout, timer_func_t func,
								int fd ));
		/* set a timer to go off at the time specified by timeout */
void clck_untimer ARGS(( struct timer *timer ));


#endif /* CLOCK_H */
