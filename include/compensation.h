/* Routines to compensate events timestamps */
#pragma once

#include "reader.h"
#include "events.h"
#include "queue.h"
#include <assert.h>

/* Timestamp information for this trace */
struct ts_info {
  /* Last (compensated) timestamp for each rank */
  double *last, *clast;
};

void
compensate_comm(struct State *recv, struct State *send, struct State *c_send,
    struct Overhead const *overhead, struct Copytime const *copytime, struct
    ts_info *ts);

void
compensate_nocomm(struct State *state, struct Overhead const *o,
    struct ts_info *ts);
