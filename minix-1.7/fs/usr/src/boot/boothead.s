!	Boothead.s - BIOS support for boot.c		Author: Kees J. Bot
!
!
! This file contains the startup and low level support for the secondary
! boot program.  It contains functions for disk, tty and keyboard I/O,
! copying memory to arbitrary locations, etc.
!
! The primary bootstrap code supplies the following parameters in registers:
!	dl	= Boot-device.
!	es:si	= Partition table entry if hard disk.
!

.define begtext, begdata, begbss
.data
begdata:
	.ascii	"(null)\0"
.bss
begbss:

	o32	    =	  0x66	! This assembler doesn't know 386 extensions
	BOOTOFF	    =	0x7C00	! 0x0000:BOOTOFF load a bootstrap here
	LOADSEG     =	0x1000	! Where this code is loaded.
	BUFFER	    =	0x0600	! First free memory
	PENTRYSIZE  =	    16	! Partition table entry size.
	DSKBASE     =	   120	! 120 = 4 * 0x1E = ptr to disk parameters
	DSKPARSIZE  =	    11	! 11 bytes of floppy parameters
	SECTORS	    =	     4	! Offset into parameters to sectors per track
	a_flags	    =	     2	! From a.out.h, struct exec
	a_text	    =	     8
	a_data	    =	    12
	a_bss	    =	    16
	a_total	    =	    24
	A_SEP	    =	  0x20	! Separate I&D flag
	K_I386	    =	0x0001	! Call Minix in 386 mode
	K_RET	    =	0x0020	! Returns to the monitor on reboot

	DS_SELECTOR =	   3*8	! Kernel data selector
	ES_SELECTOR =	   4*8	! Flat 4 Gb
	SS_SELECTOR =	   5*8	! Monitor stack
	CS_SELECTOR =	   6*8	! Kernel code
	MCS_SELECTOR=	   7*8	! Monitor code

! Imported variables and functions:
.extern _caddr, _daddr, _runsize, _edata, _end	! Runtime environment
.extern _device, _dskpars, _heads, _sectors	! Boot disk parameters
.extern _rem_part				! To pass partition info
.extern _k_flags				! Special kernel flags

.text
begtext:
.extern _boot, _printk				! Boot Minix, kernel printf

! Set segment registers and stack pointer using the programs own header!
! The header is either 32 bytes (short form) or 48 bytes (long form).  The
! bootblock will jump to address 0x10030 in both cases, calling one of the
! two jmpf instructions below.

	jmpf	boot, LOADSEG+3	! Set cs right (skipping long a.out header)
	.space	11		! jmpf + 11 = 16 bytes
	jmpf	boot, LOADSEG+2	! Set cs right (skipping short a.out header)
boot:
	mov	ax, #LOADSEG
	mov	ds, ax		! ds = header

	movb	al, a_flags
	testb	al, #A_SEP	! Separate I&D?
	jnz	sepID
comID:	xor	ax, ax
	xchg	ax, a_text	! No text
	add	a_data, ax	! Treat all text as data
sepID:
	mov	ax, a_total	! Total nontext memory usage
	and	ax, #0xFFFE	! Round down to even
	mov	a_total, ax	! total - text = data + bss + heap + stack
	cli			! Ignore interrupts while stack in limbo
	mov	sp, ax		! Set sp at the top of all that

	mov	ax, a_text	! Determine offset of ds above cs
	movb	cl, #4
	shr	ax, cl
	mov	cx, cs
	add	ax, cx
	mov	ds, ax		! ds = cs + text / 16
	mov	ss, ax
	sti			! Stack ok now
	push	es		! Save es, we need it for the partition table
	mov	es, ax
	cld			! C compiler wants UP

! Clear bss
	xor	ax, ax		! Zero
	mov	di, #_edata	! Start of bss is at end of data
	mov	cx, #_end	! End of bss (begin of heap)
	sub	cx, di		! Number of bss bytes
	shr	cx, #1		! Number of words
	rep
	stos			! Clear bss

! Copy primary boot parameters to variables.  (Can do this now that bss is
! cleared and may be written into).
	xorb	dh, dh
	mov	_device, dx	! Boot device (probably 0x00 or 0x80)
	mov	_rem_part+0, si	! Remote partition table offset
	pop	_rem_part+2	! and segment (saved es)

! Remember the current video mode for restoration on exit.
	movb	ah, #0x0F	! Get current video mode
	int	0x10
	andb	al, #0x7F	! Mask off bit 7 (no blanking)
	movb	old_vid_mode, al
	movb	cur_vid_mode, al

