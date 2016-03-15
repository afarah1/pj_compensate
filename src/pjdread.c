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

/* DRY */
#define GROW_QUEUES()\
  do {\
    *link_qs = realloc(*link_qs, rank * sizeof(**link_qs));\
    *send_qs = realloc(*send_qs, rank * sizeof(**send_qs));\
    *recv_qs = realloc(*recv_qs, rank * sizeof(**recv_qs));\
    for (size_t i_ = *ranks; i_ < rank; i_++) {\
      (*link_qs)[i_] = NULL;\
      (*send_qs)[i_] = NULL;\
      (*recv_qs)[i_] = NULL;\
    }\
    *ranks = rank;\
  }while(0)

/*
 * Fills in the states queue with the events from the file, fill the link
 * queues accordingly (updates the amount of queues in ranks). Returns a ptr
 * to the reallocated link_qs (must have been dyn allocd). Aborts on failure.
 */
static void
read_events(char const *filename, size_t *ranks, struct State_q **state_q,
    struct Link_q ***link_qs, struct State_q ***send_qs, struct State_q
    ***recv_qs)
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
        size_t rank = (size_t)(link->to + 1);
        if (rank > *ranks)
          GROW_QUEUES();
        link_q_push_ref((*link_qs) + link->to, link);
        /* Toss away our local reference */
        ref_dec(&(link->ref));
      }
    } else {
      size_t rank = (size_t)(state->rank + 1);
      if (rank > *ranks)
        GROW_QUEUES();
      state_q_push_ref(state_q, state);
      if (state_is_send(state))
        state_q_push_ref((*send_qs) + state->rank, state);
      else if (state_is_recv(state))
        state_q_push_ref((*recv_qs) + state->rank, state);
      ref_dec(&(state->ref));
    }
    free(state_line);
    free(link_line);
    free(etc_line);
    line = gl(f);
  } while (line);
  fclose(f);
}
