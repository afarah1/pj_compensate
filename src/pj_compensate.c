/* Main application */
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
 * This file works basically like this:
 *
 * 0. Read user input and intrusion data (see main, args.h)
 * 1. Read all events from the trace into States and Links (see pjdread.c)
 * 2. Link sends and recvs using a Comm struct and discard Links
 *    (see link_send_recvs)
 * 3. Process all events (see compensate_loop)
 */

#if LOG_LEVEL == LOG_LEVEL_DEBUG
/* Used for debugging this file (the #defines are at logging.h) */
static void
p_q(struct State_q **lq, size_t ranks, size_t states)
{
  for (size_t i = 0; i < ranks; i++) {
    fprintf(stderr, "%zu [ ", i);
    struct State_q *head = lq[i];
    size_t j = 0;
    while (head && j < states) {
      if (state_is_recv(head->state))
        fprintf(stderr, "%s (%d, %p), ", head->state->routine,
            head->state->comm->c_match->rank,
            (void *)(head->state->comm->c_match));
      else
        fprintf(stderr, "%s (%p), ", head->state->routine, (void *)(head->state));
      head = head->next;
      j++;
    }
    fprintf(stderr, " ],\n");
  }
}
#endif

/* DRY */
#define QS_CLEANUP(queue_, queue_str_, i_, f_)\
  do{\
    if ((queue_)[(i_)]) {\
      LOG_ERROR("Queue %s non-empty on rank %zu\n", (queue_str_), (i_));\
      (f_)(((queue_) + (i_)));\
    }\
  }while(0)

/* Is the state head of its queue? */
static inline bool
is_head(struct State *state, struct State_q **lock_qs)
{
  struct State_q *head = lock_qs[state->rank];
  return (head && head->state == state);
}

/*
 * Compensate the state, return 0 on success and 1 on failure. Assumes data
 * and its members are valid.
 */
static int
compensate_state(struct State *state, struct Data *data,
    struct State_q **lock_qs)
{
  if (state_is_recv(state)) {
    assert(state->comm);
    /* To compensate a recv the matching send should've been compensated 1st */
    if (comm_compensated(state->comm)) {
      compensate_recv(state, data);
    /* Or be the head of the lock queue for the rank of the matching send */
    } else if (is_head(state->comm->c_match, lock_qs)) {
      /*
       * Only non-local events need to wait for their counterpart, thus they're
       * the only heads of lock queues. If the head is a counterpart of the
       * current event, and given that the current event is a recv, then it
       * must be a sync send.
       */
      assert(state_is_send(state->comm->c_match) &&
          !state_is_local(state->comm->c_match, data->sync_bytes));
      compensate_ssend(state, data);
      state_q_pop(lock_qs + state->comm->c_match->rank);
    } else {
      return 1;
    }
    return 0;
  } else if (state_is_send(state) && !state_is_local(state, data->sync_bytes)) {
    return 1;
  } else if (state_is_wait(state)) {
    if (!comm_is_sync(state->comm->c_match->comm, data->sync_bytes)) {
      LOG_CRITICAL("Matching Send for Wait on rank %d mark %"PRIu64" is "
          "asynchronous (%zu bytes). This is not supported.\n", state->rank,
          state->mark, state->comm->c_match->comm->bytes);
      exit(EXIT_FAILURE);
    }
    // TODO asserts and dont repeat dereferencing, i.e. pass the recv to c_wait
    /* if (comm_compensated(send->comm), i.e. if recv was compensated */
    if (comm_compensated(state->comm->c_match->comm))
      compensate_wait(state, data);
    else
      return 1;
  /* If the state is local, just compensate it */
  } else {
    compensate_local(state, data);
  }
  return 0;
}

// TODO improve this
/* Try to compensate all enqueued states, popping on success */
static inline void
compensate_queue(struct State_q **lock_qs, int offset, struct Data *data)
{
  while (lock_qs[offset] &&
      !compensate_state(lock_qs[offset]->state, data, lock_qs))
    state_q_pop(lock_qs + offset);
}

/* Cycles through non-empty queues, returning an index, or -1 if all empty */
static int
cycle(struct State_q **qs, int ranks, int last)
{
  if (last == -1)
    last = 0;
  int i = (last + 1) % ranks;
  while (i != last && state_q_is_empty(qs[i]))
    i = (i + 1) % ranks;
  if (i == last && state_q_is_empty(qs[i]))
    return -1;
  return i;
}

