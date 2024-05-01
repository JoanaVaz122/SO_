#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>

#include "../common/constants.h"
#include "../common/io.h"
#include "operations.h"

//Mutex and condition variables
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_full = PTHREAD_COND_INITIALIZER;

//Clients struct
struct ClientData {
  char req_pipe_path[PIPE_PATH_MAX];
  char resp_pipe_path[PIPE_PATH_MAX];
};

//Client Node
struct ClientNode{
  struct ClientData client;
  struct ClientNode* next;
};

//Buffer struct, implemented as a linked list
struct Buffer{
  struct ClientNode* head;
  struct ClientNode* tail;
  size_t size;
};

struct Buffer buffer = {NULL, NULL, 0};

int sig;

void *worker_thread(void *arg) {

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);

  unsigned int session_id = *(unsigned int *)arg;
  free(arg);

  while (1) {

    pthread_mutex_lock(&buffer_mutex);

    //Wait for queue to be not empty
    while(buffer.head == NULL){
      pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
    }

    struct ClientNode* current_client = buffer.head;
    buffer.head = current_client->next;
    if (buffer.head == NULL) {
        buffer.tail = NULL;
    }

    pthread_cond_signal(&buffer_not_full); //signal that the buffer is not full
    buffer.size--;

    pthread_mutex_unlock(&buffer_mutex);

    //Open client pipes

    int req_pipe_fd = open(current_client->client.req_pipe_path, O_RDONLY);
    if (req_pipe_fd == -1){

      perror("erro ao abrir o pipe de requests");
      return NULL;
    }
    int resp_pipe_fd = open(current_client->client.resp_pipe_path, O_WRONLY);
    if (resp_pipe_fd == -1){

      perror("erro ao abrir o pipe de requests");
      return NULL;
    }

    if (write(resp_pipe_fd, &session_id, sizeof(unsigned int)) == -1) {
        perror("Error writing session_id to response pipe");
    }

    int flag = 0;

    //Handle client session

    while(flag == 0){

        char op_code;
        unsigned int id_dump;
        ssize_t bytes_read = read(req_pipe_fd, &op_code, sizeof(char));

        if (bytes_read == -1){
          perror("erros ao ler do pipe da solicitacao");
          free(current_client);
          return NULL;
        }
        ssize_t id_read = read(req_pipe_fd, &id_dump, sizeof(unsigned int));

        if (id_read == -1){
          perror("erros ao ler do pipe da solicitacao");
          free(current_client);
          return NULL;
        }

        switch(op_code){

          case '2':
            close(req_pipe_fd);
            close(resp_pipe_fd);
            free(current_client);
            flag = 1;
            
            break;

          case '3':
            unsigned int event_id_create;
            size_t num_rows_, num_cols_;
            
            ssize_t read_bytes1 = read(req_pipe_fd, &event_id_create, sizeof(unsigned int));
            if(read_bytes1 == -1){
              perror("error reading event");
              break;
            }

            ssize_t read_bytes2 = read(req_pipe_fd, &num_rows_, sizeof(size_t));
            if(read_bytes2 == -1){
              perror("error reading num_rows");
              break;
            }

            ssize_t read_bytes3 = read(req_pipe_fd, &num_cols_, sizeof(size_t));
            if(read_bytes3 == -1){
              perror("error reading num_columns");
              break;
            }

            int result = ems_create(event_id_create, num_rows_, num_cols_);

            if (write(resp_pipe_fd, &result, sizeof(int)) == -1) {
              perror("Error writing to response pipe");
              break;
            }
            break;

          case '4':
            unsigned int event_id_reserve;
            size_t num_seats_;

            ssize_t read_bytes_id = read(req_pipe_fd, &event_id_reserve, sizeof(unsigned int));
            if(read_bytes_id == -1){
              perror("error reading id");
              break;
            }
            ssize_t read_bytes_seats = read(req_pipe_fd, &num_seats_, sizeof(size_t));
            if(read_bytes_seats == -1){
              perror("error reading seats");
              break;
            }

            if (read_bytes_id ==-1 || read_bytes_seats == -1){
              perror("error reading event or num_Seats");
              break;
            }

            size_t xs[MAX_RESERVATION_SIZE];
            size_t ys[MAX_RESERVATION_SIZE];
            
            for (size_t i = 0; i < num_seats_; i++) {
              ssize_t read_bytes_x = read(req_pipe_fd, &xs[i], sizeof(size_t));
              if (read_bytes_x == -1){
                perror("error reading x");
                break;
              }
            }

            for (size_t i = 0; i < num_seats_; i++) {
              ssize_t read_bytes_y = read(req_pipe_fd, &ys[i], sizeof(size_t));
              if (read_bytes_y == -1){
                perror("error reading ");
                break;
              }
            }

            int reserve_result = ems_reserve(event_id_reserve, num_seats_, xs, ys);

            if (write(resp_pipe_fd, &reserve_result, sizeof(int)) == -1) {
              perror("Error writing to response pipe");
              break;
            }

            break;

          case '5':

            unsigned int event_id_show;
            read(req_pipe_fd, &event_id_show, sizeof(unsigned int));

            if (ems_show(resp_pipe_fd, event_id_show) == 1) {
              if (write(resp_pipe_fd, &(int){1}, sizeof(int)) == -1) {
                perror("Error writing to response pipe");
                break;
              }
              
            }
            
            break;
          
          case '6':

            if (ems_list_events(resp_pipe_fd) == 1) {
              if (write(resp_pipe_fd, &(int){1}, sizeof(int)) == -1) {
                perror("Error writing to response pipe");
                break;
              }
            }

            break;
        }
      }
    }
}

