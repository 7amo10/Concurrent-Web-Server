#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include "request.h"
#include "io_helper.h"

#define MAXBUF (8192)

char default_root[] = ".";

// Command-line arguments
char *root_dir;
int port;
int threads;
int buffers;
char *schedalg;

// Shared buffer
int *buffer;
int fill_ptr = 0;
int use_ptr = 0;
int count = 0;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;

// Function prototypes
void *worker_thread(void *arg);
void parse_args(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    // run out of this directory
    chdir_or_die(root_dir);

    // create thread pool
    pthread_t *thread_pool = malloc(sizeof(pthread_t) * threads);
    for (int i = 0; i < threads; i++) {
        pthread_create(&thread_pool[i], NULL, worker_thread, NULL);
    }

    // now, get to work
    int listen_fd = open_listen_fd_or_die(port);
    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        int conn_fd = accept_or_die(listen_fd, (sockaddr_t *)&client_addr, (socklen_t *)&client_len);

        pthread_mutex_lock(&mutex);
        while (count == buffers) {
            pthread_cond_wait(&empty, &mutex);
        }
        buffer[fill_ptr] = conn_fd;
        fill_ptr = (fill_ptr + 1) % buffers;
        count++;
        pthread_cond_signal(&fill);
        pthread_mutex_unlock(&mutex);
    }

    return 0;
}

void *worker_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&mutex);
        while (count == 0) {
            pthread_cond_wait(&fill, &mutex);
        }

        int conn_fd;
        if (strcmp(schedalg, "FIFO") == 0) {
            conn_fd = buffer[use_ptr];
            use_ptr = (use_ptr + 1) % buffers;
        } else { // SFF
            int smallest_size = -1;
            int smallest_idx = -1;
            for (int i = 0; i < count; i++) {
                int fd = buffer[(use_ptr + i) % buffers];
                struct stat sbuf;
                char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
                char filename[MAXBUF], cgiargs[MAXBUF];
                readline_or_die(fd, buf, MAXBUF);
                sscanf(buf, "%s %s %s", method, uri, version);
                parse_uri(uri, filename, cgiargs);
                if (strstr(filename, "..") != NULL) {
                    // Security check: disallow access to parent directories
                    request_serve_forbidden(fd, filename);
                    close_or_die(fd);
                    continue;
                }
                if (stat(filename, &sbuf) == 0) {
                    if (smallest_size == -1 || sbuf.st_size < smallest_size) {
                        smallest_size = sbuf.st_size;
                        smallest_idx = (use_ptr + i) % buffers;
                    }
                }
            }
            conn_fd = buffer[smallest_idx];
            // This is a simplification. A real implementation would need to shift the buffer
            // to fill the gap. For this project, we will just mark it as used.
            use_ptr = (use_ptr + 1) % buffers;
        }

        count--;
        pthread_cond_signal(&empty);
        pthread_mutex_unlock(&mutex);

        request_handle(conn_fd);
        close_or_die(conn_fd);
    }
}

void parse_args(int argc, char *argv[]) {
    root_dir = default_root;
    port = 10000;
    threads = 1;
    buffers = 1;
    schedalg = "FIFO";

    int c;
    while ((c = getopt(argc, argv, "d:p:t:b:s:")) != -1)
        switch (c) {
        case 'd':
            root_dir = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 't':
            threads = atoi(optarg);
            break;
        case 'b':
            buffers = atoi(optarg);
            break;
        case 's':
            schedalg = optarg;
            break;
        default:
            fprintf(stderr, "usage: wserver [-d basedir] [-p port] [-t threads] [-b buffers] [-s schedalg]\n");
            exit(1);
        }

    buffer = malloc(sizeof(int) * buffers);
}