/* Compensate all events in the queue, using a lock mechanism */
static void
compensate_loop(struct State_q **state_q, struct Data *data, size_t ranks)
{
  /* Either calloc or ->next = NULL, because of DL_APPEND(head, head) */
  struct State_q **lock_qs = calloc(ranks, sizeof(*lock_qs));
  /* (notice these are restrict and may not be aliased) */
  data->timestamps.last = calloc(ranks, sizeof(*(data->timestamps.last)));
  data->timestamps.c_last = calloc(ranks, sizeof(*(data->timestamps.c_last)));
  if (!lock_qs || !(data->timestamps.last) || !(data->timestamps.c_last))
    REPORT_AND_EXIT();
  /* (from here onwards, data and its members are all valid) */
  struct State_q *head = *state_q;
  int lock_head = 0;
  while (head || lock_head != -1) {
    if (head) {
      if (lock_qs[head->state->rank]) {
        state_q_push_ref(lock_qs + head->state->rank, head->state);
        compensate_queue(lock_qs, head->state->rank, data);
      } else if (compensate_state(head->state, data, lock_qs)) {
        state_q_push_ref(lock_qs + head->state->rank, head->state);
      }
      state_q_pop(state_q);
    } else {
      compensate_queue(lock_qs, lock_head, data);
    }
    head = *state_q;
    lock_head = cycle(lock_qs, (int)ranks, lock_head);
  }
  /* Cleanup */
  for (size_t i = 0; i < ranks; i++)
    QS_CLEANUP(lock_qs, "Lock", i, state_q_empty);
  free(lock_qs);
  free(data->timestamps.last);
  free(data->timestamps.c_last);
}

static void
link_send_recvs(struct Link_q **links, struct State_q **recvs, struct State
    ***sends, uint64_t *slens, size_t ranks)
{
  assert(links && recvs && sends && slens);
  for (size_t i = 0; i < ranks; i++) {
    DL_SORT(links[i], link_q_sort_e);
    struct Link_q *link_e = NULL, *tmp = NULL;
    DL_FOREACH_SAFE(links[i], link_e, tmp) {
      struct Link *link = link_e->link;
      assert(link && link->to == (int)i);
      struct State *recv = recvs[link->to]->state;
      assert(slens[link->from] > link->mark);
      struct State *send = sends[link->from][link->mark];
      if (!send || !recv) {
        LOG_CRITICAL("No matching %s for link. Comm from rank %d @ %.15f mark "
            "%"PRIu64" to rank %d @ %.15f. Unsupported routine?\n", send ?
            "recv" : (recv ? "send" : "send nor recv"), link->from,
            link->start, link->mark, link->to, link->end);
        exit(EXIT_FAILURE);
      }
      recv->comm = comm_new(send, link->container, link->bytes);
      send->comm = comm_new(recv, NULL, link->bytes);
      sends[link->from][link->mark] = NULL;
      ref_dec(&(send->ref));
      state_q_pop(recvs + link->to);
      link_q_pop(links + i);
    }
  }
  /* Cleanup */
  for (size_t i = 0; i < ranks; i++) {
    /* This inner loop is not necessary, it's just a check */
    for (size_t j = 0; j < slens[i]; j++)
      if (sends[i][j]) {
        LOG_ERROR("Queue Sends non-empty on rank %zu\n", i);
        ref_dec(&(sends[i][j]->ref));
      }
    QS_CLEANUP(links, "Link", i, link_q_empty);
    QS_CLEANUP(recvs, "Recv", i, state_q_empty);
    free(sends[i]);
  }
  free(sends);
  free(slens);
  free(links);
  free(recvs);
}

static void
compensate(char const *filename, struct Data *data)
{
  struct State_q *state_q = NULL;
  size_t ranks = 0;
  /*
   * These are temporary queues used link sends to recvs. More details, see the
   * (lengthy) explanation on pjdread.c
   */
  struct Link_q **links = NULL;
  struct State_q **recvs = NULL;
  struct State ***sends = NULL;
  uint64_t *slens = NULL;
  /* (allocate and fill) */
  read_events(filename, &ranks, &state_q, &links, &sends, &recvs, &slens);
  /* (empty and free) */
  link_send_recvs(links, recvs, sends, slens, ranks);
  /* Compensate the queues, printing the results, cleanup */
  compensate_loop(&state_q, data, ranks);
  QS_CLEANUP(&state_q, "State", (size_t)0, state_q_empty);
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
  args.sync_bytes = 4025;
  args.estimator = 1;
  args.trimming = 0.1f;
  if (argp_parse(&argp, argc, argv, 0, 0, &args) == ARGP_KEY_ERROR)
    LOG_AND_EXIT("Unknown error while parsing parameters\n");
  if (args.start > args.end)
    LOG_AND_EXIT("start must be <= end\n");
  if (args.estimator != MEAN && args.estimator != HISTOGRAM)
    LOG_AND_EXIT("Invalid estimator. See --help\n");
  if (args.trimming < 0 || args.trimming >= 1)
    LOG_AND_EXIT("Invalid trimming factor. See --help:\n");
  struct Data data = {
    /* These abort on error */
    overhead_read(args.input[2], args.estimator, args.trimming),
    copytime_read(args.input[1]),
    { NULL, NULL },
    args.sync_bytes
  };
  compensate(args.input[0], &data);
  /* (cast away the const) */
  overhead_del((struct Overhead *)(data.overhead));
  copytime_del((struct Copytime *)(data.copytime));
  return 0;
}
