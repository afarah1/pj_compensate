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
  /* Overhead estimator. See reader.h */
  struct Overhead const *overhead;
  /* Per byte msg copy time. See reader.h */
  struct Copytime const *copytime;
  /* See above */
  struct Timestamps timestamps;
  /*
   * Messages > sync_bytes are treated as synchronous. For instance with the SM
   * MCA from OpenMPI 1.6.5, MPI_Send is synchronous if the msg size is > 4096.
   */
  size_t sync_bytes;
};

/*
 * Compensates a local event (see glossary). Alters state and data timestamp
 * information with the compensated timestamp. Assumes both state and data
 * have been properly initialized.
 */
void
compensate_local(struct State *state, struct Data *data);

/*
 * Compensates a non-local recv event (see glossary). Alters state and data
 * timestamp information with the compensated timestamp. Assumes both state
 * and data have been properly initialized.
 */
void
compensate_recv(struct State *recv, struct Data *data);

/*
 * Compensates a non-local (synchronous) send event (see glossary). Alters
 * state and data timestamp information with the compensated timestamp. Assumes
 * both state and data have been properly initialized.
 */
void
compensate_ssend(struct State *recv, struct Data *data);

/*
 * Compensates a non-local Wait event (see glossary). Alters state and data
 * timestamp information with the compensated timestamp. Assumes both state and
 * data have been properly initialized.
 */
void
compensate_wait(struct State *wait, struct Data *data);
