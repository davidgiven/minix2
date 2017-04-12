/*	boot.h - Info between different parts of boot.	Author: Kees J. Bot
 */

/* Constants describing the metal: */

#define SECTOR_SIZE	512
#define SECTOR_SHIFT	9
#define RATIO		(BLOCK_SIZE / SECTOR_SIZE)

#define PARAMSEC	1	/* Sector containing boot parameters. */

#define DSKBASE		0x1E	/* Floppy disk parameter vector. */
#define DSKPARSIZE	11	/* There are this many bytes of parameters. */

#define ESC		'\33'	/* Escape key. */

#define HEADERPOS      0x00600L	/* Place for an array of struct exec's. */

#define MINIXPOS       0x00800L	/* Minix is loaded here (rounded up towards
				 * the click size).
				 */
#define FREEPOS	       0x08000L	/* Memory from FREEPOS to caddr is free to
				 * play with.
				 */
#define MSEC_PER_TICK	55	/* Clock does 18.2 ticks per second. */
#define TICKS_PER_DAY 0x1800B0L	/* After 24 hours it wraps. */

#define BOOTPOS	       0x07C00L	/* Bootstraps are loaded here. */
#define SIGNATURE	0xAA55	/* Proper bootstraps have this signature. */
#define SIGNATOFF	510	/* Offset within bootblock. */

/* BIOS video modes. */
#define MONO_MODE	0x07	/* 80x25 monochrome. */
#define COLOR_MODE	0x03	/* 80x25 color. */


/* Variables shared with boothead.s: */
#ifndef EXTERN
#define EXTERN extern
#endif

typedef struct vector {
	u16_t	offset;
	u16_t	segment;
} vector;

EXTERN vector rem_part;		/* Boot partition table entry. */

EXTERN u32_t caddr, daddr;	/* Code and data address of the boot program. */
EXTERN u32_t runsize;		/* Size of this program. */

EXTERN u16_t device;		/* Drive being booted from. */
EXTERN u16_t heads, sectors;	/* Its number of heads and sectors. */


/* Functions defined by boothead.s: */

void exit(int code);
			/* Exit the monitor. */
u32_t mon2abs(void *ptr);
			/* Local monitor address to absolute address. */
u32_t vec2abs(vector *vec);
			/* Vector to absolute address. */
void raw_copy(u32_t dstaddr, u32_t srcaddr, u32_t count);
			/* Copy bytes from anywhere to anywhere. */
u16_t get_word(u32_t addr);
			/* Get a word from anywhere. */
void put_word(u32_t addr, U16_t word);
			/* Put a word anywhere. */
void relocate(void);
			/* Switch to a copy of this program. */
int dev_geometry(void);
			/* Set parameters for the current device. */
int readsectors(u32_t bufaddr, u32_t sector, U8_t count);
			/* Read 1 or more sectors from "device". */
int writesectors(u32_t bufaddr, u32_t sector, U8_t count);
			/* Write 1 or more sectors to "device". */
int getchar(void);
			/* Blocking read for a keyboard character. */
int peekchar(void);
			/* Nonblocking keyboard read. */
void putchar(int c);
			/* Send a character to the screen. */
void reset_video(unsigned mode);
			/* Reset and clear the screen. */

u16_t get_bus(void);
			/* System bus type, XT, AT, or MCA. */
u16_t get_video(void);
			/* Display type, MDA to VGA. */
u16_t get_memsize(void);
			/* Amount of "normal" memory in K. */
u32_t get_ext_memsize(void);
			/* Amount of extended memory in K. */
u32_t get_tick(void);
			/* Current value of the clock tick counter. */

void bootstrap(int device, struct part_entry *entry);
			/* Execute a bootstrap routine for a different O.S. */
u32_t minix(u32_t koff, u32_t kcs, u32_t kds,
					char *bootparams, size_t paramsize);
			/* Start Minix. */


/* Shared between boot.c and bootimage.c: */

/* Sticky attributes. */
#define E_SPECIAL	0x01	/* These are known to the program. */
#define E_DEV		0x02	/* The value is a device name. */
#define E_RESERVED	0x04	/* May not be set by user, e.g. 'boot' */
#define E_STICKY	0x07	/* Don't go once set. */

/* Volatile attributes. */
#define E_VAR		0x08	/* Variable */
#define E_FUNCTION	0x10	/* Function definition. */

/* Variables, functions, and commands. */
typedef struct environment {
	struct environment *next;
	char	flags;
	char	*name;		/* name = value */
	char	*arg;		/* name(arg) {value} */
	char	*value;
	char	*defval;	/* Safehouse for default values. */
} environment;

EXTERN environment *env;	/* Lists the environment. */

char *b_value(char *name);	/* Get the value of a variable. */

EXTERN int fsok;	/* True if the boot device contains an FS. */
EXTERN u32_t lowsec;	/* Offset to the file system on the boot device. */
EXTERN u32_t reboot_code; /* Program returned by a rebooting Minix. */

/* Called by boot.c: */

void bootminix(void);		/* Load and start a Minix image. */


/* Called by bootimage.c: */

void readerr(off_t sec, int err);
			/* Report a read error. */
char *u2a(U16_t n), *ul2a(u32_t n);
			/* Transform an u16_t or u32_t to decimal. */
long a2l(char *a);
			/* Cheap atol(). */
unsigned a2x(char *a);
			/* ASCII to hex. */
dev_t name2dev(char *name);
			/* Translate a device name to a device number. */
int numprefix(char *s, char **ps);
			/* True for a numeric prefix. */
int numeric(char *s);
			/* True for a numeric string. */
char *unix_err(int err);
			/* Give a descriptive text for some UNIX errors. */
void invalidate_cache(void);
			/* About to load an image where the cache sits. */
void init_cache(void);
			/* Turn the block cache back on. */
int delay(char *msec);
			/* Delay for several millisec. */

#if DOS
/* The monitor runs under MS-DOS. */
int dos_open(char *name);
			/* Open file to use as the Minix virtual disk. */
extern char PSP[256];	/* Program Segment Prefix. */
#else
/* The monitor uses only the BIOS. */
#define DOS	0
#endif