void sig_handler(int sign){
  if(sign == SIGUSR1){
    if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
        exit(EXIT_FAILURE);
    }
  }
  sig = 1;
}

int main(int argc, char* argv[]) {

  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s\n <pipe_path> [delay]\n", argv[0]);
    return 1;
  }

  char* endptr;
  unsigned int state_access_delay_us = STATE_ACCESS_DELAY_US;
  if (argc == 3) {
    unsigned long int delay = strtoul(argv[2], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_us = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_us)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }
  
  if (mkfifo(argv[1], 0666) == -1){
    if (errno != EEXIST){
      perror("erro ao criar um server path");
      ems_terminate();
      return 1;
    }
  }

  //Open server
  int server_pipe_fd = open(argv[1], O_RDWR);
  
  if (server_pipe_fd == -1){

    perror("erro ao abrir o servidor");
    ems_terminate();
    return 1;
  }

  //Worker threads array
  pthread_t workers[MAX_SESSION_COUNT - 1];

  buffer.head = buffer.tail = NULL;
  buffer.size = 0;

  for (unsigned int i = 0; i < MAX_SESSION_COUNT; i++) {
    unsigned int *session_id = malloc(sizeof(int));
    *session_id = i;
    if(pthread_create(&workers[i], NULL, worker_thread, session_id) < 0){
      perror("error creating thread");
      return 1;
    };
  }

  if (signal(SIGUSR1, sig_handler) == SIG_ERR) {
    exit(EXIT_FAILURE);
  }

  while (1) {

    // Wait for client to request a session

    if(sig == 1){
      ems_handle_sigusr1();
    }

    char op_code_dump;
    char req_pipe_path[PIPE_PATH_MAX];
    char resp_pipe_path[PIPE_PATH_MAX];

    pthread_mutex_lock(&buffer_mutex);

    while (buffer.size == MAX_BUFFER_SIZE) {
      pthread_cond_wait(&buffer_not_full, &buffer_mutex);
    }

    pthread_mutex_unlock(&buffer_mutex);

    ssize_t bytes_read_op = read(server_pipe_fd, &op_code_dump, sizeof(op_code_dump));
    if (bytes_read_op == -1) {
      if(errno == EINTR){
        continue;
      }
        perror("Error reading op code from server pipe");
        return 1;
    }

    ssize_t bytes_read_req = read(server_pipe_fd, req_pipe_path, sizeof(req_pipe_path));
    if (bytes_read_req == -1) {
        perror("Error reading request pipe path from server pipe");
        return 1;
    }

    ssize_t bytes_read_resp = read(server_pipe_fd, resp_pipe_path, sizeof(resp_pipe_path));
    if (bytes_read_resp == -1) {
        perror("Error reading response pipe path from server pipe");
        return 1;
    }

    struct ClientNode* new_client = (struct ClientNode*)malloc(sizeof(struct ClientNode));
    strncpy(new_client->client.req_pipe_path, req_pipe_path, PIPE_PATH_MAX);
    strncpy(new_client->client.resp_pipe_path, resp_pipe_path, PIPE_PATH_MAX);
    new_client->next = NULL;

    pthread_mutex_lock(&buffer_mutex);

    if (buffer.tail == NULL) {
        buffer.head = new_client;
        buffer.tail = new_client;
    } else {
        buffer.tail->next = new_client;
        buffer.tail = new_client;
    }
    buffer.size++;

    pthread_cond_signal(&buffer_not_empty); //signal that the buffer is not empty

    pthread_mutex_unlock(&buffer_mutex);
  }

  for (int i = 0; i < MAX_SESSION_COUNT; i++) {
    if(pthread_join(workers[i], NULL)<0){
      perror("error joining thread");
      return 1;
    };
  }

  //TODO: Close Server

  close(server_pipe_fd);
  unlink(argv[1]);

  ems_terminate();

  return 0;

}