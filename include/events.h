/* Ref counted event structs (States and Links) and associated routines */
#pragma once

#include "ref.h"
#include <stdbool.h>
#include <stddef.h>

/* A link, as read from a pj_dump trace */
struct Link {
  struct ref ref;
  double start, end;
  size_t bytes;
  int from, to;
  char *container;
};

/*
 * Create a link from a line from a pj_dump trace. Returns NULL if the line is
 * not describing a link
 */
struct Link *
link_from_line(char *line);

struct State;

/*
 * Represents a communication to avoid Link queues for such a task. match holds
 * a copy of the matching state in the communication, c_match holds a pointer
 * to the original matching state, so we know when it's compensated ; strings
 * are copied and ref cts inc, as per usual
 */
struct Comm {
  struct State *match, *c_match;
  char *container;
  size_t bytes;
};

struct Comm *
comm_new(struct State *match, char const *container, size_t bytes);

void
comm_del(struct Comm *comm);

bool
comm_compensated(struct Comm const *comm);

/* Deep copy of non ref counted ptrs (char *, etc), shallow otherwise */
struct Comm *
comm_cpy(struct Comm const *comm);

struct State {
  struct ref ref;
  double start, end;
  int imbrication, rank;
  char *routine;
  /* Used by comm routines only */
  struct Comm *comm;
};

struct State *
state_from_line(char *line);

/* Deep copy of non ref counted ptrs (char *, etc), shallow otherwise */
struct State *
state_cpy(struct State const *state);

void
state_print(struct State const *state);

/* Print a compensated recv (as a pj_dump link) */
void
state_print_c_recv(struct State const *state);

bool
state_is_recv(struct State const *state);

bool
state_is_send(struct State const *state);

bool
state_is_ssend(struct State const *state, size_t comm_size, size_t sync_size);

bool
state_is_asend(struct State const *state, size_t comm_size, size_t sync_size);
