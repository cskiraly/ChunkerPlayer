#ifndef CHUNK_PULLER_H
#define CHUNK_PULLER_H

#ifdef HTTPIO
struct MHD_Daemon *initChunkPuller(const char *path, const int port);
void finalizeChunkPuller(struct MHD_Daemon *daemon);
#endif
#ifdef TCPIO
int initChunkPuller(const int port);
void finalizeChunkPuller(void);
#endif

#endif
