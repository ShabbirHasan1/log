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

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
  pthread_t       worker_thread;
  pthread_mutex_t lock;
  pthread_cond_t  not_full;
  pthread_cond_t  not_empty;
  FILE           *log_file;
  int             shutdown;
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
log_wait(log_ring_t *ring, log_entry_t *entry) {
  pthread_mutex_lock(&ring->lock);
  while (ring->head == ring->tail && !ring->shutdown) {
    pthread_cond_wait(&ring->not_empty, &ring->lock);
  }

  if (ring->head != ring->tail) {
    log_entry_t *next    = &ring->msgs[ring->tail];
    size_t       msg_len = strlen(next->msg);
    entry->msg           = calloc(msg_len + 1, sizeof(char));

    strncpy(entry->msg, next->msg, msg_len);
    entry->msg[msg_len] = '\0';
    entry->destination  = next->destination;
    free(next->msg);
    ring->tail = (ring->tail + 1) % DEFAULT_RING_BUFFER_SIZE;

    pthread_cond_signal(&ring->not_full);
  }
  pthread_mutex_unlock(&ring->lock);
}

void *
log_worker(void *arg) {
  log_ring_t *ring = (log_ring_t *)arg;
  log_entry_t entry;

  while (1) {
    log_wait(ring, &entry);

    if (entry.msg) {
      if (entry.destination == LOG_TO_FILE && ring->log_file) {
        fprintf(ring->log_file, "%s\n", entry.msg);
        fflush(ring->log_file);
      } else {
        printf("%s\n", entry.msg);
        fflush(stdout);
      }

      free(entry.msg);
      entry.msg = NULL;
    }

    if (ring->shutdown && ring->head == ring->tail) {
      break;  // Shutdown signal and empty
    }
  }
  return NULL;
}

// void
// log_message(log_ring_t *ring, const char *msg, log_output_t destination) {
//   log_entry_t entry;
//   size_t      msg_len = strlen(msg);
//   entry.msg           = calloc(msg_len, sizeof(entry.msg));
//   strncpy(entry.msg, msg, msg_len);
//   entry.destination = destination;
//   log_submit(ring, &entry);
// }

void
log_init(log_ring_t *ring, const char *log_file_path) {
  memset(ring, 0, sizeof(log_ring_t));
  pthread_mutex_init(&ring->lock, NULL);
  pthread_cond_init(&ring->not_full, NULL);
  pthread_cond_init(&ring->not_empty, NULL);

  if (log_file_path) {
    ring->log_file = fopen(log_file_path, "a");
    assert(ring->log_file && "Could not open log file");
  }

  int t = pthread_create(&ring->worker_thread, NULL, log_worker, ring);
  assert(t == 0 && "Failed to create worker thread");
}

void
log_cleanup(log_ring_t *ring) {
  pthread_mutex_lock(&ring->lock);
  ring->shutdown = 1;
  pthread_cond_signal(&ring->not_empty);
  pthread_mutex_unlock(&ring->lock);

  pthread_join(ring->worker_thread, NULL);

  if (ring->log_file) {
    fclose(ring->log_file);
  }

  pthread_cond_destroy(&ring->not_full);
  pthread_cond_destroy(&ring->not_empty);
  pthread_mutex_destroy(&ring->lock);
}

#define log(ring, type, color, format, ...)                                    \
  {                                                                            \
    log_entry_t        entry;                                                  \
    unsigned long long ns = TIME_A_BLOCK_NS({                                  \
      int prefix_len = snprintf(NULL, 0, "%s%s\x1b[0m %s%s:%d\x1b[0m ", color, \
                                type, "\x1b[90m", __FILE__, __LINE__);         \
      int msg_len    = snprintf(NULL, 0, format, ##__VA_ARGS__);               \
      int total_len  = prefix_len + msg_len + 1;                               \
      entry.msg      = calloc(total_len, sizeof(char *));                      \
      snprintf(entry.msg, prefix_len + 1, "%s%s\x1b[0m %s%s:%d\x1b[0m ",       \
               color, type, "\x1b[90m", __FILE__, __LINE__);                   \
      snprintf(entry.msg + prefix_len, msg_len + 1, format, ##__VA_ARGS__);    \
      entry.destination = LOG_TO_STDOUT;                                       \
    });                                                                        \
    printf("time: %llu\n", ns);                                                \
    log_submit(ring, &entry);                                                  \
  }

#define log_info(ring, format, ...) \
  log(ring, "INFO ", "\x1b[32m", format, ##__VA_ARGS__)

#define TIME_A_BLOCK_NS(x)                                  \
  ({                                                        \
    struct timespec __start, __end;                         \
    clock_gettime(CLOCK_MONOTONIC_RAW, &__start);           \
    x;                                                      \
    clock_gettime(CLOCK_MONOTONIC_RAW, &__end);             \
    ulong __delta = (__end.tv_sec - __start.tv_sec) * 1e9 + \
                    (__end.tv_nsec - __start.tv_nsec);      \
    __delta;                                                \
  })

int
main() {
  log_ring_t logger;
  log_init(&logger, "app.log");
  // log_message(&logger, "Some printf logging", LOG_TO_STDOUT);
  // log_message(&logger, "A log entry that will end up in app.log",
  // LOG_TO_FILE); Do some important work.... sleep(1); snprintf(char
  // *restrict s, size_t maxlen, const char *restrict format, ...)
  int         num = 5;
  const char *str = "hejsvejhejsvejhejsvejhejsvejhejsvejhejsvejhejsvejhejsvej";
  log_info(&logger, "num: %d, str: %s", num, str);
  // log_info(&logger, "num: %d, str: %s", num, str);
  // log_info(&logger, "num: %d, str: %s", num, str);
  // log_info(&logger, "num: %d, str: %s", num, str);
  // });

  // log_message(&logger, "Another stdout printf log thingy", LOG_TO_STDOUT);
  // log_message(&logger, "Another to the app.log file", LOG_TO_FILE);

  log_cleanup(&logger);
  return 0;
}
