/* See the header file for contracts and more docs */
/* strdup, logging.h */
#define _POSIX_C_SOURCE 200809L
#include "events.h"
#include "ref.h"
#include "logging.h"
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

static inline int
rank2int(char const *rank)
{
  int ans = -1;
  int rc = sscanf(rank, "rank%d", &ans);
  if (rc != 1)
    LOG_ERROR("Couldn't get rank number from string: %s\n", rank);
  return ans;
}

/* strtok macros for link/state_from_line/new functions */
#define CORRUPT_TRACE() LOG_AND_EXIT("Corrupt trace. Is it a pj_dump trace? "\
    "Did you call pj_dump with -u?\n")
#define SKIPTOKEN()\
  do {\
    if (!strtok(NULL, tok))\
      CORRUPT_TRACE();\
  } while(0)
#define GETTOKEN()\
  do {\
    token = strtok(NULL, tok);\
    if (!token)\
      CORRUPT_TRACE();\
  } while(0)
#define ASSERTSTRTO()\
  do {\
    if (errno || endptr == token)\
      CORRUPT_TRACE();\
  } while(0)

/*
 * Link routines
 */

static void
link_del(struct ref const *ref)
{
  assert(ref);
  struct Link *link = container_of(ref, struct Link, ref);
  if (link->container)
    free(link->container);
  if (link->type)
    free(link->type);
  free(link);
}

struct Link *
link_from_line(char *line)
{
  if (!line)
    return NULL;
  char tok[] = ", ";
  char *token = strtok(line, tok);
  if (!token || strcmp(token, "Link"))
    return NULL;
  struct Link *ans = malloc(sizeof(*ans));
  if (!ans)
    REPORT_AND_EXIT();
  errno = 0;
  char *endptr;
  /* container */
  GETTOKEN();
  ans->container = strdup(token);
  if (!ans->container)
    REPORT_AND_EXIT();
  /* LINK */
  SKIPTOKEN();
  /* Start time */
  GETTOKEN();
  ans->start = strtod(token, &endptr);
  ASSERTSTRTO();
  /* End time */
  GETTOKEN();
  ans->end = strtod(token, &endptr);
  ASSERTSTRTO();
  /* Duration */
  SKIPTOKEN();
  /* Type */
  GETTOKEN();
  ans->type = strdup(token);
  if (!ans->type)
    REPORT_AND_EXIT();
  /* From */
  GETTOKEN();
  ans->from = rank2int(token);
  /* To */
  GETTOKEN();
  ans->to = rank2int(token);
  /* send_mark */
  GETTOKEN();
  ans->mark = (uint64_t)strtoull(token, &endptr, 10);
  ASSERTSTRTO();
  /* bytes */
  token = strtok(NULL, tok);
  if (!token) {
    LOG_ERROR("Failed to read byte count. Did you call pj_dump with -u?\n");
    ans->bytes = 0;
  } else {
    ans->bytes = (size_t)strtoull(token, &endptr, 10);
    ASSERTSTRTO();
  }
  ans->ref.count = 1;
  ans->ref.free = link_del;
  return ans;
}

/*
 * Comm routines
 */

static void
comm_del(struct ref const *ref)
{
  assert(ref);
  struct Comm *comm = container_of(ref, struct Comm, ref);
  if (comm->container)
    free(comm->container);
  /*
   * These checks avoid some (not all) issues with circular references, which
   * should not happen.
   */
  if (comm->match && comm->match->ref.count)
    ref_dec(&(comm->match->ref));
  else if (comm->match)
    LOG_WARNING("Attempted to ref_dec state with ref.ct == 0\n");
  free(comm);
}

struct Comm *
comm_new(struct State *match, char const *container, size_t bytes)
{
  struct Comm *ans = calloc(1, sizeof(*ans));
  if (!ans)
    REPORT_AND_EXIT();
  if (match) {
    ans->match_original_start = match->start;
    ans->match_original_end = match->end;
    ans->match = match;
    ref_inc(&(ans->match->ref));
  }
  ans->bytes = bytes;
  if (container) {
    ans->container = strdup(container);
    if (!ans->container)
      REPORT_AND_EXIT();
  }
  ans->ref.free = comm_del;
  ans->ref.count = 1;
  return ans;
}

bool
comm_compensated(struct Comm const *comm)
{
  assert(comm && comm->match);
  return (comm->match->start != comm->match_original_start ||
      comm->match->end != comm->match_original_end);
}

/*
 * State routines
 */

static void
state_del(struct ref const *ref)
{
  assert(ref);
  struct State *state = container_of(ref, struct State, ref);
  if (state->routine)
    free(state->routine);
  if (state->comm && state->comm->ref.count)
    ref_dec(&(state->comm->ref));
  else if (state->comm)
    LOG_WARNING("Attempted to ref_dec state with ref.ct == 0\n");
  free(state);
}