! Give C code access to the code segment, data segment and the size of this
! process.
	xor	ax, ax
	mov	dx, cs
	call	seg2abs
	mov	_caddr+0, ax
	mov	_caddr+2, dx
	xor	ax, ax
	mov	dx, ds
	call	seg2abs
	mov	_daddr+0, ax
	mov	_daddr+2, dx
	push	ds
	mov	ax, #LOADSEG
	mov	ds, ax		! Back to the header once more
	mov	ax, a_total+0
	mov	dx, a_total+2	! dx:ax = data + bss + heap + stack
	add	ax, a_text+0
	adc	dx, a_text+2	! dx:ax = text + data + bss + heap + stack
	pop	ds
	add	ax, #0x000F
	adc	dx, #0
	and	ax, #0xFFF0	! Round up to a segment
	mov	_runsize+0, ax
	mov	_runsize+2, dx	! 32 bit size of this process

! Time to switch to a higher level language (not much higher)
	call	_boot

.define	_exit, __exit, ___exit		! Make various compilers happy
_exit:
__exit:
___exit:
	mov	bx, sp
	cmp	2(bx), #0		! Good exit status?
	jz	reboot
quit:	mov	ax, #any_key
	push	ax
	call	_printk
	call	_getchar
reboot:	call	restore_video
	int	0x19			! Reboot the system
.data
any_key:
	.ascii	"\nHit any key to reboot\n\0"
.text

! Alas we need some low level support
!
! u32_t mon2abs(void *ptr)
!	Address in monitor data to absolute address.
.define _mon2abs
_mon2abs:
	mov	bx, sp
	mov	ax, 2(bx)	! ptr
	mov	dx, ds		! Monitor data segment
	jmp	seg2abs

! u32_t vec2abs(vector *vec)
!	8086 interrupt vector to absolute address.
.define _vec2abs
_vec2abs:
	mov	bx, sp
	mov	bx, 2(bx)
	mov	ax, (bx)
	mov	dx, 2(bx)	! dx:ax vector
	!jmp	seg2abs		! Translate

seg2abs:			! Translate dx:ax to the 32 bit address dx-ax
	push	cx
	movb	ch, dh
	movb	cl, #4
	shl	dx, cl
	shrb	ch, cl		! ch-dx = dx << 4
	add	ax, dx
	adcb	ch, #0		! ch-ax = ch-dx + ax
	movb	dl, ch
	xorb	dh, dh		! dx-ax = ch-ax
	pop	cx
	ret

abs2seg:			! Translate the 32 bit address dx-ax to dx:ax
	push	cx
	movb	ch, dl
	mov	dx, ax		! ch-dx = dx-ax
	and	ax, #0x000F	! Offset in ax
	movb	cl, #4
	shr	dx, cl
	shlb	ch, cl
	orb	dh, ch		! dx = ch-dx >> 4
	pop	cx
	ret

! void raw_copy(u32_t dstaddr, u32_t srcaddr, u32_t count)
!	Copy count bytes from srcaddr to dstaddr.  Don't do overlaps.
!	Also handles copying words to or from extended memory.
.define _raw_copy
_raw_copy:
	push	bp
	mov	bp, sp
	push	si
	push	di		! Save C variable registers
copy:
	cmp	14(bp), #0
	jnz	bigcopy
	mov	cx, 12(bp)
	jcxz	copydone	! Count is zero, end copy
	cmp	cx, #0xFFF0
	jb	smallcopy
bigcopy:mov	cx, #0xFFF0	! Don't copy more than about 64K at once
smallcopy:
	push	cx		! Save copying count
	mov	ax, 4(bp)
	mov	dx, 6(bp)
	cmp	dx, #0x0010	! Copy to extended memory?
	jae	ext_copy
	cmp	10(bp), #0x0010	! Copy from extended memory?
	jae	ext_copy
	call	abs2seg
	mov	di, ax
	mov	es, dx		! es:di = dstaddr
	mov	ax, 8(bp)
	mov	dx, 10(bp)
	call	abs2seg
	mov	si, ax
	mov	ds, dx		! ds:si = srcaddr
	shr	cx, #1		! Words to move
	rep
	movs			! Do the word copy
	adc	cx, cx		! One more byte?
	rep
	movsb			! Do the byte copy
	mov	ax, ss		! Restore ds and es from the remaining ss
	mov	ds, ax
	mov	es, ax
	jmp	copyadjust
ext_copy:
	mov	x_dst_desc+2, ax
	movb	x_dst_desc+4, dl ! Set base of destination segment
	mov	ax, 8(bp)
	mov	dx, 10(bp)
	mov	x_src_desc+2, ax
	movb	x_src_desc+4, dl ! Set base of source segment
	mov	si, #x_gdt	! es:si = global descriptor table
	shr	cx, #1		! Words to move
	movb	ah, #0x87	! Code for extended memory move
	int	0x15
