/* Routines to compensate events timestamps */
#pragma once

#include "reader.h"
#include "events.h"
#include "queue.h"
#include <assert.h>

/* Timestamp information for this trace */
struct Timestamps {
  /* Last (compensated) timestamp for each rank */
  double *last, *c_last;
};

struct Data {
  struct Overhead const *overhead;
  struct Copytime const *copytime;
  struct Timestamps timestamps;
  size_t sync_bytes;
};

void
compensate_local(struct State *state, struct Data *data);

/* Non-local states */

void
compensate_recv(struct State *recv, struct Data *data);

void
compensate_ssend(struct State *recv, struct Data *data);
