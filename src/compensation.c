/* See the header file for contracts and more docs */
#include "compensation.h"
#include "reader.h"
#include "events.h"
#include <assert.h>
#include <stdbool.h>

/*
 * Note: None of the functions defined in this file makes any assertion about
 * Data (a NULL pointer will result in segfaults).
 */

/* Wrapper */
#define OVERHEAD(data) data->overhead->estimator(data->overhead->data)

/*
 * Return the compensated timestamp for the local event 'state', without
 * altering it or the timestamp register (data struct). Assumes both have
 * been properly initialized.
 */
static inline double
compensate_const(struct State const *state, struct Data const *data)
{
  return data->timestamps.c_last[state->rank] + (state->start -
      data->timestamps.last[state->rank]) -
    data->overhead->estimator(data->overhead->data);
}

/* Updates state and data timestamps */
#define UPDATE_STATE_TS(state_, start_, end_, timestamps_)\
  do{\
    (timestamps_).last[(state_)->rank] = (state_)->end;\
    (timestamps_).c_last[(state_)->rank] = (end_);\
    (state_)->start = (start_);\
    (state_)->end = (end_);\
  }while(0)

void
compensate_local(struct State *state, struct Data *data)
{
  assert(state)
  double c_start = compensate_const(state, data);
  double c_end = c_start + (state->end - state->start) - OVERHEAD(data);
  /* Compensate link overhead */
  if (state_is_send(state))
    c_end -= OVERHEAD(data);
  UPDATE_STATE_TS(state, c_start, c_end, data->timestamps);
  state_print(state);
}

void
compensate_recv(struct State *recv, struct Data *data)
{
  assert(recv && recv->comm && recv->comm->match && recv->comm->c_match);
  struct State *send = recv->comm->match;
  struct State *c_send = recv->comm->c_match;
  double c_recv_start = compensate_const(recv, data);
  double c_recv_end;
  double cpytime = data->copytime->estimator(data->copytime, recv->comm->bytes);
  double comm = recv->end - send->start;
  if (recv->start < send->end) {
    if (c_send->start + comm > c_start)
      c_recv_end = c_send->start + comm;
    else
      c_recv_end = c_start + cpytime;
  } else {
    double comm_upper = comm;
    double comm_lower = 2 * cpytime;
    double comm_min = (c_recv_start - c_send->start) + cpytime;
    double comm_a_lower = comm_min;
    double comm_a_upper = comm_min > comm_upper ? comm_min : comm_upper;
    /* Currently using mean of upper and lower bound */
    c_recv_end = c_send->start + (comm_a_lower + comm_a_upper) / 2.0;
  }
  /* Compensate link overhead */
  c_recv_end -= OVERHEAD(data);
  UPDATE_STATE_TS(recv, c_start, c_recv_end, data->timestamps);
  state_print(recv);
  state_print_c_recv(recv);
}

void
compensate_ssend(struct State *recv, struct Data *data)
{
  assert(recv && recv->comm && recv->comm->match && recv->comm->c_match);
  struct State *send = recv->comm->match;
  struct State *c_send = recv->comm->c_match;
  double c_recv_start = compensate_const(recv, data);
  double c_send_start = compensate_const(c_send, data);
  /* (link overhead) */
  c_send_start -= OVERHEAD(data);
  double comm = send->end - (recv->start > send->start ? recv->start :
      send->start);
  double end;
  if (c_recv_start > c_send_start)
    end = c_recv_start + comm;
  else
    end = c_send_start + comm;
  // FIXME Link overhead on the recv after completion
  UPDATE_STATE_TS(recv, c_recv_start, end, data->timestamps);
  UPDATE_STATE_TS(c_send, c_send_start, end, data->timestamps);
  state_print(recv->comm->c_match);
  state_print(recv);
  state_print_c_recv(recv);
}
