#ifndef HTTP_H
#define HTTP_H

/*
 * simple blocking HTTP get call
 *
 * returns: HTTP response content, dynamically allocated (should be deallocated by caller); NULL on error
 */
char *http_get(char *uri);

/*
 * simple blocking HTTP get call, saves to file
 *
 * returns: 0 on success; -1 on error
 */
int http_get2file(char *uri, char *fname);

#endif