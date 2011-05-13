/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/time.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <unistd.h>
#include <pthread.h>

#define TCP_BUF_SIZE 65536*16

static int accept_fd = -1;
static int socket_fd = -1;
static int isRunning = 0;
static int isReceving = 0;
static pthread_t AcceptThread;
static pthread_t RecvThread;

static void* RecvThreadProc(void* params);
static void* AcceptThreadProc(void* params);

int initChunkPuller(const int port)
{
	struct sockaddr_in servaddr;
	int r;
	int fd;

#ifdef _WIN32
	{
		WORD wVersionRequested;
		WSADATA wsaData;
		int err;

		wVersionRequested = MAKEWORD(2, 2);
		err = WSAStartup(wVersionRequested, &wsaData);
		if (err != 0) {
			fprintf(stderr, "WSAStartup failed with error: %d\n", err);
			return NULL;
		}
	}
#endif
  
	accept_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (accept_fd < 0) {
		perror("cannot create socket!\n");
		return -1;
	}
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(port);
	r = bind(accept_fd, (struct sockaddr *)&servaddr, sizeof(servaddr));
	
	fprintf(stderr,"listening on port %d\n", port);
	
	if(pthread_create( &AcceptThread, NULL, &AcceptThreadProc, NULL) != 0)
	{
		fprintf(stderr,"TCP-INPUT-MODULE: could not start accepting thread!!\n");
		return -1;
	}
	
	return accept_fd;
}

static void* AcceptThreadProc(void* params)
{
	struct sockaddr_storage their_addr;
    socklen_t addr_size;
    int fd = -1;
    
    isRunning = 1;

    listen(accept_fd, 10);
    
    while(isRunning)
    {
		printf("trying to accept connection...\n");
		fd = accept(accept_fd, (struct sockaddr *)&their_addr, &addr_size);
		printf("connection requested!!!\n");
		if(socket_fd == -1)
		{
			socket_fd = fd;
			isReceving = 1;
		}
		else
		{
			isReceving = 0;
			pthread_join(RecvThread, NULL);
			pthread_detach(RecvThread);
			socket_fd = fd;
		}
		if(pthread_create( &RecvThread, NULL, &RecvThreadProc, NULL) != 0)
		{
			fprintf(stderr,"TCP-INPUT-MODULE: could not start receveing thread!!\n");
			return NULL;
		}
	}
	
	return NULL;
}

static void* RecvThreadProc(void* params)
{
	int ret = -1;
	uint32_t fragment_size = 0;
	uint8_t* buffer = (uint8_t*) malloc(TCP_BUF_SIZE);

	fprintf(stderr,"TCP-INPUT-MODULE: receive thread created\n");

	while(isReceving) {
		ret=recv(socket_fd, &fragment_size, sizeof(uint32_t), 0);
		fragment_size = ntohl(fragment_size);

fprintf(stderr, "TCP-INPUT-MODULE: received %d bytes. Fragment size: %d\n", ret, fragment_size);
		if(ret <= 0) {
			break;
		}

		ret=recv(socket_fd, buffer, fragment_size, 0);
fprintf(stderr, "TCP-INPUT-MODULE: received %d bytes.\n", ret);
		while(ret < fragment_size)
			ret+=recv(socket_fd, buffer+ret, fragment_size-ret, 0);
		
		if(enqueueBlock(buffer, fragment_size))
			fprintf(stderr, "TCP-INPUT-MODULE: could not enqueue a received chunk!! \n");
	}
	free(buffer);
	close(socket_fd);
	socket_fd = -1;

	return NULL;
}

void finalizeChunkPuller()
{
	isRunning = 0;
	pthread_join(AcceptThread, NULL);
	pthread_detach(AcceptThread);
	
	if(socket_fd > 0)
		close(socket_fd);

	close(accept_fd);
}
