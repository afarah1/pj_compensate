/* Ref counted event structs (States and Links) and associated routines */
#pragma once

#include "ref.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Note on copies of structure fields:
 *
 * Pointers to ref counted structs are copied as is, no new structure is
 * created (i.e. the struct's ref count is increased).
 *
 * Strings are copied with strcpy. There are no pointer to other non ref
 * counted entities.
 */

/* A link, as read from a pj_dump trace */
struct Link {
  struct ref ref;
  uint64_t mark;
  double start, end;
  char *type;
  size_t bytes;
  int from, to;
  char *container;
};

/*
 * Create a link from a line from a pj_dump trace. Returns NULL if the line is
 * not describing a link. Reports other failures, possibly aborting.
 */
struct Link *
link_from_line(char *line);

struct State;

// TODO perhaps it is sufficient to store double * (state->start) instead of
// match (way more overhead on copy and uses way more memory)

/* Represents a communication to avoid Link queues for such a task */
struct Comm {
  struct ref ref;
  /*
   * match: copy of the matching state in the communication
   * cmatch: pointer to the original matching state
   */
  struct State *match,
               *c_match;
  char *container;
  size_t bytes;
};

/* Creates a new comm struct, aborts on failure. */
struct Comm *
comm_new(struct State *match, char const *container, size_t bytes);

/*
 * Returns true if event in comm has been compensated, false otherwise. Aborts
 * on failure.
 */
bool
comm_compensated(struct Comm const *comm);

struct State {
  struct ref ref;
  double start,
         end;
  int imbrication,
      rank;
  char *routine;
  /* Used by comm routines only */
  struct Comm *comm;
  /* Send mark, only really used by the wait */
  uint64_t mark;
};

/*
 * Create a state from a line from a pj_dump trace. Returns NULL if the line is
 * not describing a state. Reports other failures, possibly aborting.
 */
struct State *
state_from_line(char *line);

/* Copy a state struct, aborts on failure. */
struct State *
state_cpy(struct State const *state);

/* Prints a state to stdout pj_dump style. Aborts on failure. */
void
state_print(struct State const *state);

/* Print a compensated recv (as a pj_dump link) */
void
state_print_c_recv(struct State const *state);

/* Returns true if state is MPI_Wait, false otherwise. Aborts on failure. */
bool
state_is_wait(struct State const *state);

/* Returns true if state is a P2P recv, false otherwise. Aborts on failure. */
bool
state_is_recv(struct State const *state);

/* Returns true if state is a P2P send, false otherwise. Aborts on failure. */
bool
state_is_send(struct State const *state);

/* Returns true if comm synchronous, false otherwise. Aborts on failure. */
bool
comm_is_sync(struct Comm const *comm, size_t sync_size);

/* Returns true if state is local, false otherwise. Aborts on failure. */
bool
state_is_local(struct State const *state, size_t sync_size);

/*
 * Retunrs true if the state is a collective 1-to-n communication, false
 * otherwise.  Aborts on failure.
 */
bool
state_is_1tn(struct State const *state);
