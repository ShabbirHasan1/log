/* MIT License
 *
 * Copyright (c) 2024 sven
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// #define LOG_WITH_TIME
// #define LOG_DEBUG
// #include "../log.h"

#include <pthread.h>
#include <stdio.h>

// TODO(imprv) user definable?
#define DEFAULT_RING_BUFFER_SIZE (1024)

typedef enum { LOG_TO_FILE, LOG_TO_STDOUT } log_output_t;

typedef struct {
  char        *msg;
  log_output_t destination;
} log_entry_t;

typedef struct {
  log_entry_t     msgs[DEFAULT_RING_BUFFER_SIZE];
  size_t          head;
  size_t          tail;
  pthread_mutex_t lock;
  pthread_cond_t  not_full;
  pthread_cond_t  not_empty;
  FILE           *log_file;
} log_ring_t;

void
log_submit(log_ring_t *ring, log_entry_t *msg) {
  pthread_mutex_lock(&ring->lock);
  while ((ring->head + 1) % DEFAULT_RING_BUFFER_SIZE == ring->tail) {
    pthread_cond_wait(&ring->not_full, &ring->lock);
  }
  ring->msgs[ring->head] = *msg;
  ring->head             = (ring->head + 1) % DEFAULT_RING_BUFFER_SIZE;
  pthread_cond_signal(&ring->not_empty);
  pthread_mutex_unlock(&ring->lock);
}

void
log_wait(log_ring_t *ring, log_entry_t *msg) {
  pthread_mutex_lock(&ring->lock);
  while (ring->head == ring->tail) {
    pthread_cond_wait(&ring->not_empty, &ring->lock);
  }
  *msg       = ring->msgs[ring->tail];
  ring->tail = (ring->tail + 1) % DEFAULT_RING_BUFFER_SIZE;
  pthread_cond_signal(&ring->not_full);
  pthread_mutex_unlock(&ring->lock);
}

void
log_cleanup(log_ring_t *ring) {
  if (ring->log_file) {
    fclose(ring->log_file);
  }
  pthread_mutex_destroy(&ring->lock);
  pthread_cond_destroy(&ring->not_full);
  pthread_cond_destroy(&ring->not_empty);
}

int
main() {
  log_ring_t logger;
  //   log_init(&logger, "app.log");
}
