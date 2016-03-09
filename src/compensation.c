#include "compensation.h"
#include "reader.h"
#include "events.h"
#include <assert.h>
#include <stdbool.h>

static inline double
compensate_const(struct State const *state, struct Timestamps const
    *timestamps, struct Overhead const *overhead)
{
  return timestamps->clast[state->rank] + (state->start -
      timestamps->last[state->rank]) - overhead->estimator(overhead->data);
}

/* Updates the state and the timestamps */
#define UPDATE_STATE_TS(state_, start_, end_, timestamps_)\
  do{\
    (timestamps_)->last[(state_)->rank] = (state_)->end;\
    (timestamps_)->clast[(state_)->rank] = (end_);\
    (state_)->start = (start_);\
    (state_)->end = (end_);\
  }while(0)

void
compensate_comm(struct State *recv, struct State *send, struct State *c_send,
    struct Overhead const *overhead, struct Copytime const *copytime, struct
    Timestamps *timestamps)
{
  assert(recv && recv->comm && send && c_send && overhead && copytime &&
      timestamps);
  double c_start = compensate_const(recv, timestamps, overhead);
  double c_end;
  if (recv->start < send->end) {
    double comm = recv->end - send->start;
    if (c_send->start + comm > c_start)
      c_end = c_send->start + comm;
    else
      c_end = c_send->start + copytime->estimator(copytime, recv->comm->bytes);
  } else {
    double comm_upper = recv->end - send->end;
    double comm_lower = 2 * copytime->estimator(copytime, recv->comm->bytes);
    double comm_min = c_start - c_send->start + comm_lower;
    double comm_a_lower = comm_min;
    double comm_a_upper = comm_min > comm_upper ? comm_min : comm_upper;
    /* Currently using mean of upper and lower bound */
    c_end = c_send->start + (comm_a_lower + comm_a_upper) / 2.0;
  }
  /* Compensate link overhead */
  c_end -= overhead->estimator(overhead->data);
  UPDATE_STATE_TS(recv, c_start, c_end, timestamps);
  state_print(recv);
  state_print_c_recv(recv);
}

void
compensate_nocomm(struct State *state, struct Overhead const *o,
    struct Timestamps *timestamps)
{
  double c_start = compensate_const(state, timestamps, o);
  double c_end = c_start + (state->end - state->start) - o->estimator(o->data);
  /* Compensate link overhead */
  if (state_is_send(state))
    c_end -= o->estimator(o->data);
  UPDATE_STATE_TS(state, c_start, c_end, timestamps);
  state_print(state);
}
