/* Ref counted event structs (States and Links) and associated routines */
#pragma once

#include "ref.h"
#include <stdbool.h>
#include <stddef.h>

struct Link {
  struct ref ref;
  double start, end;
  size_t bytes;
  int from, to;
  char *container;
};

/* Deep copy of non ref counted ptrs (char *, etc), shallow otherwise */
struct Link *
link_cpy(struct Link const *link);

/* Print the link to stdout */
void
link_print(struct Link const *link);

/*
 * Create a link from a line from a pj_dump trace. Returns NULL if the line is
 * not describing a link
 */
struct Link *
link_from_line(char *line);

/*
 * Same as the link routines, for states
 */

struct State {
  struct ref ref;
  double start, end;
  int imbrication, rank;
  char *routine;
  /* Used by Recv only */
  struct Link *link;
};

struct State *
state_from_line(char *line);

struct State *
state_cpy(struct State const *state);

void
state_print(struct State const *state);

/* Set state->link as a reference to link (handles reference counting) */
void
state_set_link_ref(struct State *state, struct Link *link);

bool
state_is_recv(struct State const *state);

bool
state_is_send(struct State const *state);
