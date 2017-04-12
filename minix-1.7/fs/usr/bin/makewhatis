#!/bin/sh
#
# makewhatis 1.4 - make whatis(5) database.	Author: Kees J. Bot.
#
# Make the whatis database of a man directory from the manual pages.  This
# is no trivial thing, because next to the normal man pages in the man[1-8]
# directories, we have the "book" manual pages in man0, and we also have a
# weird collection of formatted pages in the cat[1-8] directories that have
# no formatted counterparts.  Add to this confusion different layouts in
# each different cat directory...
#
# Better not make a catman(8) yet.

case $1 in
-*)	set -$- x x
esac

case $# in
1)	;;
*)	echo "Usage: $0 <mandir>" >&2
	exit 1
esac

cd $1 || exit

{
	# First pass, gathering the .SH NAME lines in various forms.

	# First the man[1-8] directories, they are as it should be.  Look
	# how simple this script could have been.
	for chap in 1 2 3 4 5 6 7 8
	do
		for page in man$chap/*.$chap
		do
		   if	test -f "$page"; then	# (Old sh barfs on 'continue')

			sed -e 's/	/ /g
				/^\.SH NAME/,/^\.SH /!d
				/^\.SH /d
				s/\\f.//g	# should not be needed
				s/\\s[+-].//g
				s/\\s.//g
				s/\\//
				'"s/ - / ($chap) - /" < "$page"
		   fi
		done
	done

	# The Minix "Book style" manual pages, also not too difficult.
	for page in man0/*
	do
	   if	test -f "$page"; then

		sed -e 's/	/ /g
			/^\.CD /!d
			s/^[^"]*"//
			s/"[^"]*$//
			s/\\(en/-/g
			s/\\f.//g
			s/\\s[+-].//g
			s/\\s.//g
			s/\\\*(M2/MINIX/g
			s/\\//
			'"s/ - / (0) - /" < "$page"
	   fi
	done

	# The commands in cat[168], look for "Command:", stop at a line
	# that doesn't start with a space.  Ignore pages that have an
	# unformatted page too.  (For if one does run catman(8)).
	for chap in 1 6 8
	do
		for page in cat$chap/*.$chap
		do
		   if	test -f "$page"; then
		   if	test ! -f man$chap/`basename "$page"`; then

			sed -e 's/	/ /g
				/^Command:/,/^[^ ]/!d
				s/^Command:/ /
				/^[^ ]/d
				s/\\//
				'"s/ - / ($chap) - /" < "$page" | \
			tr '\012' ' '
			echo
		   fi
		   fi
		done
	done

	# The system calls and libraries in cat[23], the description may be
	# found after "Name:", "NAME", "SYSTEM CALLS", or "SUBROUTINES".
	for chap in 2 3
	do
		for page in cat$chap/*.$chap
		do
		   if	test -f "$page"; then
		   if	test ! -f man$chap/`basename "$page"`; then

			sed -e 's/	/ /g
				/^Name:/,/^[^ ]/!d
				s/^Name:/ /
				/^[^ ]/d
				'"s/ - / ($chap) - /" < "$page" | \
			tr '\012' ' '
			echo

			sed -e 's/	/ /g
				/^NAME/,/^[^ ]/!d
				s/^NAME/ /
				/^[^ ]/d
				'"s/ - / ($chap) - /" < "$page" | \
			tr '\012' ' '
			echo

			sed -e 's/	/ /g
				/^SYSTEM CALLS/,/^[^ ]/!d
				s/^SYSTEM CALLS/ /
				/^[^ ]/d
				'"s/ - / ($chap) - /" < "$page" | \
			tr '\012' ' '
			echo

			sed -e 's/	/ /g
				/^SUBROUTINES/,/^[^ ]/!d
				s/^SUBROUTINES/ /
				/^[^ ]/d
				'"s/ - / ($chap) - /" < "$page" | \
			tr '\012' ' '
			echo
		   fi
		   fi
		done
	done

	# Cat4 contains file formats, this should really be cat5.  (Cat4 is
	# for devices.)  But as cat5 contains nothing but junk, we can treat
	# the two directories the same and hope that things get cleared up
	# one day.  The whatis line can be found after "File:", but ends at
	# an empty line for a change.
	for chap in 4 5
	do
		for page in cat$chap/*.$chap
		do
		   if	test -f "$page"; then
		   if	test ! -f man$chap/`basename "$page"`; then

			sed -e 's/	/ /g
				/^File:/,/^ *$/!d
				s/^File:/ /
				'"s/ - / ($chap) - /" < "$page" | \
			tr '\012' ' '
			echo
		   fi
		   fi
		done
	done

	# Do not bother with cat7 yet.
} | {
	# Second pass, remove empty lines, leading and trailing spaces,
	# multiple spaces to one space, remove lines without a dash.
	sed -e 's/  */ /g
		s/^ //
		s/ $//
		/^$/d
		/-/!d'
} | {
	# Third pass, sort by chapter ("section" should SysV'ers say.)
	sort -t'(' +1 -o whatis
}
