/* readclock - read the real time clock		Authors: T. Holm & E. Froese */

/************************************************************************/
/*									*/
/*   readclock.c							*/
/*									*/
/*		Read from the 64 byte CMOS RAM area, then write		*/
/*		the time to standard output in a form usable by		*/
/*		date(1).						*/
/*									*/
/*		If the machine ID byte is 0xFC or 0xF8, the device	*/
/*		/dev/mem exists and can be opened for reading,		*/
/*		and no errors in the CMOS RAM are reported by the	*/
/*		RTC, then the time is read from the clock RAM		*/
/*		area maintained by the RTC.				*/
/*									*/
/*		The clock RAM values are decoded and written to		*/
/*		standard output in the form:				*/
/*									*/
/*			mmddyyhhmmss					*/
/*									*/
/*		If the machine ID does not match 0xFC or 0xF8,		*/
/*		then ``-q'' is written to standard output.		*/
/*									*/
/*		If the machine ID is 0xFC or 0xF8 and /dev/mem		*/
/*		is missing, or cannot be accessed,			*/
/*		then an error message is written to stderr,		*/
/*		and ``-q'' is written to stdout.			*/
/*									*/
/*		If the RTC reports errors in the CMOS RAM,		*/
/*		then an error message is written to stderr,		*/
/*		and ``-q'' is written to stdout.			*/
/*									*/
/*		Readclock is used as follows when placed		*/
/*		the ``/etc/rc'' script:					*/
/*									*/
/*	 	  /usr/bin/date `/usr/bin/readclock` </dev/tty  	*/
/*									*/
/*		It is best if this program is owned by bin and NOT	*/
/*		SUID because of the peculiar method of reading from	*/
/*		RTC.  When someone makes a /dev/clock driver, this	*/
/*		warning may be removed.					*/
/*									*/
/************************************************************************/
/*    origination          1987-Dec-29              efth                */
/*    robustness	   1990-Oct-06		    C. Sylvain		*/
/* incorp. B. Evans ideas  1991-Jul-06		    C. Sylvain		*/
/************************************************************************/


#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#define CPU_TYPE_SEGMENT   0xFFFF	/* Segment for Machine ID in BIOS */
#define CPU_TYPE_OFFSET    0x000E	/* Offset for Machine ID	  */

#define PC_AT		   0xFC		/* Machine ID byte for PC/AT,
					   PC/XT286, and PS/2 Models 50, 60 */
#define PS_386		   0xF8		/* Machine ID byte for PS/2 Model 80 */

/* Manufacturers usually use the ID value of the IBM model they emulate.
 * However some manufacturers, notably HP and COMPAQ, have had different
 * ideas in the past.
 *
 * Machine ID byte information source:
 *	_The Programmer's PC Sourcebook_ by Thom Hogan,
 *	published by Microsoft Press
 */

#define CLK_ELE		0x70	/* CMOS RAM address register port (write only)
				 * Bit 7 = 1  NMI disable
				 *	   0  NMI enable
				 * Bits 6-0 = RAM address
				 */

#define CLK_IO		0x71	/* CMOS RAM data register port (read/write) */

#define  YEAR             9	/* Clock register addresses in CMOS RAM	*/
#define  MONTH            8
#define  DAY              7
#define  HOUR             4
#define  MINUTE           2
#define  SECOND           0
#define  STATUS        0x0B	/* Status register B: RTC configuration	*/
#define  HEALTH	       0x0E	/* Diagnostic status: (should be set by Power
				 * On Self-Test [POST])
				 * Bit  7 = RTC lost power
				 *	6 = Checksum (for addr 0x10-0x2d) bad
				 *	5 = Config. Info. bad at POST
				 *	4 = Mem. size error at POST
				 *	3 = I/O board failed initialization
				 *	2 = CMOS time invalid
				 *    1-0 =    reserved
				 */
#define  DIAG_BADBATT       0x80
#define  DIAG_MEMDIRT       0x40
#define  DIAG_BADTIME       0x04

/* CMOS RAM and RTC information source:
 *	_System BIOS for PC/XT/AT Computers and Compatibles_
 *	by Phoenix Technologies Ltd., published by Addison-Wesley
 */

#define  BCD_TO_DEC(x)      ( (x >> 4) * 10 + (x & 0x0f) )
#define  errmsg(s)          fputs( s, stderr )

struct time {
  unsigned year;
  unsigned month;
  unsigned day;
  unsigned hour;
  unsigned minute;
  unsigned second;
};

_PROTOTYPE(int main, (void));
_PROTOTYPE(void get_time, (struct time *t));
_PROTOTYPE(int read_register, (int reg_addr));

/* I/O and memory functions. */
_PROTOTYPE(unsigned inb, (U16_t _port));
_PROTOTYPE(void outb, (U16_t _port, U8_t _value));
_PROTOTYPE(int peek, (int a, int b));


int main()
{
  struct time time1;
  struct time time2;
  int i;
  int cpu_type, cmos_state;

  cpu_type = peek(CPU_TYPE_SEGMENT, CPU_TYPE_OFFSET);
  if (cpu_type < 0) {
	errmsg( "Memory I/O failed.\n" );
	printf("-q\n");
	exit(1);
  }
  if (cpu_type != PS_386 && cpu_type != PC_AT) {
	/* This is probably an XT, exit without complaining. */
	printf("-q\n");
	exit(1);
  }
  cmos_state = read_register(HEALTH);
  if (cmos_state & (DIAG_BADBATT | DIAG_MEMDIRT | DIAG_BADTIME)) {
	errmsg( "\nCMOS RAM error(s) found...   " );
	fprintf( stderr, "CMOS state = 0x%02x\n", cmos_state );

	if (cmos_state & DIAG_BADBATT)
	    errmsg( "RTC lost power. Reset CMOS RAM with SETUP.\n" );
	if (cmos_state & DIAG_MEMDIRT)
	    errmsg( "CMOS RAM checksum is bad. Run SETUP.\n" );
	if (cmos_state & DIAG_BADTIME)
	    errmsg( "Time invalid in CMOS RAM. Reset clock with setclock.\n" );
	errmsg( "\n" );

	printf("-q\n");
	exit(1);
  }
  for (i = 0; i < 10; i++) {
	get_time(&time1);
	get_time(&time2);

	if (time1.year == time2.year &&
	    time1.month == time2.month &&
	    time1.day == time2.day &&
	    time1.hour == time2.hour &&
	    time1.minute == time2.minute &&
	    time1.second == time2.second) {
		printf("%02d%02d%02d%02d%02d%02d\n",
		       time1.month, time1.day, time1.year,
		       time1.hour, time1.minute, time1.second);
		exit(0);
	}
  }

  errmsg( "Failed to get an accurate time.\n" );
  printf("-q\n");
  exit(1);
}



/***********************************************************************/
/*                                                                     */
/*    get_time( time )                                                 */
/*                                                                     */
/*    Update the structure pointed to by time with the current time    */
/*    as read from CMOS RAM of the RTC.				       */
/*    If necessary, the time is converted into a binary format before  */
/*    being stored in the structure.                                   */
/*                                                                     */
/***********************************************************************/

void get_time(t)
struct time *t;
{
  t->year = read_register(YEAR);
  t->month = read_register(MONTH);
  t->day = read_register(DAY);
  t->hour = read_register(HOUR);
  t->minute = read_register(MINUTE);
  t->second = read_register(SECOND);



  if ((read_register(STATUS) & 0x04) == 0) {
	/* Convert BCD to binary (default RTC mode) */
	t->year = BCD_TO_DEC(t->year);
	t->month = BCD_TO_DEC(t->month);
	t->day = BCD_TO_DEC(t->day);
	t->hour = BCD_TO_DEC(t->hour);
	t->minute = BCD_TO_DEC(t->minute);
	t->second = BCD_TO_DEC(t->second);
  }
}


int read_register(reg_addr)
char reg_addr;
{
  outb(CLK_ELE, reg_addr);
  return inb(CLK_IO);
}
