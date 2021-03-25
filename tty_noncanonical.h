#ifndef TTY_NONCANONICAL_H
#define TTY_NONCANONICAL_H

#include <termios.h>
#include <unistd.h>

int tty_set_noncannonical(int fd, int speed, int parity, struct termios *prevconfig);

#endif
