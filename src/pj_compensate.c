/* Main application */
// TODO refactor scatter stuff, add scatter example trace also analyse more in
// depth how overhead is added upon event creation by aky.c
/* For logging.h */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <argp.h>
#include <stdbool.h>
#include <stdlib.h>
#include "logging.h"
#include "events.h"
#include "copytime.h"
#include "utlist.h"
#include "queue.h"
#include "args.h"
#include "compensation.h"
#include "pj_dump_read.c"

#define ASSERTSTRTO(nptr, endptr)\
  do {\
    if (errno || (endptr) == (nptr)) {\
      fprintf(stderr, "Invalid argument %s: %s.\n", (nptr), errno ?\
          strerror(errno) : "No digits were found");\
      exit(EXIT_FAILURE);\
    }\
  } while(0)

#if LOG_LEVEL == LOG_LEVEL_DEBUG
static void
print_queues(struct State_q **queues, size_t ranks, size_t states)
{
  for (size_t i = 0; i < ranks; i++) {
    fprintf(stderr, "%zu [ ", i);
    struct State_q *head = queues[i];
    size_t j = 0;
    while (head && j < states) {
      if (state_is_recv(head->state))
        fprintf(stderr, "%s (%d, %p), ", head->state->routine,
            head->state->comm.c->match->rank,
            (void *)(head->state->comm.c->match));
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
#define QUEUES_CLEANUP(queue_, queue_str_, i_, f_)\
  do{\
    if ((queue_)[(i_)]) {\
      LOG_ERROR("Queue %s non-empty on rank %zu\n", (queue_str_), (i_));\
      (f_)(((queue_) + (i_)));\
    }\
  }while(0)

static inline bool
is_head(struct State *state, struct State_q **lock_qs)
{
  struct State_q *head = lock_qs[state->rank];
  return (head && head->state == state);
}

/* Circular references in the two functions below */
static int
compensate_state(struct State *state, struct Data *data, struct State_q
    **lock_qs, bool lower);

static int
compensate_state_recv(struct State *recv, struct State *match, double ostart,
    double oend, struct Data *data, struct State_q **lock_qs, bool lower)
{
  int ans = 0;
  /* To compensate a recv the matching send should've been compensated first */
  if (compensated(match, ostart, oend)) {
    compensate_recv_(recv, match, ostart, oend, data, lower);
  /* Or be the head of the lock queue for the rank of the matching send */
  } else if (is_head(match, lock_qs)) {
    /* OBS: match is guaranteed to be a send */
    if (!state_is_local(match, data->sync_bytes)) {
      compensate_ssend_(recv, match, ostart, oend, data);
      state_q_pop(lock_qs + match->rank);
    } else {
      /*
       * An async send might be the head of a lock_q if a non-local event was
       * the former head and got popped via the state_q_pop above instead of
       * the compensate_queue state_q_pop. In this case, the recv can either
       * wait the asend to be compensated as a local event or we can do it
       * here and now (it is the head after all) like we did with the ssend.
       */
      assert(!compensate_state(match, data, lock_qs, lower));
      state_q_pop(lock_qs + match->rank);
      compensate_recv_(recv, match, ostart, oend, data, lower);
    }
  } else {
    ans = 1;
  }
  return ans;
}

/*
 * Compensate the state, return 0 on success and 1 on failure. Assumes data
 * and its members are valid.
 */
static int
compensate_state(struct State *state, struct Data *data, struct State_q
    **lock_qs, bool lower)
{
  int ans = 0;
  if (state_is_recv(state)) {
    assert(state->comm.c);
    ans = compensate_state_recv(state, state->comm.c->match, state->comm.c->ostart,
        state->comm.c->oend, data, lock_qs, lower);
  } else if (state_is_send(state) && !state_is_local(state, data->sync_bytes)) {
    ans = 1;
  } else if (state_is_wait(state)) {
    if (comm_is_sync(state->comm.c, data->sync_bytes)) {
      if (comm_compensated(state->comm.c))
        compensate_wait(state, data);
      else
        ans = 1;
    } else {
      if (comm_compensated(state->comm.c->match->comm.c))
        compensate_wait(state, data);
      else
        ans = 1;
    }
  } else if (state_is_1tn(state)) {
    assert(state->comm.c);
    if (state_is_1tns(state)) {
      if (comm_is_sync(state->comm.c, data->sync_bytes))
        ans = 1;
      else
        compensate_local(state, data);
    } else {
      ans = compensate_state_recv(state, state->comm.c->match,
          state->comm.c->ostart, state->comm.c->oend, data, lock_qs, lower);
    }
  } else if (state_is_nt1(state)) {
    assert(state->comm.g);
    if (state_is_nt1s(state)) {
      if (comm_is_sync(state->comm.g, data->sync_bytes))
        ans = 1;
      else
        compensate_local(state, data);
    } else {
      ans = 0;
      for (size_t i = 0; i < state->comm.g->ranks; i++)
        if (state->comm.g->match[i]) {
          if (compensate_state_recv(state, state->comm.g->match[i],
                state->comm.g->ostart[i], state->comm.g->oend[i], data, lock_qs,
                lower))
            LOG_AND_EXIT("GatherRecv could not be compensated because of "
                "blocking GatherSend. This is yet to be implemented\n");
        }
    }
  } else {
    compensate_local(state, data);
  }
  return ans;
}

// TODO improve this
/* Try to compensate all enqueued states, popping on success */
static inline void
compensate_queue(struct State_q **lock_qs, int offset, struct Data *data, bool
    lower)
{
  while (lock_qs[offset] &&
      !compensate_state(lock_qs[offset]->state, data, lock_qs, lower))
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
compensate_loop(struct State_q **state_q, struct Data *data, size_t ranks, bool
    lower)
{
  /* Either calloc or ->next = NULL, because of DL_APPEND(head, head) */
  struct State_q **lock_qs = calloc(ranks, sizeof(*lock_qs));
  if (!lock_qs)
    REPORT_AND_EXIT;
  /* (from here onwards, data and its members are all valid) */
  struct State_q *head = *state_q;
  int lock_head = 0;
  while (head || lock_head != -1) {
    if (head) {
      if (lock_qs[head->state->rank]) {
        state_q_push_ref(lock_qs + head->state->rank, head->state);
        compensate_queue(lock_qs, head->state->rank, data, lower);
      } else if (compensate_state(head->state, data, lock_qs, lower)) {
        state_q_push_ref(lock_qs + head->state->rank, head->state);
      }
      state_q_pop(state_q);
    } else {
      compensate_queue(lock_qs, lock_head, data, lower);
    }
    head = *state_q;
    lock_head = cycle(lock_qs, (int)ranks, lock_head);
  }
  /* Cleanup */
  for (size_t i = 0; i < ranks; i++)
    QUEUES_CLEANUP(lock_qs, "Lock", i, state_q_empty);
  free(lock_qs);
}

static inline void
no_matching_comm(struct State const *send, struct State const *recv,
    struct Link const *link)
{
  LOG_AND_EXIT("No matching %s for link. Comm from rank %d @ %.15f mark "
      "%"PRIu64" to rank %d @ %.15f. Unsupported routine? Spaces or () in "
      "the routine name?\n", send ? "recv" : (recv ? "send" : "send nor recv"),
      link->from, link->start, link->mark, link->to, link->end);
}

static void
link_send_recvs(struct Link_q **links, struct State_q **recvs, struct State
    ***sends, uint64_t *slens, size_t ranks, struct State_q **scattersS,
    struct State_q **scattersR, struct State_q **gathersS, struct State_q
    **gathersR)
{
  for (size_t i = 0; i < ranks; i++) {
    DL_SORT(links[i], link_q_sort_e);
    struct Link_q *link_e = NULL,
                  *link_tmp = NULL;
    DL_FOREACH_SAFE(links[i], link_e, link_tmp) {
      struct Link *link = link_e->link;
      assert(link);
      if (link_is_1tn(link)) {
        // FIXME we should somehow pop the link at the 'for' below
        if (!scattersS[link->from]) {
          LOG_DEBUG("No ScattersS for link->from %d\n", link->from);
          link_q_pop(links + i);
          continue;
        }
        struct State *scatterS = scattersS[link->from]->state;
        scatterS->comm.c = comm_new(NULL, NULL, link->bytes);
        struct Comm *comm_recvs = comm_new(scatterS, link->container,
            link->bytes);
        for (size_t j = 0; j < ranks; j++) {
          if (j != (size_t)(link->from)) {
            assert(scattersR[j]);
            struct State *scatterR = scattersR[j]->state;
            scatterR->comm.c = comm_recvs;
            ref_inc(&(comm_recvs->ref));
            // TODO perhaps we should have independent marks for 1TN?
            scatterR->mark = scatterS->mark;
            state_q_pop(scattersR + j);
          }
        }
        ref_dec(&(comm_recvs->ref));
        state_q_pop(scattersS + link->from);
      } else if (link_is_nt1(link)) {
        if (!gathersS[link->from]) {
          LOG_DEBUG("Not GathersS for link->from %d\n", link->from);
          link_q_pop(links + i);
          continue;
        }
        struct State *gatherR = gathersR[link->to]->state;
        struct State **gather_sends = calloc(ranks, sizeof(*gather_sends));
        if (!gather_sends)
          REPORT_AND_EXIT;
        struct Comm *comm_sends = comm_new(gatherR, link->container,
            link->bytes);
        for (size_t j = 0; j < ranks; j++) {
          if (j != (size_t)(link->to)) {
            if (!gathersS[j])
              LOG_AND_EXIT("No gather (send) at rank %zu for gather (recv) at "
                  "rank %zu", j, i);
            struct State *gatherS = gathersS[j]->state;
            gatherS->comm.c = comm_sends;
            ref_inc(&(comm_sends->ref));
            gather_sends[j] = gatherS;
            ref_inc(&(gatherS->ref));
            state_q_pop(gathersS + j);
          }
        }
        gatherR->comm.g = gcomm_new(gather_sends, link->container,
            link->bytes, ranks);
        ref_dec(&(comm_sends->ref));
        state_q_pop(gathersR + link->to);
      } else {
        assert(link->to == (int)i && link_is_ptp(link));
        if (slens[link->from] <= link->mark)
          no_matching_comm(NULL, NULL, link);
        struct State *recv = recvs[link->to]->state;
        struct State *send = sends[link->from][link->mark];
        if (!send || !recv)
          no_matching_comm(send, recv, link);
        /*
         * TODO I don't think we need send->comm.c, just pass recv->comm.c to the
         * test functions
         * This order is important. Send creates a comm only with msg byte info
         * (TODO transfer this information to the state struct?), recv then
         * creates a comm linking it to the send, and finally the wait links
         * itself to that recv, giving the graph containing no cyclic references
         * (described in the Hacking/Notes section of README.org)
         */
        struct State *wait = NULL;
        if (send->comm.c) {
          wait = send->comm.c->match;
          assert(send->comm.c->ref.count == 1 && wait->ref.count == 2);
          ref_dec(&(send->comm.c->ref));
        }
        send->comm.c = comm_new(NULL, NULL, link->bytes);
        send->mark = link->mark;
        recv->comm.c = comm_new(send, link->container, link->bytes);
        recv->mark = link->mark;
        if (wait) {
          wait->comm.c = comm_new(recv, link->container, link->bytes);
          // TODO isn't this already done @ pj_dump_read.c?
          wait->mark = link->mark;
        }
        sends[link->from][link->mark] = NULL;
        ref_dec(&(send->ref));
        state_q_pop(recvs + link->to);
      }
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
    QUEUES_CLEANUP(links, "Link", i, link_q_empty);
    QUEUES_CLEANUP(recvs, "Recv", i, state_q_empty);
    QUEUES_CLEANUP(scattersS, "scattersS", i, state_q_empty);
    QUEUES_CLEANUP(scattersR, "scattersR", i, state_q_empty);
    QUEUES_CLEANUP(gathersS, "gathersS", i, state_q_empty);
    QUEUES_CLEANUP(gathersR, "gathersR", i, state_q_empty);
    free(sends[i]);
  }
  free(sends);
  free(slens);
  free(links);
  free(recvs);
  free(scattersS);
  free(scattersR);
  free(gathersS);
  free(gathersR);
}

static void
compensate(char const *filename, bool lower, struct Data *data)
{
  assert(data);
  struct State_q *state_q = NULL;
  size_t ranks = 0;
  /*
   * These are temporary queues used link sends to recvs. More details, see the
   * (lengthy) explanation on pj_dump_parse.c
   */
  struct Link_q **links = NULL;
  struct State_q **recvs = NULL,
                 **scattersS = NULL,
                 **scattersR = NULL,
                 **gathersS = NULL,
                 **gathersR = NULL;
  struct State ***sends = NULL;
  uint64_t *slens = NULL;
  /* (allocate and fill) */
  data->timestamps.last = NULL;
  data->timestamps.c_last = NULL;
  read_events(filename, &ranks, &state_q, &links, &sends, &recvs, &slens,
      &(data->timestamps.last), &(data->timestamps.c_last), &scattersS,
        &scattersR, &gathersS, &gathersR);
  /* (empty and free) */
  link_send_recvs(links, recvs, sends, slens, ranks, scattersS, scattersR,
      gathersS, gathersR);
  /* Compensate the queues, printing the results, cleanup */
  compensate_loop(&state_q, data, ranks, lower);
  QUEUES_CLEANUP(&state_q, "State", (size_t)0, state_q_empty);
  free(state_q);
  free(data->timestamps.last);
  free(data->timestamps.c_last);
}

int
main(int argc, char **argv)
{
  /* Argument parsing */
  struct arguments args;
  memset(&args, 0, sizeof(args));
  args.lower = false;
  if (argp_parse(&argp, argc, argv, 0, 0, &args) == ARGP_KEY_ERROR)
    LOG_AND_EXIT("Unknown error while parsing parameters\n");
  char *endptr = NULL;
  double overhead = strtod(args.input[2], &endptr);
  ASSERTSTRTO(args.input[2], endptr);
  size_t sync_bytes = (size_t)strtoull(args.input[3], &endptr, 10);
  ASSERTSTRTO(args.input[3], endptr);
  struct Copytime *copytime = NULL;
  int rc = copytime_read(args.input[1], &copytime);
  if (rc)
    REPORT_AND_EXIT;
  struct Data data = {
    overhead,
    copytime,
    { NULL, NULL },
    sync_bytes
  };
  compensate(args.input[0], args.lower, &data);
  copytime_del(&copytime);
  return 0;
}