struct State *
state_from_line(char *line)
{
  if (!line)
    return NULL;
  char tok[] = ", ";
  char *token = strtok(line, tok);
  if (!token || strcmp(token, "State"))
    return NULL;
  struct State *ans = malloc(sizeof(*ans));
  if (!ans)
    REPORT_AND_EXIT();
  char *endptr;
  errno = 0;
  /* Rank */
  GETTOKEN();
  ans->rank = rank2int(token);
  /* STATE */
  SKIPTOKEN();
  /* Start time */
  GETTOKEN();
  ans->start = strtod(token, &endptr);
  ASSERTSTRTO();
  /* End time */
  GETTOKEN();
  ans->end = strtod(token, &endptr);
  ASSERTSTRTO();
  /* Duration */
  SKIPTOKEN();
  /* Imbrication */
  GETTOKEN();
  ans->imbrication = (int)strtol(token, &endptr, 10);
  ASSERTSTRTO();
  /* Routine name */
  GETTOKEN();
  token[strcspn(token, "\n")] = 0;
  ans->routine = strdup(token);
  if (!ans->routine)
    REPORT_AND_EXIT();
  /* Send mark (only relevant for the wait) */
  token = strtok(NULL, tok);
  if (!token) {
    if (state_is_wait(ans)) {
      LOG_WARNING("No send mark for Wait. Did you use the correct version of "
          "Akypuera? Did you call pj_dump with -u? MPI_Wait is currently "
          "supported only for MPI_Isend (as opposed to waiting MPI_Irecv)\n");
    } else if (state_is_send(ans)) {
      LOG_WARNING("No send mark for Send. Did you use the correct version of "
          "Akypuera? Did you call pj_dump with -u?\n");
    } else if (state_is_1tn(ans)) {
      LOG_DEBUG("1-to-n without mark (expected for the recvs only). Did you "
          "use the correct version of Akypuera? Called pj_dump with -u?\n");
      // FIXME find a better way to distinguish from the send scatter this
      // early on (later on it can be inferred from the comm linkage but the
      // file doing the linkage doesn't know if send/recv either w/o this
      // (the issue is that a send scatter might legitimately have max mark)
      ans->mark = UINT64_MAX;
    }
  } else {
    ans->mark = (uint64_t)strtoull(token, &endptr, 10);
    ASSERTSTRTO();
  }
  ans->ref.count = 1;
  ans->ref.free = state_del;
  ans->comm = NULL;
  return ans;
}

struct State *
state_cpy(struct State const *state)
{
  assert(state);
  struct State *ans = malloc(sizeof(*ans));
  if (!ans)
    REPORT_AND_EXIT();
  memcpy(ans, state, sizeof(*state));
  if (state->routine) {
    ans->routine = strdup(state->routine);
    if (!ans->routine)
      REPORT_AND_EXIT();
  }
  if (state->comm)
    ref_inc(&(state->comm->ref));
  ans->ref.count = 1;
  return ans;
}

void
state_print(struct State const *state)
{
  assert(state);
  if (state_is_send(state) || state_is_recv(state) || state_is_wait(state))
    printf("State, rank%d, STATE, %.15f, %.15f, %.15f, %.15f, %s, %"PRIu64"\n",
      state->rank, state->start, state->end, state->end - state->start,
      (double)(state->imbrication), state->routine, state->mark);
  else
    printf("State, rank%d, STATE, %.15f, %.15f, %.15f, %.15f, %s\n",
        state->rank, state->start, state->end, state->end - state->start,
        (double)(state->imbrication), state->routine);
}

void
state_print_c_recv(struct State const *state)
{
  assert(state->comm && state->comm->match);
  printf("Link, %s, LINK, %.15f, %.15f, %.15f, PTP, rank%d, rank%d, %"PRIu64
      ", %zu\n", state->comm->container, state->comm->match->start,
      state->end, state->end - state->comm->match->start,
      state->comm->match->rank, state->rank, state->mark,
      state->comm->bytes);
}

bool
state_is_recv(struct State const *state)
{
  assert(state && state->routine);
  return !strcmp(state->routine, "MPI_Recv");
}

bool
state_is_wait(struct State const *state)
{
  assert(state && state->routine);
  return !strcmp(state->routine, "MPI_Wait");
}

/* All routines that generate a PTP link by akypuera */
bool
state_is_send(struct State const *state)
{
  assert(state && state->routine);
  return (
      !strcmp(state->routine, "MPI_Send")  ||
      !strcmp(state->routine, "MPI_Ssend") ||
      !strcmp(state->routine, "MPI_Isend") ||
      !strcmp(state->routine, "MPI_Bsend") ||
      !strcmp(state->routine, "MPI_Ibsend") ||
      !strcmp(state->routine, "MPI_Irsend") ||
      !strcmp(state->routine, "MPI_Issend") ||
      !strcmp(state->routine, "MPI_Irsend")
  );
}

bool
comm_is_sync(struct Comm const *comm, size_t sync_size)
{
  assert(comm);
  return (comm->bytes >= sync_size);
}

bool
state_is_local(struct State const *state, size_t sync_size)
{
  if (state_is_recv(state) || state_is_wait(state)) {
    return false;
  } else if (state_is_send(state)) {
    assert(strlen(state->routine) > 5);
    /* Assumes there are enough resources in buffered mode */
    if (state->routine[4] == 'I' || state->routine[4] == 'B')
      return true;
    return ! comm_is_sync(state->comm, sync_size);
  } else {
    return true;
  }
}

bool
state_is_1tn(struct State const *state)
{
  assert(state && state->routine);
  return !(strcmp(state->routine, "MPI_Scatter"));
}
