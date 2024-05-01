#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#include <pthread.h>
// Mutex and read-write locks for thread synchronization
// global arg evento
// global event list
// global output
pthread_mutex_t mutex_event = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t rwlock_event_list = PTHREAD_RWLOCK_INITIALIZER; 
pthread_rwlock_t rwlock_output = PTHREAD_RWLOCK_INITIALIZER;

#include "eventlist.h"

#define BUFFER_SIZE 20

// Global variables for event list and state access delay
static struct EventList* event_list = NULL;
static unsigned int state_access_delay_ms = 0;

// Function to format event information into a string
void format_event_str(char* buffer, unsigned int event_id) {
    snprintf(buffer, BUFFER_SIZE, "Event: %u\n", event_id);
}

// Function to format seat information into a string
void format_seat_str(char* buffer, unsigned int seat) {
    snprintf(buffer, BUFFER_SIZE, "%u", seat);
}



/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event_id The ID of the event to get.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event* get_event_with_delay(unsigned int event_id) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return get_event(event_list, event_id);
}

/// Gets the seat with the given index from the state.
/// @note Will wait to simulate a real system accessing a costly memory resource.
/// @param event Event to get the seat from.
/// @param index Index of the seat to get.
/// @return Pointer to the seat.
static unsigned int* get_seat_with_delay(struct Event* event, size_t index) {
  struct timespec delay = delay_to_timespec(state_access_delay_ms);
  nanosleep(&delay, NULL);  // Should not be removed

  return &event->data[index];
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event* event, size_t row, size_t col) { return (row - 1) * event->cols + col - 1; }


// Initialization function for EMS state
int ems_init(unsigned int delay_ms) {
  if (event_list != NULL) {
    printf("EMS state has already been initialized\n");
    return 1;
  }

  event_list = create_list();
  state_access_delay_ms = delay_ms;

  return event_list == NULL;
}


// Termination function for EMS state
int ems_terminate() {
  if (event_list == NULL) {
    printf("EMS state must be initialized\n");
    return 1;
  }

  free_list(event_list);
  
  return 0;
}


int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {

  // Check if EMS state has been initialized
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }
    
  // Read lock on the event list to check if the event already exists
  pthread_rwlock_rdlock(&rwlock_event_list);
  if (get_event_with_delay(event_id) != NULL) {
    fprintf(stderr, "Event already exists\n");
    pthread_rwlock_unlock(&rwlock_event_list);
    return 1;
  }
  pthread_rwlock_unlock(&rwlock_event_list);

  // Allocate memory for a new event
  struct Event* event = malloc(sizeof(struct Event));


  // Check if memory allocation was successful
  if (event == NULL) {
    fprintf(stderr, "Error allocating memory for event\n");
    return 1;
  }

  // Lock to set event details
  pthread_mutex_lock(&mutex_event);
  event->id = event_id;
  event->rows = num_rows;
  event->cols = num_cols;
  event->reservations = 0;

  // Allocate memory for event data (seats)
  event->data = malloc(num_rows * num_cols * sizeof(unsigned int));
  pthread_mutex_unlock(&mutex_event);

  // Check if memory allocation for event data was successful
  if (event->data == NULL) {
    fprintf(stderr, "Error allocating memory for event data\n");
    free(event);
    return 1;
  }

  // Allocate memory for mutexes to control access to each seat
  event->mutex_seats = malloc(num_rows * num_cols * sizeof(pthread_mutex_t));

  // Initialize mutexes for each seat
  for (size_t i = 0; i < num_rows * num_cols; i++) {
    if (pthread_mutex_init(&event->mutex_seats[i], NULL) != 0) {
      fprintf(stderr, "Error initializing mutex for seat %zu\n", i);
      free(event->mutex_seats);
      free(event->data);
      free(event);
      return 1;
    }
    event->data[i] = 0; // Initialize seat as unreserved
  }
  // Write lock on the event list to append the new event
  pthread_rwlock_wrlock(&rwlock_event_list);
  if (append_to_list(event_list, event) != 0) {
    fprintf(stderr, "Error appending event to list\n");
    free(event->data);
    free(event);
    pthread_rwlock_unlock(&rwlock_event_list);
    return 1;
  }
  pthread_rwlock_unlock(&rwlock_event_list);
  return 0;
}

int compare_coordinates(const void* a, const void* b) {
    // Extract ys values
    size_t y1 = *((const size_t*)a + 1);
    size_t y2 = *((const size_t*)b + 1);

    if (y1 < y2) {
        return -1;
    } else if (y1 > y2) {
        return 1;
    } else {
        // If ys are equal, compare xs values
        size_t x1 = *((const size_t*)a);
        size_t x2 = *((const size_t*)b);

        if (x1 < x2) {
            return -1;
        } else if (x1 > x2) {
            return 1;
        } else {
            return 0;
        }
    }
}

int has_duplicate_coordinates(size_t num_seats, size_t* xs, size_t* ys) {
    for (size_t i = 1; i < num_seats; ++i) {
        // Check for duplicates in xy
        if (xs[i] == xs[i - 1] && ys[i] == ys[i - 1]) {
            return 1;
        }
    }

    return 0; 
}

