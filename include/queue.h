/* State and link queue implementations (see also events.h) */
#pragma once

#include "events.h"
#include <stddef.h>
#include <stdbool.h>

/* Implemented as a linked list via utlist.h */
struct State_q {
  struct State *state;
  struct State_q *prev, *next;
};

/* Push a reference to state to the queue, increasing state's ref ct */
void
state_q_push_ref(struct State_q **head, struct State *state);

/* (these inline functions are just for readability) */

/* Return the first state in queue, don't increase ref ct */
static inline struct State *
state_q_front(struct State_q const *head)
{
  if (head)
    return head->state;
  return NULL;
}

static inline bool
state_q_is_empty(struct State_q const *head)
{
  return (head == NULL);
}

/* Push a shallow copy of state to the queue */
void
state_q_push_cpy(struct State_q **head, struct State *state);

/* Pop the first state from the queue, decreasing its ref ct */
void
state_q_pop(struct State_q **head);

/* Empty the queue (pop all states) */
void
state_q_empty(struct State_q **q);

/* Delete an arbitrary element from the queue, decreasing its ref ct */
void
state_q_delete(struct State_q **q, struct State_q *ele);

/*
 * These are the same as the state functions, but for links
 */

struct Link_q {
  struct Link *link;
  struct Link_q *prev, *next;
};

void
link_q_push_ref(struct Link_q **head, struct Link *link);

void
link_q_pop(struct Link_q **head);

void
link_q_empty(struct Link_q **q);

/* (by end time) */
int
link_q_sort_e(struct Link_q const *a, struct Link_q const *b);
