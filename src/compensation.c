/* See the header file for contracts and more docs */
/* for logging.h */
#define _POSIX_C_SOURCE 200112L
#include "compensation.h"
#include "reader.h"
#include "events.h"
#include <assert.h>
#include "logging.h"
#include <stdbool.h>

/* All functions assume `struct Data *data` is a valid pointer */

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
      data->timestamps.last[state->rank]) - data->overhead;
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
  double c_start = compensate_const(state, data),
         c_end   = c_start + (state->end - state->start) - data->overhead;
  /* Compensate link overhead */
  if (state_is_send(state))
    c_end -= data->overhead;
  if (c_end <= c_start)
    LOG_ERROR("Overcompensation detected at rank %d, %s. Perhaps the overhead "
        "estimator is incorrect (incorrect frequency?).\n", state->rank,
        state->routine);
  UPDATE_STATE_TS(state, c_start, c_end, data->timestamps);
  state_print(state);
}

void
compensate_recv(struct State *recv, struct Data *data, bool lower)
{
  assert(recv && recv->comm && recv->comm->match);
  struct State *c_send = recv->comm->match;
  double send_start   = recv->comm->match_original_start,
         send_end     = recv->comm->match_original_end,
         c_recv_start = compensate_const(recv, data),
         c_recv_end,  /* Value being calculated */
         cpytime      = copytime(data, (int)(recv->comm->bytes)),
         comm         = recv->end - send_start;
  /* Communication time can be measured */
  if (recv->start < send_end) {
    /* The recv in the comp. trace had to wait data to be transfered to it */
    if (c_send->start + comm > c_recv_start)
      c_recv_end = c_send->start + comm;
    /* All the recv in the c. trace had to do was copy data between buffers */
    else
      c_recv_end = c_recv_start + cpytime;
  /* We have to take an approximated communication time */
  } else {
    double comm_upper   = comm,
           comm_lower   = 2 * cpytime,
           comm_min     = (c_recv_start - c_send->start) + cpytime,
           comm_a_lower = comm_min > comm_lower ? comm_min : comm_lower,
           comm_a_upper = comm_min > comm_upper ? comm_min : comm_upper;
    if (lower)
      c_recv_end = c_send->start + comm_a_lower;
    else
      c_recv_end = c_send->start + comm_a_upper;
  }
  // TODO can we keep the tool tracer-independent?
  /* Compensate link creation overhead (Akypuera only) */
  c_recv_end -= data->overhead;
  if (c_recv_end <= c_recv_start)
    LOG_ERROR("Overcompensation detected at rank %d, %s. Perhaps the overhead "
        "estimator is incorrect (incorrect frequency?).\n", recv->rank,
        recv->routine);
  UPDATE_STATE_TS(recv, c_recv_start, c_recv_end, data->timestamps);
  state_print(recv);
  state_print_c_recv(recv);
}

void
compensate_ssend(struct State *recv, struct Data *data)
{
  assert(recv && recv->comm && recv->comm->match);
  struct State *c_send = recv->comm->match;
  double send_start   = recv->comm->match_original_start,
         send_end     = recv->comm->match_original_end,
         c_recv_start = compensate_const(recv, data),
         c_send_start = compensate_const(c_send, data);
  /* (link overhead) */
  c_send_start -= data->overhead;
  /* Data only starts being sent once the recv is posted */
  double comm = send_end - (recv->start > send_start ? recv->start :
      send_start);
  double c_send_end; /* Value being calculated */
  /* The send in the c. trace had to wait the recv */
  if (c_recv_start > c_send_start)
    c_send_end = c_recv_start + comm;
  /* All the send in the c. trace had to do was exchange the data */
  else
    c_send_end = c_send_start + comm;
  // FIXME Link overhead on the recv after completion
  if (c_send_end <= c_recv_start)
    LOG_ERROR("Overcompensation detected at rank %d, %s. Perhaps the overhead "
        "estimator is incorrect (incorrect frequency?).\n", recv->rank,
        recv->routine);
  if (c_send_end <= c_send_start)
    LOG_ERROR("Overcompensation detected at rank %d, %s. Perhaps the overhead "
        "estimator is incorrect (incorrect frequency?).\n", c_send->rank,
        c_send->routine);
  /* We assume recv.end ~= send.end */
  UPDATE_STATE_TS(recv, c_recv_start, c_send_end, data->timestamps);
  UPDATE_STATE_TS(c_send, c_send_start, c_send_end, data->timestamps);
  state_print(recv->comm->match);
  state_print(recv);
  state_print_c_recv(recv);
}

void
compensate_wait(struct State *wait, struct Data *data)
{
  /* wait && wait->comm && (c_recv || c_send) asserted at pj_compensate.c */
  double c_wait_start = compensate_const(wait, data),
         c_wait_end; /* Value being calculated */
  // FIXME don't neglect wait overhead
  // TODO sync verification should be done @ pj_compensate
  if (comm_is_sync(wait->comm, data->sync_bytes)) {
    struct State *c_recv = wait->comm->match;
    c_wait_end = c_wait_start > c_recv->end ? c_wait_start : c_recv->end;
  } else {
    struct State *c_send = wait->comm->match->comm->match;
    double ctime = c_send->end + copytime(data, (int)(wait->comm->bytes));
    /* We need to do this manually (compensate_const is for event->start) */
    c_wait_end = c_wait_start + (wait->end - wait->start) - data->overhead;
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
