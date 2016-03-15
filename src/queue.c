/* strdup */
#define _POSIX_C_SOURCE 200809L
#include "queue.h"
#include <stdlib.h>
#include <assert.h>
#include "logging.h"
#include "ref.h"
#include "events.h"
#include "utlist.h"

void
state_q_push_ref(struct State_q **head, struct State *state)
{
  assert(state);
  struct State_q *node = calloc(1, sizeof(*node));
  if (!node)
    REPORT_AND_EXIT();
  node->state = state;
  ref_inc(&(state->ref));
  /* head can be null, in this case node becomes the head (thus **) */
  DL_APPEND(*head, node);
}

void
state_q_push_cpy(struct State_q **head, struct State *state)
{
  assert(state);
  struct State_q *node = calloc(1, sizeof(*node));
  if (!node)
    REPORT_AND_EXIT();
  node->state = state_cpy(state);
  DL_APPEND(*head, node);
}

void
state_q_pop(struct State_q **head)
{
  struct State_q *old_head = *head;
  DL_DELETE(*head, *head);
  /* (the state) */
  ref_dec(&(old_head->state->ref));
  /* (the node) */
  free(old_head);
}

void
state_q_empty(struct State_q **q)
{
  struct State_q *iter = NULL, *tmp = NULL;
  DL_FOREACH_SAFE(*q, iter, tmp) {
    state_q_pop(q);
  }
}

void
state_q_delete(struct State_q **q, struct State_q *ele)
{
  DL_DELETE(*q, ele);
  /* (the state) */
  assert(ele->state);
  ref_dec(&(ele->state->ref));
  /* (the node) */
  free(ele);
}

void
link_q_push_ref(struct Link_q **head, struct Link *link)
{
  assert(link);
  struct Link_q *node = calloc(1, sizeof(*node));
  if (!node)
    REPORT_AND_EXIT();
  node->link = link;
  ref_inc(&(link->ref));
  DL_APPEND(*head, node);
}

void
link_q_pop(struct Link_q **head)
{
  struct Link_q *old_head = *head;
  DL_DELETE(*head, *head);
  ref_dec(&(old_head->link->ref));
  free(old_head);
}

int
link_q_sort_e(struct Link_q const *a, struct Link_q const *b)
{
  assert(a && b);
  double A = a->link->end;
  double B = b->link->end;
  return (A > B) - (A < B);
}

void
link_q_empty(struct Link_q **q)
{
  struct Link_q *iter = NULL, *tmp = NULL;
  DL_FOREACH_SAFE(*q, iter, tmp) {
    link_q_pop(q);
  }
}
