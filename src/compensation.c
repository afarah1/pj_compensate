#include "compensation.h"
#include "reader.h"
#include "events.h"
#include <assert.h>
#include <stdbool.h>

/*
 * All functions assume "data" and its members are set and valid!
 */

/* Assumes state is set and valid */
static inline double
compensate_const(struct State const *state, struct Data *data)
{
  return data->timestamps.c_last[state->rank] + (state->start -
      data->timestamps.last[state->rank]) -
    data->overhead->estimator(data->overhead->data);
}

/* Updates the state and the timestamps */
#define UPDATE_STATE_TS(state_, start_, end_, timestamps_)\
  do{\
    (timestamps_).last[(state_)->rank] = (state_)->end;\
    (timestamps_).c_last[(state_)->rank] = (end_);\
    (state_)->start = (start_);\
    (state_)->end = (end_);\
  }while(0)

void
compensate_comm(struct State *recv, struct Data *data)
{
  assert(recv && recv->comm);
  struct State *send = recv->comm->match;
  struct State *c_send = recv->comm->c_match;
  double c_start = compensate_const(recv, data);
  double c_end;
  double cpytime = data->copytime->estimator(data->copytime, recv->comm->bytes);
  if (recv->start < send->end) {
    double comm = recv->end - send->start;
    if (c_send->start + comm > c_start)
      c_end = c_send->start + comm;
    else
      c_end = c_send->start + cpytime;
  } else {
    double comm_upper = recv->end - send->end;
    double comm_lower = 2 * cpytime;
    double comm_min = c_start - c_send->start + comm_lower;
    double comm_a_lower = comm_min;
    double comm_a_upper = comm_min > comm_upper ? comm_min : comm_upper;
    /* Currently using mean of upper and lower bound */
    c_end = c_send->start + (comm_a_lower + comm_a_upper) / 2.0;
  }
  /* Compensate link overhead */
  c_end -= data->overhead->estimator(data->overhead->data);
  UPDATE_STATE_TS(recv, c_start, c_end, data->timestamps);
  state_print(recv);
  state_print_c_recv(recv);
}

void
compensate_nocomm(struct State *state, struct Data *data)
{
  double c_start = compensate_const(state, data);
  double c_end = c_start + (state->end - state->start) -
    data->overhead->estimator(data->overhead->data);
  /* Compensate link overhead */
  if (state_is_send(state))
    c_end -= data->overhead->estimator(data->overhead->data);
  UPDATE_STATE_TS(state, c_start, c_end, data->timestamps);
  state_print(state);
}
