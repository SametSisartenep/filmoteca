#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <utf.h>
#include <fmt.h>
#include "dat.h"
#include "fns.h"

extern int debug;

void
sysfatal(char *fmt, ...)
{
	va_list arg;
	char buf[2048];

	va_start(arg, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	fprint(2, "%s\n", buf);
	exit(1);
}

void *
emalloc(ulong n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		sysfatal("malloc: %r");
	memset(p, 0, n);
	return p;
}
void *
erealloc(void *ptr, ulong n)
{
	void *p;

	p = realloc(ptr, n);
	if(p == nil)
		sysfatal("realloc: %r");
	return p;
}

sockaddr_in*
mkinetsa(char *ipaddr, int port)
{
	sockaddr_in *sa;

	sa = emalloc(sizeof(sockaddr_in));
	sa->sin_family = AF_INET;
	if(inet_pton(AF_INET, ipaddr, &sa->sin_addr.s_addr) <= 0)
		sysfatal("inet_pton: %r");
	sa->sin_port = htons(port);
	return sa;
}

int
listentcp(int port)
{
	sockaddr_in *addr;
	int fd;

	addr = mkinetsa("0.0.0.0", port);
	if((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
		return -1;
	if(bind(fd, (sockaddr*)addr, sizeof(sockaddr_in)) < 0){
		close(fd);
		return -1;
	}
	if(listen(fd, 128) < 0){
		close(fd);
		return -1;
	}
	if(debug)
		fprint(2, "listening on tcp!%s!%d\n", inet_ntoa(addr->sin_addr), port);
	free(addr);
	return fd;
}

int
bindudp(int port)
{
	sockaddr_in *addr;
	int fd;

	addr = mkinetsa("0.0.0.0", port);
	if((fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
		return -1;
	if(bind(fd, (sockaddr*)addr, sizeof(sockaddr_in)) < 0){
		close(fd);
		return -1;
	}
	if(debug)
		fprint(2, "listening on udp!%s!%d\n", inet_ntoa(addr->sin_addr), port);
	free(addr);
	return fd;
}

int
acceptcall(int lfd, char *caddr, int caddrlen)
{
	sockaddr_in addr;
	uint addrlen;
	int fd, port;
	char *cs;

	memset(&addr, 0, sizeof(sockaddr_in));
	addrlen = sizeof(sockaddr_in);
	if((fd = accept(lfd, (sockaddr*)&addr, &addrlen)) < 0)
		return -1;
	cs = inet_ntoa(addr.sin_addr);
	port = ntohs(addr.sin_port);
	snprint(caddr, caddrlen, "tcp!%s!%d", cs, port);
	return fd;
}

u32int
get32(uchar *p)
{
	return (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
}

void
put32(uchar *p, u32int v)
{
	p[0] = v>>24;
	p[1] = v>>16;
	p[2] = v>>8;
	p[3] = v;
}
