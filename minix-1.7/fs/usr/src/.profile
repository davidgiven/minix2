# Login shell profile.

# Environment.
umask 022
PATH=/usr/local/bin:/bin:/usr/bin
PS1="! "
export PATH

# Erase character, erase line, and interrupt keys.
stty erase '^H' kill '^U' intr '^?'

# Check terminal type.
case $TERM in
dialup|unknown|network)
	echo -n "Terminal type? ($TERM) "; read term
	TERM="${term:-$TERM}"
esac

# Shell configuration.
case "$0" in *ash) . $HOME/.ashrc;; esac
