#include <stdio.h>
#include <string.h>
#include <termios.h>

// http://tldp.org/HOWTO/Serial-Programming-HOWTO/x115.html
int tty_set_noncannonical(int fd, int speed, int parity, struct termios *prevconfig) {
	struct termios tty;

	// Save previous configuration before changing it
	if (prevconfig) {
		tcgetattr(fd, prevconfig);
	}

	memset(&tty, 0, sizeof(tty));
	tty.c_cflag = speed | parity | CS8 | CLOCAL | CREAD;
	tty.c_iflag = IGNCR;
	tty.c_oflag = 0;
	tty.c_lflag = 0; // Non-canonical

	// True nonblocking.
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 0;

	tcflush(fd, TCIFLUSH);
	if (tcsetattr(fd, TCSANOW, &tty) != 0) {
		perror("Error from tcsetattr");
		return -1;
	}

	return 0;
}
