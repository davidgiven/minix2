/* Constants used by the Memory Manager. */

#define NO_MEM ((phys_clicks) 0)  /* returned by alloc_mem() with mem is up */

/*DEBUG*/
/* PAGE_SIZE should be SEGMENT_GRANULARITY and MAX_PAGES MAX_SEGMENTS.
 * The 386 segment granularity is 1 for segments smaller than 1M and 4096
 * above that.  This is not handled properly yet - assume small programs.
 */
#if (CHIP == INTEL && _WORD_SIZE == 2)
#define PAGE_SIZE	  16	/* how many bytes in a page (s.b.HCLICK_SIZE)*/
#define MAX_PAGES       4096	/* how many pages in the virtual addr space */
#endif

#define printf        printk

#define INIT_PID	   1	/* init's process id number */
