#include "api.h"
#include "../common/constants.h"
#include "../common/io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int req_pipe_fd;
int resp_pipe_fd;
char req_pipe_path_[PIPE_PATH_MAX];
char resp_pipe_path_[PIPE_PATH_MAX];
unsigned int session_id;

int ems_setup(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path) {

  char request_buffer[81];

  char buffer_req_path[PIPE_PATH_MAX];
  char buffer_resp_path[PIPE_PATH_MAX];

  memset(buffer_req_path, '\0', sizeof(buffer_req_path));
  memset(buffer_resp_path, '\0', sizeof(buffer_resp_path));

  snprintf(req_pipe_path_, sizeof(req_pipe_path_), "%s", req_pipe_path);
  snprintf(resp_pipe_path_, sizeof(resp_pipe_path_), "%s", resp_pipe_path);

  snprintf(buffer_req_path, sizeof(buffer_req_path), "../client/%s", req_pipe_path);
  snprintf(buffer_resp_path, sizeof(buffer_resp_path), "../client/%s", resp_pipe_path);

  memcpy(request_buffer, "1", 1);
  memcpy(request_buffer + 1, buffer_req_path, sizeof(buffer_req_path));
  memcpy(request_buffer + 1 + sizeof(buffer_req_path), buffer_resp_path, sizeof(buffer_resp_path));

  char full_server_pipe_path[PIPE_PATH_MAX];
  snprintf(full_server_pipe_path, sizeof(full_server_pipe_path), "../server/%s", server_pipe_path);

  // Open server pipe for read and writing
  int server_pipe_fd = open(full_server_pipe_path, O_RDWR);

  if (server_pipe_fd == -1) {
      perror("Error opening server pipe for writing");
      return 1;
  }

  // Open req and resp pipes
  if (mkfifo(req_pipe_path, 0666) == -1 || mkfifo(resp_pipe_path, 0666) == -1) {
      perror("Error creating pipes");
      return 1;
  }

  // Write the concatenated string to the server pipe
  if (write(server_pipe_fd, request_buffer, sizeof(request_buffer)) == -1) {
      perror("Error writing paths to server pipe");
      close(server_pipe_fd);
      return 1;
  }

  close(server_pipe_fd);

  req_pipe_fd = open(req_pipe_path, O_WRONLY);
  if (req_pipe_fd == -1) {
      perror("Error opening request pipe for writing");
      return 1;
  }
  resp_pipe_fd = open(resp_pipe_path, O_RDONLY);
  if (resp_pipe_fd == -1) {
      perror("Error opening server pipe for writing");
      return 1;
  }

  //Wait for session id

  ssize_t read_bytes = read(resp_pipe_fd, &session_id, sizeof(session_id));
  if (read_bytes == -1) {
      perror("Error reading session id from response pipe");
      return 1;
  }

  return 0;
}

