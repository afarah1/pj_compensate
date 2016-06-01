/* Routines to compensate event timestamps */
#pragma once

#include "reader.h"
#include "events.h"
#include "queue.h"
#include <assert.h>

/* Timestamp information for a trace */
struct Timestamps {
  /* Arrays of the last and last compensated timestamp for each rank */
  double * restrict last, * restrict c_last;
};

/* Data particular to a trace file */
struct Data {
  struct Overhead const *overhead;
  struct Copytime const *copytime;
  struct Timestamps timestamps;
  /*
   * Messages > sync_bytes are treated as synchronous. For instance with the SM
   * MCA from OpenMPI 1.6.5, MPI_Send is synchronous if the msg size is > 4096.
   */
  size_t sync_bytes;
};

void
compensate_local(struct State *state, struct Data *data);

/* Non-local states */

void
compensate_recv(struct State *recv, struct Data *data);

void
compensate_ssend(struct State *recv, struct Data *data);
