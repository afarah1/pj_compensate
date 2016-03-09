/* Routines to compensate events timestamps */
#pragma once

#include "reader.h"
#include "events.h"
#include "queue.h"
#include <assert.h>

/* Timestamp information for this trace */
struct Timestamps {
  /* Last (compensated) timestamp for each rank */
  double *last, *clast;
};

struct Data {
  struct Overhead const *overhead;
  struct Copytime const *copytime;
  struct Timestamps timestamps;
};

void
compensate_comm(struct State *recv, struct Data *data);

void
compensate_nocomm(struct State *state, struct Data *data);