copyadjust:
	pop	cx		! Restore count
	add	4(bp), cx
	adc	6(bp), #0	! srcaddr += copycount
	add	8(bp), cx
	adc	10(bp), #0	! dstaddr += copycount
	sub	12(bp), cx
	sbb	14(bp), #0	! count -= copycount
	jmp	copy		! and repeat
copydone:
	pop	di
	pop	si		! Restore C variable registers
	pop	bp
	ret

! u16_t get_word(u32_t addr);
! void put_word(u32_t addr, u16_t word);
!	Read or write a 16 bits word at an arbitrary location.
.define	_get_word, _put_word
_get_word:
	mov	bx, sp
	call	gp_getaddr
	mov	ax, (bx)	! Word to get from addr
	jmp	gp_ret
_put_word:
	mov	bx, sp
	push	6(bx)		! Word to store at addr
	call	gp_getaddr
	pop	(bx)		! Store the word
	jmp	gp_ret
gp_getaddr:
	mov	ax, 2(bx)
	mov	dx, 4(bx)
	call	abs2seg
	mov	bx, ax
	mov	ds, dx		! ds:bx = addr
	ret
gp_ret:
	push	es
	pop	ds		! Restore ds
	ret

! void relocate(void);
!	After the program has copied itself to a safer place, it needs to change
!	the segment registers.  Caddr has already been set to the new location.
.define _relocate
_relocate:
	pop	bx		! Return address
	mov	ax, _caddr+0
	mov	dx, _caddr+2
	call	abs2seg
	mov	cx, dx		! cx = new code segment
	mov	ax, cs		! Old code segment
	sub	ax, cx		! ax = -(new - old) = -Moving offset
	mov	dx, ds
	sub	dx, ax
	mov	ds, dx		! ds += (new - old)
	mov	es, dx
	mov	ss, dx
	xor	ax, ax
	call	seg2abs
	mov	_daddr+0, ax
	mov	_daddr+2, dx	! New data address
	push	cx		! New text segment
	push	bx		! Return offset of this function
	retf			! Relocate

! void *brk(void *addr)
! void *sbrk(size_t incr)
!	Cannot fail implementations of brk(2) and sbrk(3), so we can use
!	malloc(3).  They reboot on stack collision instead of returning -1.
.data
	.align	2
break:	.data2	_end		! A fake heap pointer
.text
.define _brk, __brk, _sbrk, __sbrk
_brk:
__brk:				! __brk is for the standard C compiler
	xor	ax, ax
	jmp	sbrk		! break= 0; return sbrk(addr);
_sbrk:
__sbrk:
	mov	ax, break	! ax= current break
sbrk:	push	ax		! save it as future return value
	mov	bx, sp		! Stack is now: (retval, retaddr, incr, ...)
	add	ax, 4(bx)	! ax= break + increment
	mov	break, ax	! Set new break
	lea	dx, -1024(bx)	! sp minus a bit of breathing space
	cmp	dx, ax		! Compare with the new break
	jb	heaperr		! Suffocating noises
	lea	dx, -4096(bx)	! A warning when heap+stack goes < 4K
	cmp	dx, ax
	jae	plenty		! No reason to complain
	mov	ax, #memwarn
	push	ax
	call	_printk		! Warn about memory running low
	pop	ax
	movb	memwarn, #0	! No more warnings
plenty:	pop	ax		! Return old break (0 for brk)
	ret
heaperr:mov	ax, #chmem
	push	ax
	mov	ax, #nomem
	push	ax
	call	_printk
	jmp	quit
.data
nomem:	.ascii	"\nOut of%s\0"
memwarn:.ascii	"\nLow on"
chmem:	.ascii	" memory, use chmem to increase the heap\n\0"
.text

! int dev_geometry(void);
!	Given the device "_device" and floppy disk parameters "_dskpars",
!	set the number of heads and sectors.  It returns true iff the device
!	exists.
.define	_dev_geometry
_dev_geometry:
	push	es
	push	di		! Save registers used by BIOS calls
	movb	dl, _device	! The default device
	cmpb	dl, #0x80	! Floppy < 0x80, winchester >= 0x80
	jae	winchester
