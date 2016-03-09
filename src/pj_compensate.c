/* For logging.h */
#define _POSIX_C_SOURCE 200809L
#include <gsl/gsl_statistics_double.h>
#include <errno.h>
#include <string.h>
#include <argp.h>
#include <stdlib.h>
#include "logging.h"
#include "events.h"
#include "reader.h"
#include "utlist.h"
#include "queue.h"
#include "args.h"
#include "compensation.h"
#include "pjdread.c"

/* Assumes data and its members are valid */
static int
compensate_state(struct State *state, struct Data *data)
{
  if (state->comm) {
    if (!comm_compensated(state->comm))
      return 1;
    compensate_comm(state, data);
    return 0;
  } else {
    compensate_nocomm(state, data);
    return 0;
  }
}

/* Return the first non-empty queue not returned in the last call */
static struct State_q *
first(struct State_q **qs, size_t ranks)
{
  static size_t i = 0;
  while (i < ranks && state_q_is_empty(qs[i]))
    i++;
  if (i == ranks) {
    i = 0;
    while (i < ranks && state_q_is_empty(qs[i]))
      i++;
    if (i == ranks)
      return NULL;
  }
  return qs[i];
}

/* Try to compensate all enqueued states, popping on success */
static void
compensate_queue(struct State_q **lock_q, struct Data *data)
{
  struct State_q *lock_head = *lock_q;
  while (lock_head && !compensate_state(lock_head->state, data)) {
    state_q_pop(lock_q);
    lock_head = *lock_q;
  }
}

/* DRY */
#define QS_CLEANUP(queue_, queue_str_, i_, f_)\
  do{\
    if ((queue_)[(i_)]) {\
      LOG_ERROR("Queue %s non-empty on rank %zu\n", (queue_str_), (i_));\
      (f_)(((queue_) + (i_)));\
    }\
  }while(0)

/* Compensate all events in the queue, using a lock mechanism */
static void
compensate_loop(struct State_q **state_q, struct Data *data, size_t ranks)
{
  /* Either calloc or ->next = NULL, because of LL_APPEND(head, head) */
  struct State_q **lock_qs = calloc(ranks, sizeof(*lock_qs));
  data->timestamps.last = calloc(ranks, sizeof(*(data->timestamps.last)));
  data->timestamps.c_last = calloc(ranks, sizeof(*(data->timestamps.c_last)));
  if (!lock_qs || !data->timestamps.last || !data->timestamps.c_last)
    REPORT_AND_EXIT();
  /* (from here on, data an its members are all valid) */
  struct State_q *head = *state_q;
  struct State_q *lock_head = NULL;
  while (head || lock_head) {
    if (head) {
      if (lock_qs[head->state->rank]) {
        state_q_push_ref(lock_qs + head->state->rank, head->state);
        compensate_queue(lock_qs + head->state->rank, data);
      } else if (compensate_state(head->state, data)) {
        state_q_push_ref(lock_qs + head->state->rank, head->state);
      }
      state_q_pop(state_q);
    } else {
      compensate_queue(&lock_head, data);
    }
    head = *state_q;
    lock_head = first(lock_qs, ranks);
  }
  /* Cleanup */
  for (size_t i = 0; i < ranks; i++)
    QS_CLEANUP(lock_qs, "Lock", i, state_q_empty);
  free(lock_qs);
  free(data->timestamps.last);
  free(data->timestamps.c_last);
}

static void
compensate(char const *filename, struct Data *data)
{
  struct State_q *state_q = NULL;
  size_t ranks = 1;
  /* These are temporary queues used to generate Comm from Link */
  struct Link_q **link_qs = calloc(ranks, sizeof(*link_qs));
  struct State_q **send_qs = calloc(ranks, sizeof(*send_qs));
  struct State_q **recv_qs = calloc(ranks, sizeof(*recv_qs));
  if (!link_qs || !send_qs || !recv_qs)
    REPORT_AND_EXIT();
  read_events(filename, &ranks, &state_q, &link_qs, &send_qs, &recv_qs);
  /* Generate Comm from link */
  for (size_t i = 0; i < ranks; i++) {
    /* Notice the sort is by end time, i.e. per the Recv order */
    LL_SORT(link_qs[i], link_q_sort_e);
    struct Link_q *link_e, *tmp;
    LL_FOREACH_SAFE(link_qs[i], link_e, tmp) {
      struct Link *link = link_e->link;
      assert(link->to == (int)i);
      struct State *recv = recv_qs[link->to]->state;
      struct State *send = send_qs[link->from]->state;
      recv->comm = comm_new(send, link->container, link->bytes);
      state_q_pop(recv_qs + link->to);
      state_q_pop(send_qs + link->from);
      link_q_pop(link_qs + i);
    }
  }
  /* Cleanup */
  for (size_t i = 0; i < ranks; i++) {
    QS_CLEANUP(link_qs, "Link", i, link_q_empty);
    QS_CLEANUP(send_qs, "Send", i, state_q_empty);
    QS_CLEANUP(recv_qs, "Recv", i, state_q_empty);
  }
  free(link_qs);
  free(send_qs);
  free(recv_qs);
  /* Compensate the queues, printing the results, + cleanup */
  compensate_loop(&state_q, data, ranks);
  QS_CLEANUP(&state_q, "Recv", (size_t)0, state_q_empty);
  free(state_q);
}

int
main(int argc, char **argv)
{
  /* Argument parsing */
  struct arguments args;
  memset(&args, 0, sizeof(args));
  args.start = 0;
  args.end = 1e9;
  args.estimator = 1;
  args.trimming = 0.1f;
  if (argp_parse(&argp, argc, argv, 0, 0, &args) == ARGP_KEY_ERROR) {
    fprintf(stderr, "%s, error during the parsing of parameters\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  if (args.start > args.end) {
    fprintf(stderr, "start must be <= end\n");
    exit(EXIT_FAILURE);
  }
  if (args.estimator != MEAN && args.estimator != HISTOGRAM) {
    fprintf(stderr, "Invalid estimator. See --help\n");
    exit(EXIT_FAILURE);
  }
  if (args.trimming < 0 || args.trimming >= 1) {
    fprintf(stderr, "Invalid trimming factor. See --help:\n");
    exit(EXIT_FAILURE);
  }
  struct Data data = {
    /* These abort on error */
    overhead_read(args.input[2], args.estimator, args.trimming),
    copytime_read(args.input[1]),
    { NULL, NULL }
  };
  compensate(args.input[0], &data);
  /* (cast away the const) */
  overhead_del((struct Overhead *)data.overhead);
  copytime_del((struct Copytime *)data.copytime);
  return 0;
}
