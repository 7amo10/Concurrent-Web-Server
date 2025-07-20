#ifndef __REQUEST_H__

void request_handle(int fd);
int parse_uri(char *uri, char *filename, char *cgiargs);
void request_serve_forbidden(int fd, char *filename);
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

#endif // __REQUEST_H__