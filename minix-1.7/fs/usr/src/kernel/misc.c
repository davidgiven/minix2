/* This file contains a collection of miscellaneous procedures:
 *	mem_init:	initialize memory tables.  Some memory is reported
 *			by the BIOS, some is guesstimated and checked later
 *	env_parse	parse environment variable.
 *	bad_assertion	for debugging
 *	bad_compare	for debugging
 */

#include "kernel.h"
#include "assert.h"
#include <stdlib.h>
#include <minix/com.h>

#if (CHIP == INTEL)

#define EM_BASE     0x100000L	/* base of extended memory on AT's */
#define SHADOW_BASE 0xFA0000L	/* base of RAM shadowing ROM on some AT's */
#define SHADOW_MAX  0x060000L	/* maximum usable shadow memory (16M limit) */

/*=========================================================================*
 *				mem_init				   *
 *=========================================================================*/
PUBLIC void mem_init()
{
/* Initialize the memory size tables.  This is complicated by fragmentation
 * and different access strategies for protected mode.  There must be a
 * chunk at 0 big enough to hold Minix proper.  For 286 and 386 processors,
 * there can be extended memory (memory above 1MB).  This usually starts at
 * 1MB, but there may be another chunk just below 16MB, reserved under DOS
 * for shadowing ROM, but available to Minix if the hardware can be re-mapped.
 * In protected mode, extended memory is accessible assuming CLICK_SIZE is
 * large enough, and is treated as ordinary memory.
 */

  u32_t ext_clicks;
  phys_clicks max_clicks;

  /* Get the size of ordinary memory from the BIOS. */
  mem[0].size = k_to_click(low_memsize);	/* base = 0 */

  if (pc_at && protected_mode) {
	/* Get the size of extended memory from the BIOS.  This is special
	 * except in protected mode, but protected mode is now normal.
	 * Note that no more than 16M can be addressed in 286 mode, so make
	 * sure that the highest memory address fits in a short when counted
	 * in clicks.
	 */
	ext_clicks = k_to_click((u32_t) ext_memsize);
	max_clicks = USHRT_MAX - (EM_BASE >> CLICK_SHIFT);
	mem[1].size = MIN(ext_clicks, max_clicks);
	mem[1].base = EM_BASE >> CLICK_SHIFT;

	if (ext_memsize <= (unsigned) ((SHADOW_BASE - EM_BASE) / 1024)
			&& check_mem(SHADOW_BASE, SHADOW_MAX) == SHADOW_MAX) {
		/* Shadow ROM memory. */
		mem[2].size = SHADOW_MAX >> CLICK_SHIFT;
		mem[2].base = SHADOW_BASE >> CLICK_SHIFT;
	}
  }

  /* Total system memory. */
  tot_mem_size = mem[0].size + mem[1].size + mem[2].size;
}
#endif /* (CHIP == INTEL) */

/*=========================================================================*
 *				env_parse				   *
 *=========================================================================*/
PUBLIC int env_parse(env, fmt, field, param, min, max)
char *env;		/* environment variable to inspect */
char *fmt;		/* template to parse it with */
int field;		/* field number of value to return */
long *param;		/* address of parameter to get */
long min, max;		/* minimum and maximum values for the parameter */
{
/* Parse an environment variable setting, something like "DPETH0=300:3".
 * Panic if the parsing fails.  Return EP_UNSET if the environment variable
 * is not set, EP_OFF if it is set to "off", EP_ON if set to "on" or a
 * field is left blank, or EP_SET if a field is given (return value through
 * *param).  Commas and colons may be used in the environment and format
 * string, fields in the environment string may be empty, and punctuation
 * may be missing to skip fields.  The format string contains characters
 * 'd', 'o', 'x' and 'c' to indicate that 10, 8, 16, or 0 is used as the
 * last argument to strtol.
 */

  char *val, *end;
  long newpar;
  int i = 0, radix, r;

  if ((val = k_getenv(env)) == NIL_PTR) return(EP_UNSET);
  if (strcmp(val, "off") == 0) return(EP_OFF);
  if (strcmp(val, "on") == 0) return(EP_ON);

  r = EP_ON;
  for (;;) {
	while (*val == ' ') val++;

	if (*val == 0) return(r);	/* the proper exit point */

	if (*fmt == 0) break;		/* too many values */

	if (*val == ',' || *val == ':') {
		/* Time to go to the next field. */
		if (*fmt == ',' || *fmt == ':') i++;
		if (*fmt++ == *val) val++;
	} else {
		/* Environment contains a value, get it. */
		switch (*fmt) {
		case 'd':	radix =   10;	break;
		case 'o':	radix =  010;	break;
		case 'x':	radix = 0x10;	break;
		case 'c':	radix =    0;	break;
		default:	goto badenv;
		}
		newpar = strtol(val, &end, radix);

		if (end == val) break;	/* not a number */
		val = end;

		if (i == field) {
			/* The field requested. */
			if (newpar < min || newpar > max) break;
			*param = newpar;
			r = EP_SET;
		}
	}
  }
badenv:
  printf("Bad environment setting: '%s = %s'\n", env, k_getenv(env));
  panic("", NO_NUM);
  /*NOTREACHED*/
}

#if DEBUG
/*=========================================================================*
 *				bad_assertion				   *
 *=========================================================================*/
PUBLIC void bad_assertion(file, line, what)
char *file;
int line;
char *what;
{
  printf("panic at %s(%d): assertion \"%s\" failed\n", file, line, what);
  panic(NULL, NO_NUM);
}

/*=========================================================================*
 *				bad_compare				   *
 *=========================================================================*/
PUBLIC void bad_compare(file, line, lhs, what, rhs)
char *file;
int line;
int lhs;
char *what;
int rhs;
{
  printf("panic at %s(%d): compare (%d) %s (%d) failed\n",
	file, line, lhs, what, rhs);
  panic(NULL, NO_NUM);
}
#endif /* DEBUG */