floppy:
	int	0x11		! Get equipment configuration
	testb	al, #0x01	! Bit 0 set if floppies available
	jz	geoerr		! No floppy drives on this box
	shl	ax, #1		! Highest floppy drive # in bits 6-7
	shl	ax, #1		! Now in bits 0-1 of ah
	andb	ah, #0x03	! Extract bits
	cmpb	dl, ah		! Must be dl <= ah for drive to exist
	ja	geoerr		! Alas no drive dl.
	movb	dh, #2		! Floppies have two sides
	mov	bx, #_dskpars	! bx = disk parameters
	movb	cl, SECTORS(bx)
	xor	ax, ax
	mov	es, ax		! es = 0 = vector segment
	eseg
	mov	DSKBASE+0, bx
	eseg
	mov	DSKBASE+2, ds	! DSKBASE+2:DSKBASE+0 = ds:bx = floppy parms
	jmp	geoboth
winchester:
	movb	ah, #0x08	! Code for drive parameters
	int	0x13		! dl still contains drive
	jb	geoerr		! No such drive?
	andb	cl, #0x3F	! cl = max sector number (1-origin)
	incb	dh		! dh = 1 + max head number (0-origin)
geoboth:
	movb	_heads, dh	! Number of heads for this device
	movb	_sectors, cl	! Sectors per track
	movb	al, cl		! al = sectors per track
	mulb	dh		! ax = heads * sectors
	mov	secspcyl, ax	! Sectors per cylinder = heads * sectors
	mov	ax, #1		! Code for success
geodone:
	pop	di
	pop	es		! Restore di and es registers
	ret
geoerr:	xor	ax, ax		! Code for failure
	jmp	geodone
.bss
secspcyl:	.space 1*2
.text

! int readsectors(u32_t bufaddr, u32_t sector, u8_t count)
! int writesectors(u32_t bufaddr, u32_t sector, u8_t count)
!	Read/write several sectors from/to disk or floppy.  The buffer must
!	be between 64K boundaries!  Count must fit in a byte.  The external
!	variables _device, _sectors and _heads describe the disk and its
!	geometry.  Returns 0 for success, otherwise the BIOS error code.
!
.define _readsectors, _writesectors
_writesectors:
	push	bp
	mov	bp, sp
	movb	13(bp), #3	! Code for a disk write
	jmp	rwsec
_readsectors:
	push	bp
	mov	bp, sp
	movb	13(bp), #2	! Code for a disk read
rwsec:	push	di
	push	es
	mov	ax, 4(bp)
	mov	dx, 6(bp)
	call	abs2seg
	mov	bx, ax
	mov	es, dx		! es:bx = bufaddr
	mov	di, #3		! Execute 3 resets on floppy error
	cmpb	_device, #0x80
	jb	nohd
	mov	di, #1		! But only 1 reset on hard disk error
nohd:	cmpb	12(bp), #0	! count equals zero?
	jz	done
more:	mov	ax, 8(bp)
	mov	dx, 10(bp)	! dx:ax = abs sector.  Divide it by sectors/cyl
	div	secspcyl	! ax = cylinder, dx = sector within cylinder
	xchg	ax, dx		! ax = sector within cylinder, dx = cylinder
	movb	ch, dl		! ch = low 8 bits of cylinder
	divb	_sectors	! al = head, ah = sector (0-origin)
	xorb	dl, dl		! About to shift bits 8-9 of cylinder into dl
	shr	dx, #1
	shr	dx, #1		! dl[6..7] = high cylinder
	orb	dl, ah		! dl[0..5] = sector (0-origin)
	movb	cl, dl		! cl[0..5] = sector, cl[6..7] = high cyl
	incb	cl		! cl[0..5] = sector (1-origin)
	movb	dh, al		! dh = head
	movb	dl, _device	! dl = device to use
	movb	al, _sectors	! Sectors per track - Sector number (0-origin)
	subb	al, ah		! = Sectors left on this track
	cmpb	al, 12(bp)	! Compare with # sectors to transfer
	jbe	doit		! Can't go past the end of a cylinder?
	movb	al, 12(bp)	! 12(bp) < sectors left on this track
doit:	movb	ah, 13(bp)	! Code for disk read (2) or write (3)
	push	ax		! Save al = sectors to read
	int	0x13		! call the BIOS to do the transfer
	pop	cx		! Restore al in cl
	jb	ioerr		! I/O error
	movb	al, cl		! Restore al = sectors read
	addb	bh, al		! bx += 2 * al * 256 (add bytes transferred)
	addb	bh, al		! es:bx = where next sector is located
	add	8(bp), ax	! Update address by sectors transferred
	adc	10(bp), #0	! Don't forget high word
	subb	12(bp), al	! Decrement sector count by sectors transferred
	jnz	more		! Not all sectors have been transferred
done:	xorb	ah, ah		! No error here!
	jmp	finish
