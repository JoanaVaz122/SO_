#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../common/io.h"
#include "eventlist.h"

static struct EventList* event_list = NULL;
static unsigned int state_access_delay_us = 0;

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @param from First node to be searched.
/// @param to Last node to be searched.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id, struct ListNode* from, struct ListNode* to) {
  struct timespec delay = {0, state_access_delay_us * 1000};
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id, from, to);
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }

int ems_handle_sigusr1(){

  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct ListNode* current = event_list->head;

  if (current == NULL) {
    fprintf(stderr, "No events\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  pthread_rwlock_unlock(&event_list->rwl);

  while(current != NULL){

    if (pthread_mutex_lock(&current->event->mutex) != 0) {
      fprintf(stderr, "Error locking mutex\n");
      return 1;
    }

    struct Event* event = current->event;
    size_t rows = event->rows;
    size_t cols = event->cols;

    printf("Event: %d\n", event->id);

    for (size_t i = 1; i <= rows; i++) {
      for (size_t j = 1; j <= cols; j++) {

        char buffer[16];
        sprintf(buffer, "%u", event->data[seat_index(event, i, j)]);

        printf("%s", buffer);

        if (j < cols) {
          printf(" ");
        }

      }
      printf("\n");
    }
    pthread_mutex_unlock(&current->event->mutex);
    current = current->next;
  }

  return 0;
}

int ems_init(unsigned int delay_us) {
  if (event_list != NULL) {
    fprintf(stderr, "EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  state_access_delay_us = delay_us;

  return event_list == NULL;
}

int ems_terminate() {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  free_list(event_list);
  pthread_rwlock_unlock(&event_list->rwl);
  return 0;
}

int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_wrlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  if (get_event_with_delay(event_id, event_list->head, event_list->tail) != NULL) {
    fprintf(stderr, "Event already exists\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  struct Event* event = malloc(sizeof(struct Event));

  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;
  if (pthread_mutex_init(&event->mutex, NULL) != 0) {
    pthread_rwlock_unlock(&event_list->rwl);
    free(event);
    return 1;
  }
  event->data = calloc(num_rows * num_cols, sizeof(unsigned int));

  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    pthread_rwlock_unlock(&event_list->rwl);
    free(event);
    return 1;
  }

  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    pthread_rwlock_unlock(&event_list->rwl);
    free(event->data);
    free(event);
    return 1;
  }

  pthread_rwlock_unlock(&event_list->rwl);
  return 0;
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  }

  for (size_t i = 0; i < num_seats; i++) {
    if (xs[i] <= 0 || xs[i] > event->rows || ys[i] <= 0 || ys[i] > event->cols) {
      fprintf(stderr, "Seat out of bounds\n");
      pthread_mutex_unlock(&event->mutex);
      return 1;
    }
  }

  for (size_t i = 0; i < event->rows * event->cols; i++) {
    for (size_t j = 0; j < num_seats; j++) {
      if (seat_index(event, xs[j], ys[j]) != i) {
        continue;
      }

      if (event->data[i] != 0) {
        fprintf(stderr, "Seat already reserved\n");
        pthread_mutex_unlock(&event->mutex);
        return 1;
      }

      break;
    }
  }

  unsigned int reservation_id = ++event->reservations;

  for (size_t i = 0; i < num_seats; i++) {
    event->data[seat_index(event, xs[i], ys[i])] = reservation_id;
  }

  pthread_mutex_unlock(&event->mutex);
  return 0;
}

int ems_show(int out_fd, unsigned int event_id) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct Event* event = get_event_with_delay(event_id, event_list->head, event_list->tail);

  pthread_rwlock_unlock(&event_list->rwl);

  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  if (pthread_mutex_lock(&event->mutex) != 0) {
    fprintf(stderr, "Error locking mutex\n");
    return 1;
  }

  size_t response_size = sizeof(int) + 2 * sizeof(size_t) + event->rows * event->cols * sizeof(unsigned int);

  // Allocate dynamic memory for the response buffer
  char* resp_buffer = malloc(response_size);
  if (resp_buffer == NULL) {
      fprintf(stderr, "Error allocating memory for response buffer\n");
      pthread_mutex_unlock(&event->mutex);
      return 1;
  }

  // Construct the response buffer
  int success_status = 0;
  memcpy(resp_buffer, &success_status, sizeof(int));
  memcpy(resp_buffer + sizeof(int), &event->rows, sizeof(size_t));
  memcpy(resp_buffer + sizeof(int) + sizeof(size_t), &event->cols, sizeof(size_t));
  memcpy(resp_buffer + sizeof(int) + 2 * sizeof(size_t), event->data, event->rows * event->cols * sizeof(unsigned int));

  // Write the response buffer to the specified pipe
  if (write(out_fd, resp_buffer, response_size) == -1) {
      perror("Error writing to response pipe");
      free(resp_buffer);  // Free the allocated memory
      pthread_mutex_unlock(&event->mutex);
      return 1;
  }

  // Free the allocated memory
  free(resp_buffer);

  pthread_mutex_unlock(&event->mutex);
  return 0;
}

int ems_list_events(int out_fd) {
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  if (pthread_rwlock_rdlock(&event_list->rwl) != 0) {
    fprintf(stderr, "Error locking list rwl\n");
    return 1;
  }

  struct ListNode* current = event_list->head;
  size_t num_events = 0;

  while (current != NULL) {
    num_events++;
    current = current->next;
  }

  size_t response_size = sizeof(int) + sizeof(size_t) + num_events * sizeof(unsigned int);
  char resp_buffer[response_size];

  // Construct the response buffer
  int success_status = 0;
  memcpy(resp_buffer, &success_status, sizeof(int));
  memcpy(resp_buffer + sizeof(int), &num_events, sizeof(size_t));

  if (num_events > 0) {
    // Copy event IDs into the response buffer
    struct ListNode* current_ = event_list->head;
    unsigned int event_ids[num_events];
    current_ = event_list->head;

    for (size_t i = 0; i < num_events; i++) {
      event_ids[i] = current_->event->id;
      current_ = current_->next;
    }

    memcpy(resp_buffer + sizeof(int) + sizeof(size_t), event_ids, num_events * sizeof(unsigned int));
  }

  // Write the response buffer to the specified pipe
  if (write(out_fd, resp_buffer, response_size) == -1) {
    perror("Error writing to response pipe");
    pthread_rwlock_unlock(&event_list->rwl);
    return 1;
  }

  pthread_rwlock_unlock(&event_list->rwl);
  return 0;
}
