#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <sys/wait.h>
#include <pthread.h> 

#include "constants.h"
#include "operations.h"
#include "parser.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Structure to pass arguments to the thread function
struct ThreadArgs {
    int in_fd;
    int out_fd;
    int thread_id;
};

int barrierFlag = 0;

// Function executed by each thread
void *thread_function(void *args_void_ptr){

    int wait_id = 0;
    int wait_delay = 0;
    int flag = 0;
    struct ThreadArgs *args = (struct ThreadArgs *)args_void_ptr;
    int in_fd = args->in_fd;
    int out_fd = args->out_fd;
    int counter = 0;

    while (flag == 0) {
        unsigned int event_id, delay, thread_id;
        size_t num_rows, num_columns, num_coords;
        size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

        if(wait_id == args->thread_id){

          fprintf(stderr, "Waiting...\n");
          ems_wait((unsigned int)wait_delay);
          wait_id = 0;
          wait_delay =0;
        }

        pthread_mutex_lock(&mutex);

        if(barrierFlag == 1){
          pthread_mutex_unlock(&mutex);
          free(args);
          return NULL;
        }

        switch (get_next(in_fd)) {
          case CMD_CREATE:
            counter++;
            if (parse_create(in_fd, &event_id, &num_rows, &num_columns) != 0) {

              pthread_mutex_unlock(&mutex);
              fprintf(stderr, "Invalid command. See HELP for usage\n");
              continue;
            }

            pthread_mutex_unlock(&mutex);
            if (ems_create(event_id, num_rows, num_columns)) {

              fprintf(stderr, "Failed to create event\n");
            }
            break;

          case CMD_RESERVE:
          counter++;
            num_coords = parse_reserve(in_fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);
            pthread_mutex_unlock(&mutex);

            if (num_coords == 0) {

              fprintf(stderr, "Invalid command. See HELP for usage\n");
              continue;
            }
            if (ems_reserve(event_id, num_coords, xs, ys)) {

              fprintf(stderr, "Failed to reserve seats\n");
            }

            break;

          case CMD_SHOW:
          counter++;
            if (parse_show(in_fd, &event_id) != 0) {

              pthread_mutex_unlock(&mutex);
              fprintf(stderr, "Invalid command. See HELP for usage\n");
              continue;
            }

            pthread_mutex_unlock(&mutex);
            if (ems_show(out_fd, event_id)) {

              fprintf(stderr, "Failed to show event\n");
            }
            break;

          case CMD_LIST_EVENTS:
          counter++;
            pthread_mutex_unlock(&mutex);
            if (ems_list_events(out_fd)) {

              fprintf(stderr, "Failed to list events\n");
            }
            break;

          case CMD_WAIT:
          counter++;
            if (parse_wait(in_fd, &delay, &thread_id) == -1) {  

              pthread_mutex_unlock(&mutex);
              fprintf(stderr, "Invalid command. See HELP for usage\n");
              continue;
            }
            if (thread_id != 0){
              if(args->thread_id == (int)thread_id){

                fprintf(stderr, "Waiting...\n");
                ems_wait(delay);
                delay = 0;
                thread_id = 0;
              } else{

                wait_id = (int)thread_id;
                wait_delay = (int)delay;
                delay = 0;
                thread_id = 0;
              }

            }

            pthread_mutex_unlock(&mutex);

            if (delay > 0) {

              pthread_mutex_lock(&mutex);
              fprintf(stderr, "Waiting...\n");
              ems_wait(delay);
              pthread_mutex_unlock(&mutex);
            }

            break;

          case CMD_INVALID:
            pthread_mutex_unlock(&mutex);
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            break;

          case CMD_HELP:
            pthread_mutex_unlock(&mutex);
            fprintf(stderr,
                "Available commands:\n"
                "  CREATE <event_id> <num_rows> <num_columns>\n"
                "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
                "  SHOW <event_id>\n"
                "  LIST\n"
                "  WAIT <delay_ms> [thread_id]\n"  
                "  BARRIER\n"
                "  HELP\n");

            break;

          case CMD_BARRIER: 
          
            barrierFlag = 1;
            pthread_mutex_unlock(&mutex);
            free(args);
            pthread_exit((void *)1); // Signal that BARRIER command is encountered  //add
            flag = 1;
            printf("%s",counter);
            
            break;
          case CMD_EMPTY:
            pthread_mutex_unlock(&mutex);
            break;

          case EOC:
            pthread_mutex_unlock(&mutex);
            free(args);
            pthread_exit((void *)EOF);
            flag = 1;
            break;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {

    int pid = 1;
    int flag_barrier = 0;
    int *join_ret;
    unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

    if (argc > 4) {

      char *endptr;
      unsigned long int delay = strtoul(argv[4], &endptr, 10);

      if (*endptr != '\0' || delay > UINT_MAX) {

        fprintf(stderr, "Invalid delay value or value too large\n");
        return 1;
      }

      state_access_delay_ms = (unsigned int)delay;
    }

    if (ems_init(state_access_delay_ms)) {

        fprintf(stderr, "Failed to initialize EMS\n");
        return 1;
    }

    char *endptr;
    long int max_proc = strtol(argv[2], &endptr, 10);

    if (*endptr != '\0' || max_proc <= 0) {
        fprintf(stderr, "Invalid MAX_PROC value\n");
        return 1;
    }

    char *endptr_1;
    long int max_threads = strtol(argv[3], &endptr_1, 10);

    pthread_t threads[max_threads];
    //pthread_t threads_ids[max_threads];

    int active_processes = 0; 

    DIR *dir = opendir(argv[1]);
    
    if (dir == NULL) {
        perror("Error opening directory");
        return 0;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {

        // Check for files with ".jobs" extension
        if (strstr(entry->d_name, ".jobs") != NULL) {

            if (active_processes >= max_proc) {
                    int status;
                    waitpid(-1, &status, 0);
                    
                    active_processes--;
            }

            if(pid != 0){
                pid = fork();
                if(pid == -1){
                  fprintf(stderr, "Failed to create child process\n");
                  return 1;
                }
                active_processes++;
            }

            // Construct paths for input and output files
            size_t path_length_out = strlen(argv[1]) + 1 + strlen(entry->d_name) - 4 + strlen("out") + 1;
            size_t path_length_inp = strlen(argv[1]) + 1 + strlen(entry->d_name) +1;
            char output_file_path[path_length_out];
            char input_file_path[path_length_inp];
            strcpy(output_file_path, argv[1]);
            strcpy(input_file_path, argv[1]);
            strcat(input_file_path, "/");
            strcat(output_file_path, "/");
            strcat(input_file_path, entry->d_name);
            strncat(output_file_path, entry->d_name, strlen(entry->d_name) - 4);
            strcat(output_file_path, "out");
            
             // Open input and output files
            int output_fd = open(output_file_path, O_CREAT | O_TRUNC | O_WRONLY , S_IRUSR | S_IWUSR);
            int input_fd = open(input_file_path, O_RDONLY);

            if (pid == 0) {

              while(1){
                  flag_barrier = 0;
              
                  for(int i = 0; i < max_threads; i++){
                    struct ThreadArgs *args = (struct ThreadArgs *)malloc(sizeof(struct ThreadArgs));
                    args->in_fd = input_fd;
                    args->out_fd = output_fd;
                    args->thread_id = i+1;


                    pthread_create(&threads[i], NULL, thread_function, (void *)args);
                    //threads_ids[i] = pthread_self;
                  }
                  
                  for(int i = 0; i < max_threads; i++){
                    pthread_join(threads[i], (void**) &join_ret);
                    if(join_ret == (void *)1){
                      flag_barrier = 1;
                    }
                  }

                  if(flag_barrier == 0){
                    break;
                  }else{
                    barrierFlag = 0;
                  }
              }

              close(input_fd);
              close(output_fd);

                
            }
        }
    }
    // Wait for all child processes to finish
    while (active_processes > 0) {
        int status;
        pid_t terminated_pid = waitpid(-1, &status, 0);
        if(terminated_pid != -1){
          printf("%d ended with status %d\n", terminated_pid, status);
        }
        active_processes--;
    }
    // Cleanup and close resources
    ems_terminate();
    closedir(dir);

    return 0; 
}



