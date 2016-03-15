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
compensate_recv(struct State *recv, struct Data *data)
{
  assert(recv && recv->comm);
  struct State *send = recv->comm->match;
  struct State *c_send = recv->comm->c_match;
  double c_start = compensate_const(recv, data);
  double c_end;
  double cpytime = data->copytime->estimator(data->copytime, recv->comm->bytes);
  double comm = recv->end - send->start;
  if (recv->start < send->end) {
    if (c_send->start + comm > c_start)
      c_end = c_send->start + comm;
    else
      c_end = c_send->start + cpytime;
  } else {
    double comm_upper = comm;
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
compensate_ssend(struct State *recv, struct Data *data)
{
  struct State *send = recv->comm->match;
  struct State *c_send = recv->comm->c_match;
  double recv_c_start = compensate_const(recv, data);
  double send_c_start = compensate_const(c_send, data);
  /* (link overhead) */
  recv_c_start -= data->overhead->estimator(data->overhead->data);
  send_c_start -= data->overhead->estimator(data->overhead->data);
  double comm = recv->end - send->start;
  double end;
  if (recv_c_start > send_c_start)
    end = recv_c_start + comm;
  else
    end = send_c_start + comm;
  UPDATE_STATE_TS(recv, recv_c_start, end, data->timestamps);
  UPDATE_STATE_TS(c_send, send_c_start, end, data->timestamps);
  state_print(recv->comm->c_match);
  state_print(recv);
  state_print_c_recv(recv);
}

void
compensate_local(struct State *state, struct Data *data)
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
