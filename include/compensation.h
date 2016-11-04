/* Routines to compensate event timestamps */
#pragma once

#include "reader.h"
#include "events.h"
#include "queue.h"
#include <assert.h>

/* Singleton. Carries timestamp info for the loaded trace. */
struct Timestamps {
  /* Timestamp of the last event visited in each rank  */
  double *last;
  /* Compensated timestamp of the last event visited in each rank  */
  double *c_last;
};

/* Singleton. Carries data about the loaded trace. */
struct Data {
  /* Mean overhead */
  double overhead;
  /* Per byte msg copy time. See reader.h */
  struct Copytime const *copytime;
  /* See above */
  struct Timestamps timestamps;
  /*
   * Messages > sync_bytes are treated as synchronous. For instance with the SM
   * BTL from OpenMPI 1.6.5, MPI_Send is synchronous if the msg + header size
   * is > 4096.
   */
  size_t sync_bytes;
};

/*
 * Compensates a local event. Alters state and data timestamp information with
 * the compensated timestamp. Assumes both state and data have been properly
 * initialized.
 */
void
compensate_local(struct State *state, struct Data *data);

/*
 * Compensates a non-local recv event. Alters state and data timestamp
 * information with the compensated timestamp. Assumes both state and data have
 * been properly initialized. Use an lower bound? If not, use an upper one.
 */
void
compensate_recv(struct State *recv, struct Data *data, bool lower);

/* Same as compensate_recv, but for a gather recv */
void
compensate_grecv(struct State *recv, size_t i, struct Data *data, bool lower);

/* Generic compensation routine for recv, see the wrappers above. */
void
compensate_recv_(struct State *recv, struct State *c_send, double send_start,
    double send_end, struct Data *data, bool lower);

/*
 * Compensates a non-local (synchronous) send event. Alters state and data
 * timestamp information with the compensated timestamp. Assumes both state and
 * data have been properly initialized.
 */
void
compensate_ssend(struct State *recv, struct Data *data);

/* Same as compensate_ssend, but for a gather ssend */
void
compensate_gssend(struct State *grecv, size_t i, struct Data *data);

/* Generic compensation function for recv, see the wrappers above. */
void
compensate_ssend_(struct State *recv, struct State *c_send, double send_start,
    double send_end, struct Data *data);

/*
 * Compensates a non-local Wait event. Alters state and data timestamp
 * information with the compensated timestamp. Assumes both state and data have
 * been properly initialized.
 */
void
compensate_wait(struct State *wait, struct Data *data);
