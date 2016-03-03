/* Functions to read pj_dump files (for internal use) */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "logging.h"
#include "ref.h"
#include "events.h"
#include "queue.h"

static char *
gl(FILE *f)
{
  char *buff = NULL;
  size_t size = 0;
  ssize_t rc = getline(&buff, &size, f);
  if (rc == -1) {
    if (buff)
      free(buff);
    return NULL;
  }
  return buff;
}

static inline bool
grow_ranks(size_t *ranks, int rank)
{
  if ((size_t)rank + 1 > *ranks) {
    *ranks = (size_t)rank + 1;
    return true;
  }
  return false;
}

#define GROW(queues_, ranks_, rank_)\
  do {\
    size_t old_rank_ = *(ranks_);\
    if (grow_ranks((ranks_), (rank_))) {\
      queues_ = realloc((queues_), *(ranks_) * sizeof(*(queues_)));\
      if (!(queues_))\
        REPORT_AND_EXIT();\
      for (size_t i_ = old_rank_; i_ < *(ranks_); i_++) {\
        (queues_)[i_] = NULL;\
      }\
    }\
  }while(0)

/*
 * Fills in the states queue with the events from the file, fill the link
 * queues accordingly (updates the amount of queues in ranks). Returns a ptr
 * to the reallocated link_qs (must have been dyn allocd). Aborts on failure.
 */
static struct Link_q **
read_events(char const *filename, size_t *ranks, struct State_q **state_q,
    struct Link_q **link_qs)
{
  FILE *f = fopen(filename, "r");
  if (!f)
    LOG_AND_EXIT("Could not open %s: %s\n", filename, strerror(errno));
  char *line = gl(f);
  if (!line)
    REPORT_AND_EXIT();
  do {
    /* strtok shenanigans */
    char *state_line = strdup(line);
    char *link_line = strdup(line);
    char *etc_line = strdup(line);
    free(line);
    if (!state_line || !link_line || !etc_line)
      REPORT_AND_EXIT();
    struct State *state = state_from_line(state_line);
    if (!state) {
      struct Link *link = link_from_line(link_line);
      if (!link) {
        LOG_DEBUG("Line is not a State nor a Link\n");//: %s", etc_line);
        printf("%s", etc_line);
      } else {
        GROW(link_qs, ranks, link->to);
        link_q_push_ref(link_qs + link->to, link);
        /* Toss away our local reference */
        ref_dec(&(link->ref));
      }
    } else {
      GROW(link_qs, ranks, state->rank);
      state_q_push_ref(state_q, state);
      ref_dec(&(state->ref));
    }
    free(state_line);
    free(link_line);
    free(etc_line);
    line = gl(f);
  } while (line);
  fclose(f);
  return link_qs;
}
