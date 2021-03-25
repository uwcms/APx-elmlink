#include "elmlink_protocol.h"
#include "tty_noncanonical.h"
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* This protocol implementation provides the following guarantees:
 *
 * GUARANTEED: No permanent sync loss (line protocol synchronizes on every packet) [protocol]
 * GUARANTEED: No corrupt packets (checksum) [protocol]
 * GUARANTEED: No out of order packets per client (enqueued in the order received) [implementation]
 *
 * NOT GUARANTED: Your packets will arrive (there is no ACK/retry mechanism) [protocol]
 *
 * tl;dr: Your packets will arrive intact and in order, if they arrive, but
 *        arrival itself is not guaranteed.
 */

// After this many clients, we will not accept() on that channel.
#define MAX_CLIENTS_PER_CHANNEL 16 // clients

// After this much data pending to send to a client, we will discard new packets.
#define MAX_CLIENT_SENDBUF (32 * ELMLink::MAX_DECODED_PACKET_LENGTH) // bytes

/* After this much data pending to send to the IPMC, we will block writing clients.
 *
 * With the current implementation, each client can get one packet in per loop.
 * This limit is applied only at the beginning of each loop (affecting the
 * select() rfdset), so the actual buffer can exceed this value, but no client
 * is unfairly prioritized (at least in packet count, if not byte count).
 *
 * Use of config.baud here will result in backpressure after more than 1
 * physical second of data is enqueued.
 */
#define MAX_UART_SENDBUF (config.baud) // bytes

// This channel ID will be used to request and receive the channel index.
#define CHANNEL_INDEX_CHANNEL 0

// Every so often, we will request a refresh of the channel index, just in case.
#define CHANNEL_INDEX_REFRESH_PERIOD 30 // seconds

// This directory will have its contents erased, and will be used for client sockets.
#define CHANNEL_SOCKET_DIR "/var/run/elmlinkd"

// The chmod to apply to newly instantiated channels by default.
// Since normally the directory permissions will define access, we default to 0777.
#define CHANNEL_SOCKET_CHMOD 0777

class Client {
public:
	int fd;
	std::list<std::string> send_buffer;
	size_t send_buffer_size;
	Client(int fd) : fd(fd), send_buffer_size(0){};
	virtual ~Client() {
		close(this->fd);
	}

	void join_fdset(fd_set *rfdset, fd_set *wfdset, int &maxfd) {
		if (rfdset) {
			FD_SET(this->fd, rfdset);
		}
		if (wfdset && this->send_buffer_size) {
			FD_SET(this->fd, wfdset);
		}
		if ((rfdset || wfdset) && this->fd > maxfd)
			maxfd = this->fd;
	}
};

class Channel {
public:
	std::string name;
	std::string path;
	uint8_t channel_number;
	int listenfd;
	std::list<std::shared_ptr<Client>> clients;

	Channel(int channel_number, std::string name)
	    : name(name), channel_number(channel_number), listenfd(listenfd) {

		this->listenfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
		if (this->listenfd < 0)
			throw std::runtime_error("socket() failed");

		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));

		// Filling server information
		addr.sun_family = AF_UNIX;
		this->path = std::string(CHANNEL_SOCKET_DIR) + "/" + this->name;
		strncpy(addr.sun_path, this->path.c_str(), sizeof(addr.sun_path) - 1);

		// Bind the socket with the server address
		unlink(this->path.c_str()); // Insurance.
		if (bind(this->listenfd, (const struct sockaddr *)&addr, sizeof(sockaddr_un)) < 0) {
			close(this->listenfd);
			throw std::runtime_error("bind() failed");
		}

		fchmod(this->listenfd, CHANNEL_SOCKET_CHMOD);

		if (listen(this->listenfd, 1) < 0) {
			close(listenfd);
			throw std::runtime_error("listen() failed");
		}
	};

	virtual ~Channel() {
		unlink(this->path.c_str());
		close(this->listenfd);
	}
};

class Configuration {
public:
	std::string uartpath;
	int uartfd;
	unsigned int baud;
	std::map<uint8_t, std::shared_ptr<Channel>> channels;
	Configuration() : uartfd(-1), baud(0){};
};

