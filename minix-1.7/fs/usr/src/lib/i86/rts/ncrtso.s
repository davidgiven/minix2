! This is the C run-time start-off routine.  It's job is to take the
! arguments as put on the stack by EXEC, and to parse them and set them up the
! way _main expects them.
! It also initializes _environ when this variable isn't defined by the
! programmer.  The detection of whether _environ belong to us is rather
! simplistic.  We simply check for some magic value, but there is no other
! way.

.extern _main, _exit, crtso, __penvp
.extern begtext, begdata, begbss, endtext, enddata, endbss
.text
begtext:
crtso:		mov	bx,sp
		mov	cx,(bx)
		add	bx,*2
		mov	ax,cx
		inc	ax
		shl	ax,#1
		add	ax,bx
		mov	__penvp,ax	! save envp in __envp

		! Test whether address of environ < address of end.
		! This is done for separate I&D systems.
		mov	dx,#_environ
		cmp	dx,#__end
		jae	1f
		cmp	_environ,#21331		! is it our _environ?
		jne	1f
		mov	_environ,ax
1:
		push	ax	! push environ
		push	bx	! push argv
		push	cx	! push argc
		xor	bp,bp	! clear bp for traceback of core files
		call	_main
		add	sp,*6
		push	ax	! push exit status
		call	_exit

.data
begdata:
__penvp:	.data2 0
.bss
begbss:
