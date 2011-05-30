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

//handle threads through SDL
#include <SDL.h>
#include <SDL_thread.h>

#define TCP_BUF_SIZE 65536*16

static int accept_fd = -1;
static int socket_fd = -1;
static int isRunning = 0;
static int isReceving = 0;
static SDL_Thread *AcceptThread;
static SDL_Thread *RecvThread;

static int RecvThreadProc(void* params);
static int AcceptThreadProc(void* params);

int initChunkPuller(const int port)
{
	struct sockaddr_in servaddr;
	int r;

#ifdef _WIN32
	{
		WORD wVersionRequested;
		WSADATA wsaData;
		int err;

		wVersionRequested = MAKEWORD(2, 2);
		err = WSAStartup(wVersionRequested, &wsaData);
		if (err != 0) {
			fprintf(stderr, "WSAStartup failed with error: %d\n", err);
			return -1;
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
	if (r < 0) {
		perror("cannot bind to port!\n");
		return -1;
	}
	
	fprintf(stderr,"listening on port %d\n", port);
	
	if((AcceptThread = SDL_CreateThread(&AcceptThreadProc, NULL)) == 0)
	{
		fprintf(stderr,"TCP-INPUT-MODULE: could not start accepting thread!!\n");
		return -1;
	}
	
	return accept_fd;
}

static int AcceptThreadProc(void* params)
{
    int fd = -1;
    
    isRunning = 1;

    listen(accept_fd, 10);
    
    while(isRunning)
    {
		printf("TCP-INPUT-MODULE: waiting for connection...\n");
		fd = accept(accept_fd, NULL, NULL);
		if (fd < 0) {
			perror("TCP-INPUT-MODULE: accept error");
			continue;
		}
		printf("TCP-INPUT-MODULE: accept: fd =%d\n", fd);
		if(socket_fd == -1)
		{
			socket_fd = fd;
			isReceving = 1;
		}
		else
		{
			isReceving = 0;
			printf("TCP-INPUT-MODULE: waiting for receive thread to terminate...\n");
			SDL_WaitThread(RecvThread, NULL);
			printf("TCP-INPUT-MODULE: receive thread terminated\n");
			socket_fd = fd;
		}
		if((RecvThread = SDL_CreateThread(&RecvThreadProc, NULL)) == 0)
		{
			fprintf(stderr,"TCP-INPUT-MODULE: could not start receveing thread!!\n");
			return NULL;
		}
	}
	
	return NULL;
}

static int RecvThreadProc(void* params)
{
	int ret = -1;
	uint32_t fragment_size = 0;
	uint8_t* buffer = (uint8_t*) malloc(TCP_BUF_SIZE);

	fprintf(stderr,"TCP-INPUT-MODULE: receive thread created\n");

	while(isReceving) {
		int b;

		ret=recv(socket_fd, &fragment_size, sizeof(uint32_t), 0);
		fragment_size = ntohl(fragment_size);

		if(ret < 0) {
			perror("TCP-INPUT-MODULE: recv error:");
			break;
		} else if (ret == 0) {
			fprintf(stderr, "TCP-INPUT-MODULE: connection closed\n");
			break;
		}
		//fprintf(stderr, "TCP-INPUT-MODULE: received %d bytes. Fragment size: %u\n", ret, fragment_size);

		if (fragment_size > TCP_BUF_SIZE) {
			fprintf(stderr, "TCP-INPUT-MODULE: buffer too small or some corruption, closing connection ... "); //TODO, handle this better
			break;
		} else if (fragment_size == 0) {	//strange, but valid
			continue;
		}

		b = 0;
		while(b < fragment_size) {
			ret = recv(socket_fd, buffer + b, fragment_size - b, 0);
			if (ret <= 0) {
				break;
			}
			b += ret;
		}
		if (ret <= 0) {
			fprintf(stderr, "TCP-INPUT-MODULE: error or close during chunk receive, closing connection ...");
		}
		//fprintf(stderr, "TCP-INPUT-MODULE: received %d bytes.\n", ret);
		
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
	SDL_KillThread(AcceptThread);
	
	if(socket_fd > 0)
		close(socket_fd);

	close(accept_fd);
}