ioerr:	cmpb	ah, #0x80	! Disk timed out?  (Floppy drive empty)
	je	finish
	cmpb	ah, #0x03	! Disk write protected?
	je	finish
	dec	di		! Do we allow another reset?
	jl	finish		! No, report the error
	xorb	ah, ah		! Code for a reset (0)
	int	0x13
	jnb	more		! Succesful reset, try request again
finish:	movb	al, ah
	xorb	ah, ah		! ax = error number
	pop	es
	pop	di
	pop	bp
	ret

! int getchar(void), peekchar(void);
!	Read a character from the keyboard, or just look if there is one.
!	A carriage return is changed into a linefeed for UNIX compatibility.
.define _getchar, _peekchar
_peekchar:
	movb	ah, #0x01	! Keyboard status
	int	0x16
	jnz	getc		! Keypress?
	xor	ax, ax		! No key
	ret
_getchar:
	xorb	ah, ah		! Read character from keyboard
	int	0x16
getc:	cmpb	al, #0x0D	! Carriage return?
	jnz	nocr
	movb	al, #0x0A	! Change to linefeed
nocr:	xorb	ah, ah		! ax = al
	ret

! int putchar(int c);
!	Write a character in teletype mode.  The putc and putk synonyms
!	are for the kernel printk function that uses one of them.
!	Newlines are automatically preceded by a carriage return.
!
.define _putchar, _putc, _putk
_putchar:
_putc:
_putk:	mov	bx, sp
	movb	al, 2(bx)	! al = character to be printed
	testb	al, al		! 1.6.* printk adds a trailing null
	jz	nulch
	cmpb	al, #0x0A	! al = newline?
	jnz	putc
	movb	al, #0x0D
	call	putc		! putc('\r')
	movb	al, #0x0A	! Restore the '\n' and print it
putc:	movb	ah, #0x0E	! Print character in teletype mode
	mov	bx, #0x0001	! Page 0, foreground color
	int	0x10		! Call BIOS VIDEO_IO
nulch:	ret

! void reset_video(unsigned mode);
!	Reset and clear the screen.
.define _reset_video
_reset_video:
	mov	bx, sp
	mov	ax, 2(bx)	! Video mode
	mov	cur_vid_mode, ax
	testb	ah, ah
	jnz	xvesa		! VESA extended mode?
	int	0x10		! Reset video (ah = 0)
	jmp	setcur
xvesa:	mov	bx, ax		! bx = extended mode
	mov	ax, #0x4F02	! Reset video
	int	0x10
setcur:	xor	dx, dx		! dl = column = 0, dh = row = 0
	xorb	bh, bh		! Page 0
	movb	ah, #0x02	! Set cursor position
	int	0x10
	ret

restore_video:			! To restore the video mode on exit
	mov	ax, old_vid_mode
	cmp	ax, cur_vid_mode
	je	vidok
	push	ax
	call	_reset_video
	pop	ax
vidok:	ret

! u32_t get_tick(void);
!	Return the current value of the clock tick counter.  This counter
!	increments 18.2 times per second.  Poll it to do delays.  Does not
!	work on the original PC, but works on the PC/XT.
.define _get_tick
_get_tick:
	xorb	ah, ah		! Code for get tick count
	int	0x1A
	mov	ax, dx
	mov	dx, cx		! dx:ax = cx:dx = tick count
	ret


! Functions used to obtain info about the hardware, like the type of video
! and amount of memory.  Boot uses this information itself, but will also
! pass them on to a pure 386 kernel, because one can't make BIOS calls from
! protected mode.  The video type could probably be determined by the kernel
! too by looking at the hardware, but there is a small chance on errors that
! the monitor allows you to correct by setting variables.

.define _get_bus		! returns type of system bus
.define _get_video		! returns type of display
.define _get_memsize		! returns amount of low memory in K
.define _get_ext_memsize	! returns amount of extended memory in K

! u16_t get_bus(void)
!	Return type of system bus, in order: XT, AT, MCA.
_get_bus:
	call	_getprocessor
	xor	dx, dx		! Assume XT
	cmp	ax, #286	! An AT has at least a 286
	jb	got_bus
	inc	dx		! Assume AT
	movb	ah, #0xC0	! Code for get configuration
	int	0x15
	jc	got_bus		! Carry clear and ah = 00 if supported
	testb	ah, ah
	jne	got_bus
	eseg
	movb	al, 5(bx)	! Load feature byte #1
	inc	dx		! Assume MCA
	testb	al, #0x02	! Test bit 1 - "bus is Micro Channel"
	jnz	got_bus
	dec	dx		! Assume AT
	testb	al, #0x40	! Test bit 6 - "2nd 8259 installed"
	jnz	got_bus
	dec	dx		! It is an XT
