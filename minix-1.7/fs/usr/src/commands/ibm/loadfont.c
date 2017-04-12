/* loadfont.c - Load custom font into EGA, VGA video card
 *
 * Author: Hrvoje Stipetic (hs@hck.hr) Jun-1995.
 *
 */

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#define FONT_SIZE	8192
#define FONT_FILE_SIZE	4096


void tell(char *s)
{
  write(2, s, strlen(s));
}


void report(char *say)
{
  int err = errno;
  tell("loadfont: ");
  if (say != NULL) {
	tell(say);
	tell(": ");
  }
  tell(strerror(err));
  tell("\n");
}


void usage(void)
{
  tell("Usage: loadfont fontfile\n");
  exit(1);
}


int main(int argc, char *argv[])
{
  static u8_t font[FONT_SIZE];
  u8_t *cm;
  int fd, n, i, j;


  if (argc != 2)
	usage();

  for (cm = font; cm < font + FONT_SIZE; cm++) *cm = 0;

  if ((fd = open(argv[1], O_RDONLY)) < 0) {
	report(argv[1]);
	exit(1);
  }

  if ((n = read(fd, font, FONT_FILE_SIZE)) != FONT_FILE_SIZE)
  {
	if (n < 0) {
		report(argv[1]);
	} else {
		tell("loadfont: ");
		tell(argv[1]);
		tell(": fontfile too short\n");
	}
	exit(1);
  }

  close(fd);

  for (i = 0; i < 256; i++)
    for (j = 0; j < 16; j++)
      font[FONT_SIZE-1 - 16 - i*32 - j] = font[FONT_FILE_SIZE-1 - i*16 - j]; 

  if (ioctl(0, TIOCSFON, font) < 0) {
	report((char *) NULL);
	exit(1);
  }

  exit(0);
}
