#include "elmlink_protocol.h"
#include "tty_noncanonical.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <termios.h>
#include <unistd.h>

static inline uint8_t parse_byte(const char *str) {
	char *endptr;
	uint32_t result = strtoul(str, &endptr, 0);
	if (result > 0xff)
		throw std::range_error("Value too large.");
	return result;
}

int main(int argc, char *argv[]) {
	if (argc < 5) {
		fputs("Usage: elmlink-lowlevel-send /dev/ttyUL1 115200 $channel $hexbyte $hexbyte $hexbyte ...\n", stderr);
		return 1;
	}

	std::string uart_baud_str = argv[2];
	int baud_flag = 0;
	if (uart_baud_str == "9600")
		baud_flag = B9600;
	else if (uart_baud_str == "19200")
		baud_flag = B19200;
	else if (uart_baud_str == "115200")
		baud_flag = B115200;
	else {
		printf("Baud rate must be 9600, 19200 or 115200.\n");
		return 1;
	}

	int uartfd = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (uartfd == -1) {
		perror("Failed to open serial endpoint\n");
		return 1;
	}
	tty_set_noncannonical(uartfd, baud_flag, 0, NULL);

	// We now have the UART socket set up.

	uint8_t channel;
	uint8_t data[argc - 4];

	try {
		channel = parse_byte(argv[3]);
		for (int i = 4; i < argc; ++i)
			data[i - 4] = parse_byte(argv[i]);
	}
	catch (std::range_error &e) {
		printf("Unable to parse arguments.\n");
		return 1;
	}

	ELMLink::Packet packet(channel, data, argc - 4);

	std::string send_buffer = packet.serialize();

	while (send_buffer.size()) {
		int rv = write(uartfd, send_buffer.data(), send_buffer.size());
		if (rv > 0)
			send_buffer.erase(0, rv);
		if (rv < 0)
			return 1;
	}
	return 0;
}
