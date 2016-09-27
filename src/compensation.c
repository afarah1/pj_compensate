/* See the header file for contracts and more docs */
/* for logging.h */
#define _POSIX_C_SOURCE 200112L
#include "compensation.h"
#include "reader.h"
#include "events.h"
#include <assert.h>
#include "logging.h"
#include <stdbool.h>

/*
 * Note: None of the functions defined in this file makes any assertion about
 * Data (a NULL pointer will result in segfaults).
 */

/* Wrapper */
static inline double
overhead(struct Data const *data)
{
  return data->overhead->estimator(data->overhead->data);
}

static inline double
copytime(struct Data const *data, int bytes)
{
  struct Copytime *tmp = NULL;
  HASH_FIND_INT(data->copytime, &bytes, tmp);
  if (!tmp)
    LOG_AND_EXIT("%d not found in copytime table\n", bytes);
  return tmp->mean;
}

/*
 * Return the compensated timestamp for the local event 'state', without
 * altering it or the timestamp register (data struct). Assumes both have
 * been properly initialized.
 */
static inline double
compensate_const(struct State const *state, struct Data const *data)
{
  return data->timestamps.c_last[state->rank] + (state->start -
      data->timestamps.last[state->rank]) - overhead(data);
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
  assert(state);
  double c_start = compensate_const(state, data);
  double c_end = c_start + (state->end - state->start) - overhead(data);
  /* Compensate link overhead */
  if (state_is_send(state))
    c_end -= overhead(data);
  if (c_end <= c_start)
    LOG_ERROR("Overcompensation detected at rank %d. Perhaps the overhead "
        "estimator is incorrect (incorrect frequency?).\n", state->rank);
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
  double cpytime = copytime(data, (int)(recv->comm->bytes));
  double comm = recv->end - send->start;
  if (recv->start < send->end) {
    if (c_send->start + comm > c_recv_start)
      c_recv_end = c_send->start + comm;
    else
      c_recv_end = c_recv_start + cpytime;
  } else {
    double comm_upper = comm;
    double comm_lower = 2 * cpytime;
    double comm_min = (c_recv_start - c_send->start) + cpytime;
    double comm_a_lower = comm_min > comm_lower ? comm_min : comm_lower;
    double comm_a_upper = comm_min > comm_upper ? comm_min : comm_upper;
    /* Currently using mean of upper and lower bound */
    c_recv_end = c_send->start + (comm_a_lower + comm_a_upper) / 2.0;
  }
  /* Compensate link overhead */
  c_recv_end -= overhead(data);
  if (c_recv_end <= c_recv_start)
    LOG_ERROR("Overcompensation detected at rank %d. Perhaps the overhead "
        "estimator is incorrect (incorrect frequency?).\n", recv->rank);
  UPDATE_STATE_TS(recv, c_recv_start, c_recv_end, data->timestamps);
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
  c_send_start -= overhead(data);
  double comm = send->end - (recv->start > send->start ? recv->start :
      send->start);
  double end;
  if (c_recv_start > c_send_start)
    end = c_recv_start + comm;
  else
    end = c_send_start + comm;
  // FIXME Link overhead on the recv after completion
  if (end <= c_recv_start)
    LOG_ERROR("Overcompensation detected at rank %d. Perhaps the overhead "
        "estimator is incorrect (incorrect frequency?).\n", recv->rank);
  if (end <= c_send_start)
    LOG_ERROR("Overcompensation detected at rank %d. Perhaps the overhead "
        "estimator is incorrect (incorrect frequency?).\n", send->rank);
  UPDATE_STATE_TS(recv, c_recv_start, end, data->timestamps);
  UPDATE_STATE_TS(c_send, c_send_start, end, data->timestamps);
  state_print(recv->comm->c_match);
  state_print(recv);
  state_print_c_recv(recv);
}

void
compensate_wait(struct State *wait, struct Data *data)
{
  /* wait && wait->comm && (c_recv || c_send) asserted at pj_compensate.c */
  double c_wait_start = compensate_const(wait, data);
  double c_wait_end;
  // TODO don't neglect wait overhead
  if (comm_is_sync(wait->comm, data->sync_bytes)) {
    struct State *c_recv = wait->comm->c_match;
    c_wait_end = c_wait_start > c_recv->end ? c_wait_start : c_recv->end;
  } else {
    struct State *c_send = wait->comm->c_match->comm->c_match;
    double ctime = c_send->end + copytime(data, (int)(wait->comm->bytes));
    /* We need to do this manually (compensate_const is for event->start) */
    c_wait_end = c_wait_start + (wait->end - wait->start) - overhead(data);
    if (c_wait_end <= ctime)
      c_wait_end = ctime;
  }
  /* this never happens
   * if (c_wait_end < c_wait_start)
   *   LOG_ERROR("Overcompensation detected at rank %d. Perhaps the overhead "
   *       "estimator is incorrect (incorrect frequency?).\n", wait->rank);
   */
  UPDATE_STATE_TS(wait, c_wait_start, c_wait_end, data->timestamps);
  state_print(wait);
}
