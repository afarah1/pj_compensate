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

/*
 * Try to compensate state (any), return 0 on success and 1 on failure. Takes
 * the send queues required for compensating MPI_Recv, as well as the overhead
 * and copytime structs and the timestamp info struct.
 */
static int
compensate_state(struct State *state, struct Overhead const *overhead, struct
    Copytime const *copytime, struct ts_info *ts)
{
  assert(state && overhead && copytime && ts);
  if (state->comm) {
    if (!comm_compensated(state->comm))
      return 1;
    compensate_comm(state, state->comm->match, state->comm->c_match, overhead,
        copytime, ts);
    return 0;
  } else {
    compensate_nocomm(state, overhead, ts);
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
    return NULL;
  }
  return qs[i];
}

/*
 * Try to compensate all enqueued states, popping on success (also update the
 * c_send queues). As all static functions, this is intended for internal use.
 */
static void
compensate_queue(struct State_q **lock_q, struct Overhead const *overhead,
    struct Copytime const *copytime, struct ts_info *ts)
{
  struct State_q *lock_head = *lock_q;
  while (lock_head && !compensate_state(lock_head->state, overhead, copytime,
        ts)) {
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

/* Compensate all events in the queue (implements the lock mechanism etc) */
static void
compensate_loop(struct State_q **state_q, struct Overhead const *overhead,
    struct Copytime const *copytime, size_t ranks)
{
  /* Either calloc or ->next = NULL, because of LL_APPEND(head, head) */
  struct State_q **lock_qs = calloc(ranks, sizeof(*lock_qs));
  struct ts_info ts;
  ts.last = calloc(ranks, sizeof(*(ts.last)));
  ts.clast = calloc(ranks, sizeof(*(ts.clast)));
  if (!lock_qs || !ts.last || !ts.clast)
    REPORT_AND_EXIT();
  struct State_q *head = *state_q;
  struct State_q *lock_head = NULL;
  while (head || lock_head) {
    if (head) {
      if (lock_qs[head->state->rank]) {
        state_q_push_ref(lock_qs + head->state->rank, head->state);
        compensate_queue(lock_qs + head->state->rank, overhead, copytime, &ts);
      } else if (compensate_state(head->state, overhead, copytime, &ts)) {
        state_q_push_ref(lock_qs + head->state->rank, head->state);
      }
      state_q_pop(state_q);
    } else {
      compensate_queue(&lock_head, overhead, copytime, &ts);
    }
    head = *state_q;
    lock_head = first(lock_qs, ranks);
  }
  /* Cleanup */
  for (size_t i = 0; i < ranks; i++)
    QS_CLEANUP(lock_qs, "Lock", i, state_q_empty);
  free(lock_qs);
  free(ts.last);
  free(ts.clast);
}

static void
compensate(char const *filename, struct Overhead const *overhead, struct
    Copytime const *copytime)
{
  /* Read the trace events into the queues */
  struct State_q *state_q = NULL;
  size_t ranks = 1;
  struct Link_q **link_qs = calloc(ranks, sizeof(*link_qs));
  struct State_q **send_qs = calloc(ranks, sizeof(*send_qs));
  struct State_q **recv_qs = calloc(ranks, sizeof(*recv_qs));
  if (!link_qs || !send_qs || !recv_qs)
    REPORT_AND_EXIT();
  read_events(filename, &ranks, &state_q, &link_qs, &send_qs, &recv_qs);
  for (size_t i = 0; i < ranks; i++) {
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
  compensate_loop(&state_q, overhead, copytime, ranks);
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
  /* Read data */
  struct Copytime *copytime = copytime_read(args.input[1]);
  struct Overhead *overhead = overhead_read(args.input[2], args.estimator,
      args.trimming);
  /* Compensate trace + cleanup */
  compensate(args.input[0], overhead, copytime);
  overhead_del(overhead);
  copytime_del(copytime);
  return 0;
}
