#include "compensation.h"
#include "reader.h"
#include "events.h"
#include <assert.h>
#include <stdbool.h>

static inline double
compensate_const(struct State const *state, struct ts_info const *ts,
    struct Overhead const *overhead)
{
  return ts->clast[state->rank] + (state->start - ts->last[state->rank]) -
    overhead->estimator(overhead->data);
}

/* Macro for compensation functions (updates the state and the ts, prints) */
#define UPDATE_STATE_TS(state_, start_, end_, ts_)\
  do{\
    (ts_)->last[(state_)->rank] = (state_)->end;\
    (ts_)->clast[(state_)->rank] = (end_);\
    (state_)->start = (start_);\
    (state_)->end = (end_);\
    state_print((state_));\
  }while(0)

void
compensate_comm(struct State *recv, struct State *send, struct State *c_send,
    struct Overhead const *overhead, struct Copytime const *copytime, struct
    ts_info *ts)
{
  assert(recv && send && recv->link && c_send && overhead && copytime && ts);
  double c_start = compensate_const(recv, ts, overhead);
  double c_end;
  if (recv->start < send->end) {
    double comm = recv->end - send->start;
    if (c_send->start + comm > c_start)
      c_end = c_send->start + comm;
    else
      c_end = c_send->start + copytime->estimator(copytime, recv->link->bytes);
  } else {
    double comm_upper = recv->end - send->end;
    double comm_lower = 2 * copytime->estimator(copytime, recv->link->bytes);
    double comm_min = c_start - c_send->start + comm_lower;
    double comm_a_lower = comm_min;
    double comm_a_upper = comm_min > comm_upper ? comm_min : comm_upper;
    /* Currently using mean of upper and lower bound */
    c_end = c_send->start + (comm_a_lower + comm_a_upper) / 2.0;
  }
  /* Compensate link overhead */
  c_end -= overhead->estimator(overhead->data);
  UPDATE_STATE_TS(recv, c_start, c_end, ts);
  recv->link->start = c_send->start;
  recv->link->end = c_end;
  link_print(recv->link);
}

void
compensate_nocomm(struct State *state, struct Overhead const *o,
    struct ts_info *ts)
{
  double c_start = compensate_const(state, ts, o);
  double c_end = c_start + (state->end - state->start) - o->estimator(o->data);
  /* Compensate link overhead */
  if (state_is_send(state))
    c_end -= o->estimator(o->data);
  UPDATE_STATE_TS(state, c_start, c_end, ts);
}
