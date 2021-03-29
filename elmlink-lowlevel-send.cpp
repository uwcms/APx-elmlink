#include "baudparse.h"
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

	BaudRate baud = BaudRate::find_setting(argv[2]);
	if (baud.rate == 0) {
		printf("Baud rate \"%s\" not supported.\n", argv[2]);
		printf("\n");
		printf("I support:");
		for (auto setting : BaudRate::baud_settings)
			printf(" %d", setting.rate);
		printf("\n");
		return 1;
	}

	int uartfd = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (uartfd == -1) {
		perror("Failed to open serial endpoint\n");
		return 1;
	}
	tty_set_noncannonical(uartfd, baud.flag, 0, NULL);

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
