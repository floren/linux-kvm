#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "kvm/util.h"
#include "kvm/kvm.h"
#include "kvm/sockterm.h"

int sockterm(u32 port)      
{
	int 	 sd, sd_current;
	uint 	 addrlen;
	struct   sockaddr_in sin;
	struct   sockaddr_in pin;

	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		die("socket");
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);

	if (bind(sd, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
		die("bind");
	}

	if (listen(sd, 5) == -1) {
		die("listen");
	}
        addrlen = sizeof(pin); 
	if ((sd_current = accept(sd, (struct sockaddr *)  &pin, &addrlen)) == -1) {
		die("accept");
	}

	printf("Dup'ing socket %d over in/out\n", sd_current);
	dup2(sd_current, 0);
	dup2(sd_current, 1);
	return 0;
}