got_bus:
	push	ds
	pop	es		! Restore es
	mov	ax, dx		! Return bus code
	ret

! u16_t get_video(void)
!	Return type of display, in order: MDA, CGA, mono EGA, color EGA,
!	mono VGA, color VGA.

_get_video:
	mov	ax, #0x1A00	! Function 1A returns display code
	int	0x10		! al = 1A if supported
	cmpb	al, #0x1A
	jnz	no_dc		! No display code function supported

	mov	ax, #2
	cmpb	bl, #5		! Is it a monochrome EGA?
	jz	got_video
	inc	ax
	cmpb	bl, #4		! Is it a color EGA?
	jz	got_video
	inc	ax
	cmpb	bl, #7		! Is it a monochrome VGA?
	jz	got_video
	inc	ax
	cmpb	bl, #8		! Is it a color VGA?
	jz	got_video

no_dc:	movb	ah, #0x12	! Get information about the EGA
	movb	bl, #0x10
	int	0x10
	cmpb	bl, #0x10	! Did it come back as 0x10? (No EGA)
	jz	no_ega

	mov	ax, #2
	cmpb	bh, #1		! Is it monochrome?
	jz	got_video
	inc	ax
	jmp	got_video

no_ega:	int	0x11		! Get bit pattern for equipment
	and	ax, #0x30	! Isolate color/mono field
	sub	ax, #0x30
	jz	got_video	! Is it an MDA?
	mov	ax, #1		! No it's CGA

got_video:
	ret

! u16_t get_memsize(void);
!	Ask the BIOS how much normal memory there is.

_get_memsize:
	int	0x12		! Returns the size (in K) in ax
	ret

! u32_t get_ext_memsize(void);
!	Ask the BIOS how much extended memory there is.

_get_ext_memsize:
	call	_getprocessor
	cmp	ax, #286	! Only 286s and above have extended memory
	jb	no_ext
	movb	ah, #0x88	! Code for get extended memory size
	clc			! Carry will stay clear if call exists
	int	0x15		! Returns size (in K) in ax for AT's
	jnc	got_ext
no_ext:	xor	ax, ax		! Error, no extended memory
got_ext:
	xor	dx, dx
	ret

! Functions to leave the boot monitor.
.define _bootstrap		! Call another bootstrap
.define _minix			! Call Minix

! void _bootstrap(int device, struct part_entry *entry)
!	Call another bootstrap routine to boot MS-DOS for instance.  (No real
!	need for that anymore, now that you can format floppies under Minix).
!	The bootstrap must have been loaded at BOOTSEG from "device".
_bootstrap:
	call	restore_video
	mov	bx, sp
	movb	dl, 2(bx)	! Device to boot from
	mov	si, 4(bx)	! ds:si = partition table entry
	xor	ax, ax
	mov	es, ax		! Vector segment
	mov	di, #BUFFER	! es:di = buffer in low core
	mov	cx, #PENTRYSIZE	! cx = size of partition table entry
 rep	movsb			! Copy the entry to low core
	mov	si, #BUFFER	! es:si = partition table entry
	mov	ds, ax		! Some bootstraps need zero segment registers
	cli
	mov	ss, ax
	mov	sp, #BOOTOFF	! This should do it
	sti
	jmpf	BOOTOFF, 0	! Back to where the BIOS loads the boot code

! u32_t minix(u32_t koff, u32_t kcs, u32_t kds,
!					char *bootparams, size_t paramsize);
!	Call Minix.
_minix:
	push	bp
	mov	bp, sp		! Pointer to arguments

	mov	dx, #0x03F2	! Floppy motor drive control bits
	movb	al, #0x0C	! Bits 4-7 for floppy 0-3 are off
	outb	dx		! Kill the motors
	push	ds
	mov	ax, #0x0040	! BIOS data segment
	mov	ds, ax
	andb	0x003F, #0xF0	! Clear diskette motor status bits of BIOS
	pop	ds
	cli			! No more interruptions

	test	_k_flags, #K_I386 ! Switch to 386 mode?
	jnz	minix386

! Call Minix in real mode.
minix86:
	push	18(bp)		! # bytes of boot parameters
	push	16(bp)		! address of boot parameters

	test	_k_flags, #K_RET ! Can the kernel return?
	jz	noret86
	push	cs
	mov	ax, #ret86
	push	ax		! Monitor far return address
