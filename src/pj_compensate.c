/* Main application */
/* For logging.h */
#define _POSIX_C_SOURCE 200809L
#include <gsl/gsl_statistics_double.h>
#include <errno.h>
#include <string.h>
#include <argp.h>
#include <stdbool.h>
#include <stdlib.h>
#include "logging.h"
#include "events.h"
#include "reader.h"
#include "utlist.h"
#include "queue.h"
#include "args.h"
#include "compensation.h"
#include "pj_dump_parse.c"

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
      /* OBS: c_match is guaranteed to be a send */
      if (!state_is_local(state->comm->c_match, data->sync_bytes)) {
        compensate_ssend(state, data);
        state_q_pop(lock_qs + state->comm->c_match->rank);
      } else {
        /*
         * An async send might be the head of a lock_q if a non-local event was
         * the former head and got popped via the state_q_pop above instead of
         * the compensate_queue state_q_pop. In this case, the recv can either
         * wait the asend to be compensated as a local event or we can do it
         * here and now (it is the head after all) like we did with the ssend.
         */
        assert(!compensate_state(state->comm->c_match, data, lock_qs));
        state_q_pop(lock_qs + state->comm->c_match->rank);
        compensate_recv(state, data);
      }
    } else {
      return 1;
    }
  } else if (state_is_send(state) && !state_is_local(state, data->sync_bytes)) {
    return 1;
  } else if (state_is_wait(state)) {
    if (comm_is_sync(state->comm, data->sync_bytes)) {
      if (comm_compensated(state->comm))
        compensate_wait(state, data);
      else
        return 1;
    } else {
      if (comm_compensated(state->comm->c_match->comm))
        compensate_wait(state, data);
      else
        return 1;
    }
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
  /*
   * Initialize the timestamps for each rank with the timestamp of the first
   * event from that rank. TODO find a better way to do this
   */
  int *ranks_done = calloc(ranks, sizeof(*ranks_done));
  size_t sum = 0;
  struct State_q *head = *state_q;
  while (sum != ranks && head) {
    if (!(ranks_done[head->state->rank])) {
      data->timestamps.last[head->state->rank] = head->state->start;
      data->timestamps.c_last[head->state->rank] = head->state->start;
      ranks_done[head->state->rank] = 1;
      sum++;
    }
    head = head->next;
  }
  free(ranks_done);
  if (sum != ranks)
    LOG_WARNING("There are empty ranks\n");
  /* (from here onwards, data and its members are all valid) */
  head = *state_q;
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
      /*
       * TODO I don't think we need send->comm, just pass recv->comm to the
       * test functions
       * This order is important. Send creates a comm only with msg byte info
       * (TODO transfer this information to the state struct?), recv then
       * creates a comm linking it to the send, and finally the wait links
       * itself to that recv, giving the graph containing no cyclic references
       * (described in the Hacking/Notes section of README.org)
       */
      struct State *wait = NULL;
      if (send->comm) {
        wait = send->comm->c_match;
        assert(send->comm->ref.count == 1 && wait->ref.count == 2);
        ref_dec(&(send->comm->ref));
      }
      send->comm = comm_new(NULL, NULL, link->bytes);
      send->mark = link->mark;
      /* Currently, comm->container is only used to print Recvs */
      recv->comm = comm_new(send, link->container, link->bytes);
      recv->mark = link->mark;
      if (wait) {
        wait->comm = comm_new(recv, link->container, link->bytes);
        wait->mark = link->mark;
      }
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
   * (lengthy) explanation on pj_dump_parse.c
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
  /* For parse_options */
  errno = 0;
  memset(&args, 0, sizeof(args));
  args.sync_bytes = 4025;
  args.trimming = 0.1f;
  args.estimator = strdup("mean");
  if (!args.estimator)
    REPORT_AND_EXIT();
  if (argp_parse(&argp, argc, argv, 0, 0, &args) == ARGP_KEY_ERROR)
    LOG_AND_EXIT("Unknown error while parsing parameters\n");
  struct Copytime *copytime = NULL;
  copytime_read(args.input[1], &copytime);
  struct Data data = {
    /* These abort on error */
    overhead_read(args.input[2], args.estimator, args.trimming),
    copytime,
    { NULL, NULL },
    args.sync_bytes
  };
  free(args.estimator);
  compensate(args.input[0], &data);
  /* (cast away the const) */
  overhead_del((struct Overhead *)(data.overhead));
  copytime_del(&copytime);
  return 0;
}