void sync_available_channels(Configuration &config, const std::map<uint8_t, std::string> &channel_index) {
	std::set<uint8_t> known_channel_numbers;
	std::set<std::string> known_channel_names;

	FILE *indexfile = fopen((std::string(CHANNEL_SOCKET_DIR) + "/.index~").c_str(), "w");
	if (indexfile >= 0)
		fprintf(indexfile, "UART %s %d\n", config.uartpath.c_str(), config.baud);
	for (auto channel_data : channel_index) {
		if (channel_data.first == CHANNEL_INDEX_CHANNEL)
			continue; // This one doesn't get instantiated.

		known_channel_numbers.insert(channel_data.first);
		known_channel_names.insert(channel_data.second);

		if (indexfile >= 0)
			fprintf(indexfile, "CHANNEL %hhu %s\n", channel_data.first, channel_data.second.c_str());

		if (config.channels.count(channel_data.first) && config.channels.at(channel_data.first)->name != channel_data.second) {
			// This channel doesn't match.  It's a reused ID.  Erase it.
			// The channel's destructor will clean it up.
			config.channels.erase(channel_data.first);
		}

		if (config.channels.count(channel_data.first) == 0) {
			// No channel here!  Instantiate one.

			if (channel_data.first >= 0x80) {
				fprintf(stderr, "Channel number out of range in sync: %hhu\n", channel_data.first);
			}
			else {
				config.channels.emplace(channel_data.first, std::make_shared<Channel>(channel_data.first, channel_data.second));
			}
		}
	}

	if (indexfile) {
		fclose(indexfile);
		rename((std::string(CHANNEL_SOCKET_DIR) + "/.index~").c_str(), (std::string(CHANNEL_SOCKET_DIR) + "/.index").c_str());
	}
	else {
		unlink((std::string(CHANNEL_SOCKET_DIR) + "/.index").c_str());
	}

	for (auto it = config.channels.begin(); it != config.channels.end(); /* handled in body */) {
		if (known_channel_numbers.count(it->first) == 0) {
			// This channel must have been removed.  Let's clean it up.
			it = config.channels.erase(it);
		}
		else {
			++it;
		}
	}

	DIR *channeldir = opendir(CHANNEL_SOCKET_DIR);
	if (channeldir) {
		struct dirent *file;
		while (file = readdir(channeldir)) {
			std::string filename = file->d_name;
			if (filename == "." || filename == ".." || filename == ".index")
				continue;
			else if (known_channel_names.count(filename) == 0) {
				// An unknown file is in our socket dir.  Let's clear it out.
				std::string fullpath = std::string(CHANNEL_SOCKET_DIR) + "/" + filename;
				unlink(fullpath.c_str());
			}
		}
		closedir(channeldir);
	}
}