noret86:

	mov	ax, 8(bp)
	mov	dx, 10(bp)
	call	abs2seg
	push	dx		! Kernel code segment
	push	4(bp)		! Kernel code offset
	mov	ax, 12(bp)
	mov	dx, 14(bp)
	call	abs2seg
	mov	ds, dx		! Kernel data segment
	mov	es, dx		! Set es to kernel data too
	retf			! Make a far call to the kernel

! Call Minix in 386 mode.
minix386:
  cseg	mov	cs_real-2, cs	! Patch CS and DS into the instructions that
  cseg	mov	ds_real-2, ds	! reload them when switching back to real mode

	mov	dx, ds		! Monitor ds
	mov	ax, #p_gdt	! dx:ax = Global descriptor table
	call	seg2abs
	mov	p_gdt_desc+2, ax
	movb	p_gdt_desc+4, dl ! Set base of global descriptor table

	mov	ax, 12(bp)
	mov	dx, 14(bp)	! Kernel ds (absolute address)
	mov	p_ds_desc+2, ax
	movb	p_ds_desc+4, dl ! Set base of kernel data segment

	mov	dx, ss		! Monitor ss
	xor	ax, ax		! dx:ax = Monitor stack segment
	call	seg2abs		! Minix starts with the stack of the monitor
	mov	p_ss_desc+2, ax
	movb	p_ss_desc+4, dl

	mov	ax, 8(bp)
	mov	dx, 10(bp)	! Kernel cs (absolute address)
	mov	p_cs_desc+2, ax
	movb	p_cs_desc+4, dl

	mov	dx, cs		! Monitor cs
	xor	ax, ax		! dx:ax = Monitor code segment
	call	seg2abs
	mov	p_mcs_desc+2, ax
	movb	p_mcs_desc+4, dl

	push	#MCS_SELECTOR
	push	#bios13		! Far address to BIOS int 13 support

	push	#0
	push	18(bp)		! 32 bit size of parameters on stack
	push	#0
	push	16(bp)		! 32 bit address of parameters (ss relative)

	test	_k_flags, #K_RET ! Can the kernel return?
	jz	noret386
	push	#MCS_SELECTOR
	push	#ret386		! Monitor far return address
noret386:

	push	#0
	push	#CS_SELECTOR
	push	6(bp)
	push	4(bp)		! 32 bit far address to kernel entry point

	call	real2prot	! Switch to protected mode
	mov	ax, #DS_SELECTOR ! Kernel data
	mov	ds, ax
	mov	ax, #ES_SELECTOR ! Flat 4 Gb
	mov	es, ax
	.data1	o32		! Make a far call to the kernel
	retf

! Minix-86 returns here on a halt or reboot.
ret86:
	mov	8(bp), ax
	mov	10(bp), dx	! Return value
	jmp	return

! Minix-386 returns here on a halt or reboot.
ret386:
	.data1	o32
	mov	8(bp), ax	! Return value
	call	prot2real	! Switch to real mode

return:
	mov	sp, bp		! Pop parameters
	sti			! Can take interrupts again
	call	_get_video	! MDA, CGA, EGA, ...
	movb	dh, #24		! dh = row 24
	cmp	ax, #2		! At least EGA?
	jb	is25		! Otherwise 25 rows
	push	ds
	mov	ax, #0x0040	! BIOS data segment
	mov	ds, ax
	movb	dh, 0x0084	! Number of rows on display minus one
	pop	ds
is25:
	xorb	dl, dl		! dl = column 0
	xorb	bh, bh		! Page 0
	movb	ah, #0x02	! Set cursor position
	int	0x10
	mov	cur_vid_mode, #-1  ! Minix may have messed things up

	mov	ax, 8(bp)
	mov	dx, 10(bp)	! dx-ax = return value from the kernel
	pop	bp
	ret			! Continue as if nothing happened

! Support function for Minix-386 to make a BIOS int 13 call (disk I/O).
.define bios13
bios13:
	mov	bp, sp
	call	prot2real

	mov	ax, 8(bp)	! Load parameters
	mov	bx, 10(bp)
	mov	cx, 12(bp)
	mov	dx, 14(bp)
	mov	es, 16(bp)
	sti			! Enable interrupts
	int	0x13		! Make the BIOS call
	cli			! Disable interrupts
	mov	8(bp), ax	! Save results
	mov	10(bp), bx
	mov	12(bp), cx
	mov	14(bp), dx
	mov	16(bp), es

	call	real2prot
	mov	ax, #DS_SELECTOR ! Kernel data
	mov	ds, ax
	.data1	o32
	retf			! Return to the kernel

