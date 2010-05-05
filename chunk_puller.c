#include <platform.h>
#include <microhttpd.h>


static struct connection_info_struct {
  uint8_t *block;
  int block_size;
};
static int listen_port = 0;
static char listen_path[256];

void request_completed(void *cls, struct MHD_Connection *connection,
                       void **con_cls, enum MHD_RequestTerminationCode toe) {
  struct connection_info_struct *con_info = (struct connection_info_struct *)*con_cls;
  if(NULL == con_info)
    return;
  if(con_info->block)
    free (con_info->block);
  free(con_info);
  *con_cls = NULL;
}

int send_response(struct MHD_Connection *connection, unsigned int code) {
  int ret;
  struct MHD_Response *response;

  response = MHD_create_response_from_data(0, NULL, MHD_NO, MHD_NO);
  if(!response)
    return MHD_NO;

  ret = MHD_queue_response(connection, code, response);
  MHD_destroy_response (response);

  return ret;
}

int answer_to_connection(void *cls, struct MHD_Connection *connection,
                         const char *url, const char *method,
                         const char *version, const char *upload_data,
                         size_t *upload_data_size, void **con_cls) {
  struct connection_info_struct *con_info = NULL;
  uint8_t *block = NULL;

  if(*con_cls==NULL) {
    con_info = malloc(sizeof(struct connection_info_struct));
    if(con_info == NULL)
      return MHD_NO;

    con_info->block = NULL;
    con_info->block_size = 0; 
    *con_cls = (void *)con_info;

    return MHD_YES;
  }

  if(0 == strcmp (method, "GET")) {
    return send_response(connection, MHD_HTTP_BAD_REQUEST);
  }

  if(0 == strcmp(method, "POST")) {
    if(0 == strcmp(url, listen_path)) {
      con_info = (struct connection_info_struct *)*con_cls;
      if(*upload_data_size > 0) {
        block = (uint8_t *)malloc(con_info->block_size+*upload_data_size);

        if(!block)
          return MHD_NO;

        memcpy(block, con_info->block, con_info->block_size);
        memcpy(block+con_info->block_size, upload_data, *upload_data_size);
        free(con_info->block); //free the old referenced memory
        con_info->block = block;
        con_info->block_size += *upload_data_size;
        *upload_data_size = 0;
        return MHD_YES;
      }
      else {
				// i do not mind about return value or problems into the enqueueBlock()
        enqueueBlock(con_info->block, con_info->block_size); //this might take some time
        free(con_info->block); //the enqueueBlock makes a copy of block into a chunk->data
        return send_response(connection, MHD_HTTP_OK);
      }
    }
    else
      return send_response(connection, MHD_HTTP_NOT_FOUND);
  }
  return send_response(connection, MHD_HTTP_BAD_REQUEST);
}


struct MHD_Daemon *initChunkPuller(const char *path, const int port) {
  sprintf(listen_path, "%s", path);
  listen_port = port;
printf("starting HTTPD on %s port %d\n", listen_path, listen_port);
  return MHD_start_daemon(MHD_USE_SELECT_INTERNALLY | MHD_USE_DEBUG, listen_port,
                          NULL, NULL,
                          &answer_to_connection, NULL, MHD_OPTION_END);
}

void finalizeChunkPuller(struct MHD_Daemon *daemon) {
  MHD_stop_daemon(daemon);
}

