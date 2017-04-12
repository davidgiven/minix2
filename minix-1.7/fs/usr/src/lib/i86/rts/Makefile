# Makefile for lib/i86/rts.

CFLAGS	= -O -D_MINIX -D_POSIX_SOURCE -Was-ncc
CC1	= $(CC) $(CFLAGS) -c

LIBRARY	= ../../libc.a

all:	\
	../../ncrtso.o \
	$(LIBRARY)

../../ncrtso.o:	ncrtso.s
	$(CC1) -c ncrtso.s
	mv ncrtso.o $@

OBJECTS	= \
	$(LIBRARY)(__sigreturn.o) \
	$(LIBRARY)(_sendrec.o) \
	$(LIBRARY)(brksize.o) \
	$(LIBRARY)(setjmp.o) \

$(LIBRARY):	$(OBJECTS)
	aal cr $@ *.o
	rm *.o

$(LIBRARY)(__sigreturn.o):	__sigreturn.s
	$(CC1) __sigreturn.s

$(LIBRARY)(_sendrec.o):	_sendrec.s
	$(CC1) _sendrec.s

$(LIBRARY)(brksize.o):	brksize.s
	$(CC1) brksize.s

$(LIBRARY)(setjmp.o):	setjmp.s
	$(CC1) setjmp.s
