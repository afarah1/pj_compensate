/* strdup, logging.h */
#define _POSIX_C_SOURCE 200809L
#include "events.h"
#include "ref.h"
#include "logging.h"
#include <stdbool.h>
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
    LOG_WARNING("Couldn't get rank number from string: %s\n", rank);
  return ans;
}

/* strtok macros for link/state_from_line/new functions */
#define CORRUPT_TRACE()\
  do {\
    LOG_CRITICAL("Corrupted trace file. Is it a pj_dump trace?\n");\
    exit(EXIT_FAILURE);\
  } while(0)
#define SKIPTOKEN()\
  do {\
    if (!strtok(NULL, tok)) {\
      CORRUPT_TRACE();\
    }\
  } while(0)
#define GETTOKEN()\
  do {\
    token = strtok(NULL, tok);\
    if (!token) {\
      CORRUPT_TRACE();\
    }\
  } while(0)
#define ASSERTSTRTO()\
  do {\
    if (errno || endptr == token) {\
      CORRUPT_TRACE();\
    }\
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
  /* Duration, type (PTP) */
  SKIPTOKEN();
  SKIPTOKEN();
  /* From */
  GETTOKEN();
  ans->from = rank2int(token);
  /* To */
  GETTOKEN();
  ans->to = rank2int(token);
  /* send_mark */
  SKIPTOKEN();
  /* bytes */
  GETTOKEN();
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

struct Comm *
comm_new(struct State *match, char const *container, size_t bytes)
{
  assert(match && container);
  struct Comm *ans = malloc(sizeof(*ans));
  if (!ans)
    REPORT_AND_EXIT();
  ans->match = state_cpy(match);
  ans->c_match = match;
  ref_inc(&(match->ref));
  ans->bytes = bytes;
  ans->container = strdup(container);
  if (!ans->container)
    REPORT_AND_EXIT();
  return ans;
}

void
comm_del(struct Comm *comm)
{
  assert(comm);
  if (comm->container)
    free(comm->container);
  if (comm->match)
    ref_dec(&(comm->match->ref));
  if (comm->c_match)
    ref_dec(&(comm->c_match->ref));
  free(comm);
}

bool
comm_compensated(struct Comm const *comm)
{
  assert(comm && comm->match && comm->c_match);
  return (comm->match->start != comm->c_match->start);
}

struct Comm *
comm_cpy(struct Comm const *comm)
{
  assert(comm);
  struct Comm *ans = malloc(sizeof(*ans));
  if (!ans)
    REPORT_AND_EXIT();
  memcpy(ans, comm, sizeof(*ans));
  if (comm->match)
    ref_inc(&(comm->match->ref));
  if (comm->c_match)
    ref_inc(&(comm->c_match->ref));
  if (comm->container) {
    ans->container = strdup(comm->container);
    if (!ans->container)
      REPORT_AND_EXIT();
  }
  return ans;
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
  if (state->comm)
    comm_del(state->comm);
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
    ans->comm = comm_cpy(state->comm);
  ans->ref.count = 1;
  return ans;
}

void
state_print(struct State const *state)
{
  assert(state);
  printf("State, rank%d, STATE, %.15f, %.15f, %.15f, %.15f, %s\n", state->rank,
      state->start, state->end, state->end - state->start,
      (double)(state->imbrication), state->routine);
}

void
state_print_c_recv(struct State const *state)
{
  assert(state->comm && state->comm->c_match);
  printf("Link, %s, LINK, %.15f, %.15f, %.15f, PTP, rank%d, rank%d, 0, %zu\n",
      state->comm->container, state->comm->c_match->start, state->end,
      state->end - state->comm->c_match->start, state->comm->c_match->rank,
      state->rank, state->comm->bytes);
}

bool
state_is_recv(struct State const *state)
{
  assert(state && state->routine);
  return !strcmp(state->routine, "MPI_Recv");
}

bool
state_is_send(struct State const *state)
{
  assert(state && state->routine);
  return (
      !strcmp(state->routine, "MPI_Send")  ||
      !strcmp(state->routine, "MPI_Isend") ||
      !strcmp(state->routine, "MPI_Bsend")
  );
}
