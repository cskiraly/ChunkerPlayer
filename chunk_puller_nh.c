#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <net_helper.h>
#include <msg_types.h>

#define MY_IP "127.0.0.1"
#define MY_PORT 8888

#define BUFFSIZE 1024000

static struct nodeID *myID;

int init_net_helper() {
  myID = net_helper_init(MY_IP,MY_PORT);
  if(! myID) {
    fprintf(stderr,"Error initializing net_helper: port %d used by something else?", MY_PORT);
    return -1;
  }
  bind_msg_type(MSG_TYPE_CHUNK);
  return 1;
}

static void *receive(void *dummy)
{
fprintf(stderr,"receive called\n");
  while (1) {
    int len;
    struct nodeID *remote;
    static uint8_t buff[BUFFSIZE];

//fprintf(stderr,"calling recv_from_peer\n");
    len = recv_from_peer(myID, &remote, buff, BUFFSIZE);
//fprintf(stderr,"recv_from_peer returned\n");
    if (len < 0) {
      fprintf(stderr,"Error receiving message. Maybe larger than %d bytes\n", BUFFSIZE);
      nodeid_free(remote);
      continue;
    }
    //dprintf("Received message (%c) from %s\n", buff[0], node_addr(remote));
//fprintf(stderr,"msglen: %d\n", len);
//fprintf(stderr,"msgtype: %x\n", buff[0]);
    switch (buff[0] /* Message Type */) {
      case MSG_TYPE_CHUNK:
if(len>510000) {
fprintf(stderr,"warning: big message %d\n", len);
}
        enqueueBlock(buff+1, len-1);
        break;
      default:
        fprintf(stderr, "Unknown Message Type %x\n", buff[0]);
    }
    nodeid_free(remote);
  }

  return NULL;
}

void *initChunkPuller() {
  pthread_t stdin_thread;
  if (! init_net_helper()) {
    return;
  }
  pthread_create(&stdin_thread, NULL, receive, NULL); 
//  pthread_join(stdin_thread, NULL);

}

void finalizeChunkPuller(void *d) {
}