int ems_quit(void) {
  //TODO: close pipes

  size_t request_size = 1 + sizeof(unsigned int);

  char* request_buffer = malloc(request_size);
  if (request_buffer == NULL) {
      fprintf(stderr, "Error allocating memory for request_buffer\n");
      return 1;
  }

  memcpy(request_buffer, "2", 1);
  memcpy(request_buffer + 1, &session_id, sizeof(unsigned int));

  if (write(req_pipe_fd, request_buffer, request_size) == -1) {
      perror("Error writing quit request to request pipe");
      free(request_buffer);
      return 1;
  }

  // Close request and response pipes
  if (close(req_pipe_fd) == -1) {
      perror("Error closing request pipe");
      free(request_buffer);
      return 1;
  }

  if (close(resp_pipe_fd) == -1) {
      perror("Error closing response pipe");
      free(request_buffer);
      return 1;
  }

  // Unlink (remove) the named pipes
  if (unlink(req_pipe_path_) == -1) {
      perror("Error unlinking request pipe");
      free(request_buffer);
      return 1;
  }

  if (unlink(resp_pipe_path_) == -1) {
      perror("Error unlinking response pipe");
      free(request_buffer);
      return 1;
  }

  free(request_buffer);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  size_t request_size = 1 + sizeof(unsigned int) * 2 + sizeof(size_t) * 2;

  char* request_buffer = malloc(request_size);
  if (request_buffer == NULL) {
      fprintf(stderr, "Error allocating memory for request_buffer\n");
      return 1;
  }

  int response;
  size_t offset_c = 0;

  memcpy(request_buffer + offset_c, "3", 1);
  offset_c += 1;

  memcpy(request_buffer + offset_c, &session_id, sizeof(unsigned int));
  offset_c += sizeof(unsigned int);

  memcpy(request_buffer + offset_c, &event_id, sizeof(unsigned int));
  offset_c += sizeof(unsigned int);

  memcpy(request_buffer + offset_c, &num_rows, sizeof(size_t));
  offset_c += sizeof(size_t);

  memcpy(request_buffer + offset_c, &num_cols, sizeof(size_t));

  //Writing the request through the request pipe to the server
  if (write(req_pipe_fd, request_buffer, request_size) == -1) {
      free(request_buffer);
      perror("Error writing to request pipe");
      return 1;
  }

  //Read the response from the server through the response pipe
  ssize_t read_bytes = read(resp_pipe_fd, &response, sizeof(int));
  if (read_bytes == -1) {
      free(request_buffer);
      perror("Error reading from response pipe");
      return 1;
  }

  if (response == 1) {
      free(request_buffer);
      return 1;

  }

  free(request_buffer);
  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
    size_t request_size = 1 + sizeof(unsigned int) * 2 + sizeof(size_t) + num_seats * sizeof(size_t) * 2;

    // Allocate a buffer to store the data
    char* request_buffer = malloc(request_size);
    if (request_buffer == NULL) {
        fprintf(stderr, "Error allocating memory for request_buffer\n");
        return 1;
    }

    // Construct the request_buffer
    size_t offset = 0;
    memcpy(request_buffer + offset, "4", 1);
    offset += 1;

    memcpy(request_buffer + offset, &session_id, sizeof(unsigned int));
    offset += sizeof(unsigned int);

    memcpy(request_buffer + offset, &event_id, sizeof(unsigned int));
    offset += sizeof(unsigned int);

    memcpy(request_buffer + offset, &num_seats, sizeof(size_t));
    offset += sizeof(size_t);

    memcpy(request_buffer + offset, xs, num_seats * sizeof(size_t));
    offset += num_seats * sizeof(size_t);
    
    memcpy(request_buffer + offset, ys, num_seats * sizeof(size_t));

    // Write to the request pipe
    if (write(req_pipe_fd, request_buffer, request_size) == -1) {
        perror("Error writing to request pipe");
        free(request_buffer);  // Free the allocated memory
        return 1;
    }

    // Read the response from the response pipe
    int response;
    ssize_t read_bytes = read(resp_pipe_fd, &response, sizeof(int));
    if (read_bytes == -1) {
        perror("Error reading from response pipe");
        free(request_buffer);  // Free the allocated memory
        return 1;
    }

    // Process the response
    if (response == 1) {
        perror("Event couldn't be reserved");
    }

    // Free the allocated memory
    free(request_buffer);

    return 0;
}