int ems_reserve(unsigned int event_id, size_t num_seats, size_t* xs, size_t* ys) {

  // Flag to track reservation success
  int flag_reserve = 1;
  int flag_iteration = 0;
 
  // Check if EMS state has been initialized
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  // Read lock on the event list to get the event details
  pthread_rwlock_rdlock(&rwlock_event_list);
  struct Event* event = get_event_with_delay(event_id);
  pthread_rwlock_unlock(&rwlock_event_list);

  // Check if the event exists
  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  // Sort seat coordinates for proper comparison
  qsort(xs, num_seats, sizeof(size_t), compare_coordinates);
  qsort(ys, num_seats, sizeof(size_t), compare_coordinates);


  // Check for duplicate seat coordinates
  if(has_duplicate_coordinates(num_seats, xs, ys) == 1){

    return 0;
  }

  // Iterate through seats to be reserved
  for (size_t i = 0; i < num_seats; i++) {
    size_t row = xs[i];
    size_t col = ys[i];

    // Check if the seat coordinates are valid
    if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
      flag_reserve = 0;
      flag_iteration = (int)i - 1;
      fprintf(stderr, "Invalid seat\n");
      break;
    }

    // Lock the mutex for the seat
    pthread_mutex_lock(&event->mutex_seats[seat_index(event, row, col)]);

    // Check if the seat is already reserved
    if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
      flag_reserve = 0;
      flag_iteration = (int)i;
      fprintf(stderr, "Seat already reserved\n");
      break;
    }
  }

    // If reservation is successful, update the seats
    if(flag_reserve == 1){

        // Lock global event mutex for reservation ID assignment
        pthread_mutex_lock(&mutex_event);
        unsigned int reservation_id = ++event->reservations;
        pthread_mutex_unlock(&mutex_event);

        // Update each seat with the reservation ID
        for (size_t j = 0; j < num_seats; j++) {

            size_t row_ = xs[j];
            size_t col_ = ys[j];
            *get_seat_with_delay(event, seat_index(event, row_, col_)) = reservation_id;
            pthread_mutex_unlock(&event->mutex_seats[seat_index(event, row_, col_)]);
        }

    }else if(flag_reserve == 0){
        // Reservation failed, unlock the seats that were locked
        for (size_t h = 0; (int)h <= flag_iteration; h++) {

            size_t row_ = xs[h];
            size_t col_ = ys[h];
            pthread_mutex_unlock(&event->mutex_seats[seat_index(event, row_, col_)]);
        }
    }

  return 0;
}

int ems_show(int fd, unsigned int event_id) {

// Check if EMS state has been initialized
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
    return 1;
  }

  // Read lock on the event list to get the event details
  pthread_rwlock_rdlock(&rwlock_event_list);
  struct Event* event = get_event_with_delay(event_id);
  pthread_rwlock_unlock(&rwlock_event_list);

  // Check if the event exists
  if (event == NULL) {
    fprintf(stderr, "Event not found\n");
    return 1;
  }

  // Write lock on the output to update the file descriptor
  pthread_rwlock_wrlock(&rwlock_output);

  // Iterate through rows and columns to print seat information
  for (size_t i = 1; i <= event->rows; i++) {
    for (size_t j = 1; j <= event->cols; j++) {
      unsigned int* seat = get_seat_with_delay(event, seat_index(event, i, j));
      char seat_str[BUFFER_SIZE];
      // Lock the mutex for the seat      
      pthread_mutex_lock(&event->mutex_seats[seat_index(event, i, j)]);
     // Format seat information and write to the file descriptor
      format_seat_str(seat_str, *seat);
      write(fd, seat_str, strlen(seat_str));
      // Unlock the mutex for the seat
      pthread_mutex_unlock(&event->mutex_seats[seat_index(event, i, j)]);

      if (j < event->cols) {
        write(fd, " ", 1);
      }
    }

    write(fd,"\n", 1);
  }
  // Unlock the output
  pthread_rwlock_unlock(&rwlock_output);
  return 0;
}

int ems_list_events(int fd){

  // Check if EMS state has been initialized
  if (event_list == NULL) {
    fprintf(stderr, "EMS state must be initialized\n");
  
    return 1;
  }
  if (event_list->head == NULL) {
    write(fd, "No events\n", 10);
  
    return 0;
  }
  // Read lock on the event list to iterate through events
  pthread_rwlock_rdlock(&rwlock_event_list);
  struct ListNode* current = event_list->head;
  pthread_rwlock_unlock(&rwlock_event_list);
  // Iterate through events in the event list
  while (current != NULL) {
    char event_str[BUFFER_SIZE];
    format_event_str(event_str, current->event->id);
    pthread_rwlock_wrlock(&rwlock_output);
    format_event_str(event_str, current->event->id);
    write(fd, event_str, strlen(event_str));
    pthread_rwlock_unlock(&rwlock_output);
     // Read lock on the event list to move to the next event
    pthread_rwlock_rdlock(&rwlock_event_list);
    current = current->next;
    pthread_rwlock_unlock(&rwlock_event_list);
  }

  return 0;
}

void ems_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}
