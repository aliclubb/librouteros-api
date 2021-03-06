/*
    librouteros-api - Connect to RouterOS devices using official API protocol
    Copyright (C) 2013, Håkon Nessjøen <haakon.nessjoen@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "../librouteros.h"

struct ros_connection *conn;
volatile int do_continue = 0;
volatile int tasks = 0;
int id=0;

void handleInterface(struct ros_result *result) {
	static int calls = 0;

	if (result->re && calls++ == 0) {
		printf("Traffic on %s:\n", ros_get(result, "=name"));
	}

	if (result->re) {
		printf("In: %20s Out: %20s\n", ros_get(result, "=rx-bits-per-second"), ros_get(result, "=tx-bits-per-second"));			
	}

	if (result->done) {
		/* This is received when the device has stopped sending
		   info. (in this case after /cancel)
		*/
		printf("\n");
		tasks--;
	}
	if (result->re && calls == 5) {
		/* Tell device to stop sending info */
		ros_cancel(conn, id);
	}

	ros_result_free(result);
}

void handleUptime(struct ros_result *result) {
	if (result->re) {
		printf("Resources:\n  Uptime: %s\n  CPU load: %s%%\n\n", ros_get(result, "=uptime"), ros_get(result, "=cpu-load"));
	}

	if (result->done) {
		tasks--;
	}
	ros_result_free(result);
}

int main(int argc, char **argv) {
	fd_set read_fds;
	srand(time(0));

	if (argc < 4) {
		fprintf(stderr, "Usage: %s <ip> <user> <password>\n", argv[0]);
		return 1;
	}

	conn = ros_connect(argv[1], ROS_PORT); 
	if (conn == NULL) {
		fprintf(stderr, "Error connecting to %s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	
	ros_set_type(conn, ROS_EVENT);

	if (ros_login(conn, argv[2], argv[3])) {
		struct timeval timeout;
		struct ros_sentence *sentence;

		/* Static parameters/words */
		id = ros_send_command_cb(conn, handleInterface, "/interface/monitor-traffic", "=interface=ether1", NULL);
		tasks++;

		/* Dynamic amount of parameters/words */
		sentence = ros_sentence_new();
		ros_sentence_add(sentence, "/system/resource/print");
		ros_sentence_add(sentence, "=.proplist=uptime,cpu-load");
		ros_send_sentence_cb(conn, handleUptime, sentence);
		ros_sentence_free(sentence);
		tasks++;

		do_continue = 1;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		while (do_continue) {
			int reads;
			FD_ZERO(&read_fds);
			FD_SET(conn->socket, &read_fds);

			reads = select(conn->socket + 1, &read_fds, NULL, NULL, &timeout);
			if (reads > 0) {
				if (FD_ISSET(conn->socket, &read_fds)) {
					/* handle incoming data with specified callback */
					ros_runloop_once(conn, NULL);
				}
			}
			if (tasks == 0) {
				ros_disconnect(conn);
				return 0;
			}
		}

		ros_disconnect(conn);
	} else {
		fprintf(stderr, "Error logging in\n");
		return 1;
	}

	return 0;
}