int ems_show(int out_fd, unsigned int event_id) {
  //TODO: send show request to the server (through the request pipe) and wait for the response (through the response pipe)
  size_t request_size = 1 + sizeof(unsigned int) * 2;

  char* request_buffer = malloc(request_size);
  if (request_buffer == NULL) {
      fprintf(stderr, "Error allocating memory for request_buffer\n");
      return 1;
  }

  size_t offset = 0;

  memcpy(request_buffer + offset, "5", 1);
  offset += 1;

  memcpy(request_buffer + offset, &session_id, sizeof(unsigned int));
  offset += sizeof(unsigned int);

  memcpy(request_buffer + offset, &event_id, sizeof(unsigned int));

  if (write(req_pipe_fd, request_buffer, request_size) == -1) {
      perror("Error writing to request pipe");
      free(request_buffer);
      return 1;
  }

  int ret_show;
  ssize_t read_bytes = read(resp_pipe_fd, &ret_show, sizeof(int));
  if (read_bytes == -1) {
      perror("Error reading from response pipe");
      free(request_buffer);
      return 1;
  }

  if (ret_show == 1) {
      free(request_buffer);
      return 1;
  }

  size_t num_rows_show, num_cols_show;
  ssize_t rows_bytes = read(resp_pipe_fd, &num_rows_show, sizeof(size_t));
  if (rows_bytes == -1) {
      perror("Error reading from response pipe");
      free(request_buffer);
      return 1;
  }

  ssize_t cols_bytes = read(resp_pipe_fd, &num_cols_show, sizeof(size_t));
  if (cols_bytes == -1) {
      perror("Error reading from response pipe");
      free(request_buffer);
      return 1;
  }


  unsigned int data_show[num_cols_show * num_rows_show -1];

  ssize_t data_bytes = read(resp_pipe_fd, &data_show, sizeof(unsigned int)* num_cols_show * num_rows_show);
  if (data_bytes == -1) {
      perror("Error reading from response pipe");
      free(request_buffer);
      return 1;
  }

  for (size_t i = 0; i < num_rows_show; i++) {
    for (size_t j = 0; j < num_cols_show; j++) {
      print_uint(out_fd, data_show[(i * num_rows_show) + j]);

      if (j < num_cols_show -1) {
        if (print_str(out_fd, " ")) {
          perror("Error writing to file descriptor");
          return 1;
        }
      }
    }

    if (print_str(out_fd, "\n")) {
      perror("Error writing to file descriptor");
      return 1;
    }
  }

  free(request_buffer);
  return 0;
}

int ems_list_events(int out_fd) {
  //TODO: send list request to the server (through the request pipe) and wait for the response (through the response pipe)
  size_t request_size = 1 + sizeof(unsigned int);

  char* request_buffer = malloc(request_size);
  if (request_buffer == NULL) {
      fprintf(stderr, "Error allocating memory for request_buffer\n");
      return 1;
  }

  size_t offset = 0;

  memcpy(request_buffer + offset, "6", 1);
  offset += 1;

  memcpy(request_buffer + offset, &session_id, sizeof(unsigned int));
  offset += sizeof(unsigned int);

  if (write(req_pipe_fd, request_buffer, request_size) == -1) {
      perror("Error writing to request pipe");
      free(request_buffer);
      return 1;
  }

  int ret_list;
  ssize_t read_bytes = read(resp_pipe_fd, &ret_list, sizeof(int));
  if (read_bytes == -1) {
      perror("Error reading from response pipe");
      free(request_buffer);
      return 1;
  }

  if(ret_list == 1){
    free(request_buffer);
    return 1;
  }

  size_t num_events;
  ssize_t num_events_bytes = read(resp_pipe_fd, &num_events, sizeof(size_t));
  if (num_events_bytes == -1) {
      perror("Error reading from response pipe");
      free(request_buffer);
      return 1;
  }

  if(num_events == 0){

    const char* no_events_msg = "No events\n";
    if (write(out_fd, no_events_msg, strlen(no_events_msg)) == -1) {
      perror("Error writing 'No events' message to output");
      free(request_buffer);
      return 1;
    }
  }

  unsigned int ids[num_events - 1];
  ssize_t ids_bytes = read(resp_pipe_fd, &ids, sizeof(unsigned int)* num_events);
  if (ids_bytes == -1) {
      perror("Error reading from response pipe");
      free(request_buffer);
      return 1;
  }

  for (size_t i = 0; i < num_events; i++) {
    char event_msg[50];  // Adjust the size as needed
    snprintf(event_msg, sizeof(event_msg), "Event: %u\n", ids[i]);
    if (write(out_fd, event_msg, strlen(event_msg)) == -1) {
      perror("Error writing event message to output");
      free(request_buffer);
      return 1;
    }
  }

  free(request_buffer);
  return 0;
}