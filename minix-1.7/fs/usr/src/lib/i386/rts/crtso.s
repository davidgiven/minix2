! This is the C run-time start-off routine.  It's job is to take the
! arguments as put on the stack by EXEC, and to parse them and set them up the
! way _main expects them.
! It also initializes _environ when this variable isn't defined by the
! programmer.  The detection of whether _environ belong to us is rather
! simplistic.  We simply check for some magic value, but there is no other
! way.

.sect .text; .sect .rom; .sect .data; .sect .bss

.define begtext, begdata, begbss
.sect .text
begtext:
.sect .rom
begrom:
.sect .data
begdata:
.sect .bss
begbss:

.define crtso, ___main, __penvp, __fpu_present
.extern _main, _exit
.sect .text
crtso:
	xor     ebp, ebp		! clear for backtrace of core files
	mov     eax, (esp)		! argc
	lea     edx, 4(esp)		! argv
	lea     ecx, 8(esp)(eax*4)	! envp

	mov	(__penvp), ecx		! save envp in __envp

	! Test whether address of environ < address of end.
	! This is done for separate I&D systems.
	mov	ebx, _environ
	cmp	ebx, __end
	jae	0f
	cmp	(_environ), 0x53535353	! is it our _environ?
	jne	0f
	mov	(_environ), ecx
0:
	push	ecx			! push environ
	push	edx			! push argv
	push	eax			! push argc

	! Test the EM bit of the MSW to determine if an FPU is present and
	! set __fpu_present if one is found.
	smsw	ax
	testb	al, 0x4			! EM bit in MSW
	setz	al			! True if not set
	movb	(__fpu_present), al

	call	_main			! main(argc, argv, envp)

	push	eax			! push exit status
	call	_exit

	hlt				! force a trap if exit fails

___main:				! for GCC
	ret

.sect .rom
	.data4	0			! Common I&D: *NULL == 0
.sect .bss
	.comm	__penvp, 4		! Environment vector
	.comm	__fpu_present, 4	! FPU present flag

.extern endtext				! Force loading of end labels.