int main(int argc, char *argv[]) {
	Configuration config;
	if (argc != 3) {
		fputs("Usage: elmlinkd /dev/ttyUL1 115200\n", stderr);
		return 1;
	}

	config.uartpath = argv[1];
	char resolved_uartpath[PATH_MAX];
	if (realpath(argv[1], resolved_uartpath))
		config.uartpath = resolved_uartpath;

	std::string uart_baud_str = argv[2];
	int baud_flag = 0;
	if (uart_baud_str == "9600") {
		baud_flag = B9600;
		config.baud = 9600;
	}
	else if (uart_baud_str == "19200") {
		baud_flag = B19200;
		config.baud = 19200;
	}
	else if (uart_baud_str == "115200") {
		baud_flag = B115200;
		config.baud = 115200;
	}
	else {
		printf("Baud rate must be 9600, 19200 or 115200.\n");
		return 1;
	}

	mkdir(CHANNEL_SOCKET_DIR, 0777);

	config.uartfd = open(config.uartpath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (config.uartfd == -1) {
		perror("Failed to open serial endpoint\n");
		return 1;
	}
	tty_set_noncannonical(config.uartfd, baud_flag, 0, NULL);

	// We now have the UART socket set up.

	std::string receive_buffer;
	std::string send_buffer;
	bool need_channel_sync = true;
	std::map<uint8_t, std::string> channel_index;

	time_t last_index_refresh_request = 0; // We need to request a sync at startup.

	while (true) {
		// This will open any new channels, close any old ones, and clean up the
		// CHANNEL_SOCKET_PATH dir.
		if (need_channel_sync) {
			need_channel_sync = false;
			sync_available_channels(config, channel_index);
		}

		// We will periodically request a channel index sync from the IPMC.
		if (last_index_refresh_request + CHANNEL_INDEX_REFRESH_PERIOD < time(NULL)) {
			send_buffer.append(ELMLink::Packet(CHANNEL_INDEX_CHANNEL, "INDEX_REQUEST").serialize());
			last_index_refresh_request = time(NULL);
			fflush(stdout);
		}

		fd_set rfds, wfds;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);

		FD_SET(config.uartfd, &rfds);
		if (send_buffer.size())
			FD_SET(config.uartfd, &wfds);

		int maxfd = config.uartfd;

		bool accepting_client_packets = send_buffer.size() < MAX_UART_SENDBUF;

		for (auto channel_it : config.channels) {
			if (channel_it.second->clients.size() < MAX_CLIENTS_PER_CHANNEL) {
				FD_SET(channel_it.second->listenfd, &rfds);
				if (channel_it.second->listenfd > maxfd)
					maxfd = channel_it.second->listenfd;
			}

			for (auto client : channel_it.second->clients)
				client->join_fdset(accepting_client_packets ? &rfds : NULL, &wfds, maxfd);
		}

		int rv = select(maxfd + 1, &rfds, &wfds, NULL, NULL);
		if (rv == -1) {
			perror("select() failed");
			exit(1);
		}

		if (FD_ISSET(config.uartfd, &rfds)) {
			uint8_t buf[16 + ELMLink::MAX_ENCODED_PAYLOAD_LENGTH]; // A full packet, and header, and a little bit.
			int rv = read(config.uartfd, buf, 16 + ELMLink::MAX_ENCODED_PAYLOAD_LENGTH);
			if (rv > 0) {
				receive_buffer.append((char *)buf, rv);
				ELMLink::Packet packet;
				while (packet.digest(receive_buffer)) {
					if (packet.channel == CHANNEL_INDEX_CHANNEL) {
						// We have an index packet.  Let's parse it as such.
						channel_index = ELMLink::Packet::decode_channel_index_update_packet(packet.data);
						need_channel_sync = true;
					}
					if (config.channels.count(packet.channel)) {
						for (auto client : config.channels.at(packet.channel)->clients) {
							client->send_buffer.push_back(packet.data);
							client->send_buffer_size += packet.data.size();
							// Enqueue complete.  It'll get pushed out next loop.
						}
					}
				}
			}
		}
		if (FD_ISSET(config.uartfd, &wfds) && send_buffer.size()) {
			/* There's really no clear documentation on how to make non-blocking
             * serial writes in linux... Stackoverflow and everything I see says
             * "use another thread", but that seems wrong.  For now, I'll trust
             * in O_NONBLOCK, and pick buffers that will result in 0.01 seconds
             * of blocking if it comes to that.
			 */
			size_t write_size = send_buffer.size();
			if (write_size > config.baud / 100)
				write_size = config.baud / 100;
			int rv = write(config.uartfd, send_buffer.data(), write_size);
			if (rv > 0)
				send_buffer.erase(0, rv);
		}

		for (auto channel_it : config.channels) {
			if (FD_ISSET(channel_it.second->listenfd, &rfds)) {
				int fd = accept(channel_it.second->listenfd, NULL, NULL);
				if (fd > -1)
					channel_it.second->clients.emplace_back(std::make_shared<Client>(fd));
			}
			for (auto client_it = channel_it.second->clients.begin(); client_it != channel_it.second->clients.end(); /* handled in body */) {
				if (FD_ISSET((*client_it)->fd, &rfds)) {
					uint8_t buf[ELMLink::MAX_DECODED_PACKET_LENGTH];
					int rv = recv((*client_it)->fd, buf, ELMLink::MAX_DECODED_PACKET_LENGTH, MSG_DONTWAIT);
					if (rv == 0) {
						// EOF. Clean up.
						close((*client_it)->fd);
						client_it = channel_it.second->clients.erase(client_it);
						continue;
					}
					else if (rv > 0)
						send_buffer.append(ELMLink::Packet(channel_it.second->channel_number, buf, rv).serialize());
				}
				if (FD_ISSET((*client_it)->fd, &wfds) && (*client_it)->send_buffer.size()) {
					std::string buf = (*client_it)->send_buffer.front();

					errno = 0;
					int rv = send((*client_it)->fd, buf.data(), buf.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
					if (rv >= 0) {
						(*client_it)->send_buffer_size -= buf.size();
						(*client_it)->send_buffer.pop_front();
					}
					if (errno == EPIPE) {
						// EPIPE. Clean up.
						close((*client_it)->fd);
						client_it = channel_it.second->clients.erase(client_it);
						continue;
					}
				}
				++client_it;
			}
		}
	}

	return 1; // What? We're not supposed to get here.
}