! Switch from real to protected mode.
real2prot:
	lgdt	p_gdt_desc	! Global descriptor table
	.data1	0x0F,0x20,0xC0	! mov	eax, cr0
	.data1	o32
	mov	msw, ax		! Save cr0
	orb	al, #0x01	! Set PE (protection enable) bit
	.data1	0x0F,0x22,0xC0	! mov	cr0, eax
	jmpf	cs_prot, MCS_SELECTOR ! Set code segment selector
cs_prot:
	mov	ax, #SS_SELECTOR ! Set data selectors
	mov	ds, ax
	mov	es, ax
	mov	ss, ax

	movb	ah, #0xDF	! Code for A20 enable
	jmp	gate_A20

! Switch from protected to real mode.
prot2real:
	lidt	p_idt_desc	! Real mode interrupt vectors
	.data1	o32
	mov	ax, msw		! Saved cr0
	.data1	0x0F,0x22,0xC0	! mov	cr0, eax
	jmpf	cs_real, 0xDEAD	! Reload cs register
cs_real:
	mov	ax, #0xBEEF
ds_real:
	mov	ds, ax		! Reload data segment registers
	mov	es, ax
	mov	ss, ax

	movb	ah, #0xDD	! Code for A20 disable
	!jmp	gate_A20

! Enable (ah = 0xDF) or disable (ah = 0xDD) the A20 address line.
gate_A20:
	call	kb_wait
	movb	al, #0xD1	! Tell keyboard that a command is coming
	outb	0x64
	call	kb_wait
	movb	al, ah		! Enable or disable code
	outb	0x60
	call	kb_wait
	mov	ax, #25		! 25 microsec delay for slow keyboard chip
0:	out	0xED		! Write to an unused port (1us)
	dec	ax
	jne	0b
	ret
kb_wait:
	inb	0x64
	testb	al, #0x02	! Keyboard input buffer full?
	jnz	kb_wait		! If so, wait
	ret

.data
	.align	2

! Global descriptor tables.
	UNSET	= 0		! Must be computed

! For "Extended Memory Block Move".
x_gdt:
x_null_desc:
	! Null descriptor
	.data2	0x0000, 0x0000
	.data1	0x00, 0x00, 0x00, 0x00
x_gdt_desc:
	! Descriptor for this descriptor table
	.data2	6*8-1, UNSET
	.data1	UNSET, 0x00, 0x00, 0x00
x_src_desc:
	! Source segment descriptor
	.data2	0xFFFF, UNSET
	.data1	UNSET, 0x92, 0x00, 0x00
x_dst_desc:
	! Destination segment descriptor
	.data2	0xFFFF, UNSET
	.data1	UNSET, 0x92, 0x00, 0x00
x_bios_desc:
	! BIOS segment descriptor (scratch for int 0x15)
	.data2	UNSET, UNSET
	.data1	UNSET, UNSET, UNSET, UNSET
x_ss_desc:
	! BIOS stack segment descriptor (scratch for int 0x15)
	.data2	UNSET, UNSET
	.data1	UNSET, UNSET, UNSET, UNSET

! Protected mode descriptor table.
p_gdt:
p_null_desc:
	! Null descriptor
	.data2	0x0000, 0x0000
	.data1	0x00, 0x00, 0x00, 0x00
p_gdt_desc:
	! Descriptor for this descriptor table
	.data2	8*8-1, UNSET
	.data1	UNSET, 0x00, 0x00, 0x00
p_idt_desc:
	! Real mode interrupt descriptor table descriptor
	.data2	0x03FF, 0x0000
	.data1	0x00, 0x00, 0x00, 0x00
p_ds_desc:
	! Kernel data segment descriptor (4 Gb flat)
	.data2	0xFFFF, UNSET
	.data1	UNSET, 0x92, 0xCF, 0x00
p_es_desc:
	! Physical memory descriptor (4 Gb flat)
	.data2	0xFFFF, 0x0000
	.data1	0x00, 0x92, 0xCF, 0x00
p_ss_desc:
	! Monitor data segment descriptor (64 kb flat)
	.data2	0xFFFF, UNSET
	.data1	UNSET, 0x92, 0x00, 0x00
p_cs_desc:
	! Kernel code segment descriptor (4 Gb flat)
	.data2	0xFFFF, UNSET
	.data1	UNSET, 0x9A, 0xCF, 0x00
p_mcs_desc:
	! Monitor code segment descriptor (64 kb flat)
	.data2	0xFFFF, UNSET
	.data1	UNSET, 0x9A, 0x00, 0x00

.bss
	.comm	old_vid_mode, 2		! Video mode at startup
	.comm	cur_vid_mode, 2		! Current video mode
	.comm	msw, 4			! Saved machine status word (cr0)
