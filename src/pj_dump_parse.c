/* Read a pj_dump trace file into the event queues */
/*
 * This file has functions to read the events from a trace into some queues,
 * updating some counters. Here is an explanation of why we do this and how we
 * use these queues to link recvs with their matching sends, using info from
 * the registered links.
 *
 * Events we have from the trace file:
 *
 * - State: Either a Send or a Recv, contains start and end time and rank info
 *   - Recv: That's it
 *   - Send: Also has send_mark, indicating it was the nth send in its rank
 * - Link: Has start time ~= send st, end time ~= recv et, from and to (ranks)
 *   info and send_mark of the send
 *
 * State and Link queues into which we read events from the trace file:
 *
 * Every rank has an array of pointers to States, where we store all Sends to
 * later on link to Recvs. Why array of pointers?  Because the structs are ref
 * counted and we only need one allocation if we work with pointers from there
 * on. So what we have is:
 *
 * [ * * * ... ] Outer arr, one inner arr per rank, State ***
 *   ^ [ * * * ...] Inner arr, one element per event in that rank, State **
 *       ^ The event pointer, State *
 *
 * Since we grow the arrs dynamically so the user doesn't have to inform the
 * number of ranks or the number of Sends per rank, we need to use realloc,
 * thus we need to pass &outer_arr, thus having struct *****, which we typedef
 * below for sanity.
 *
 * The growing of the arrays works like this: The outer array always has an
 * exact known size equal to the highest rank found so far. The inner arrays
 * have each their own, independent number of elements, equal to the number of
 * events in that rank so far (it grows with time), and a cap (the actual size
 * allocated), also independent between the ranks. The cap is doubled every
 * time it is reached. Thus we have the following auxiliary arrays:
 *
 * scaps - The array of caps for the send arrs (one cap per rank)
 * slens - The array of #eles for the send arrs (one #ele per rank)
 *
 * Rationale and how we link Recvs and Sends:
 *
 * Why not use a double linked list, since the arrs represent a FIFO queue, one
 * might ask. We actually use this for the recv and link queues, because it's
 * simpler than arrays that grow. We shouldn't use for the send queue because
 * we associate sends with recvs using send_mark, and it'd be O(n) with a DL
 * list:
 *
 * for each rank in ranks:
 *   sort_by_end_time(links[rank])
 *   for each link in links[rank]:
 *     first(recvs[link->to])->comm->match = sends[link->from][link->mark]
 *
 * One might think we could do sends[link->from]++ every time, since it's FIFO,
 * but that's not possible as the sends with a given rank of which the dest is
 * the rank of a recv queue, are not aligned to the recvs in that queue of
 * which the source is the send rank. Graphically:
 *
 * [ SendTo0, SendTo1, SendTo0, SendTo0          ] Rank2
 * [ SendTo0, RecvFr2, SendTo0                   ] Rank1
 * [ RecvFr2, RecvFr2, RecvFr1, RecvFr1, RecvFr2 ] Rank0
 * [ 2t0 2t0 2t1 1t0 1t0 2t0] Links
 * (all sends are asynchronous)
 *
 * How can we link recvs with sends in this case? If we were to use a DL FIFO:
 *
 * The link only has info about the send (send rank and send mark) and the recv
 * rank, but not a recv mark, so we need to align the link queue to the recv
 * queue (which we do by sorting by end time). If we were to ++ on the send
 * queue every time we ++ on the link/recv queue (only way to get the next
 * recv, since the queues are aligned and there is no mark info), we would end
 * up linking:
 *
 * RecvFr2(0) to SendTo0(2) -> Correct
 * RecvFr2(0) to SendTo1(2) -> Incorrect!
 *
 * The alternative is to keep ++ing until send_mark, and then for the next
 * sends -- or ++ until the send_mark as needed, which is slow compared to
 * send_arr[send_mark].
 *
 * That is, assuming we're processing the queues rank by rank using the l/r
 * queues as base. If we were to use the send queues as base we would link:
 *
 * SendTo0(2) to RecvFr2(0) -> Correct
 * SendTo1(2) to RecvFr2(1) -> Correct
 * SendTo0(2) to RecvFr2(0) -> Correct
 * SendTo0(2) to RecvFr1(0) -> Incorrect!
 * (remember we advance in the recv queues by ++, the only way!)
 *
 * And this would be even more complicated. It might be irrelevantly slow for
 * some traces to do the ++-- approach, but for other traces it might make a
 * significant difference. I didn't test it, but I think it's worth to
 * sacrifice a little simplicity in this case. There could be an issue of
 * running out of space because of the doubling cap, but, assuming Linux,
 * that'd probably be over committed and never used.
 *
 * PS: In the past we didn't use send mark and searched for the send using
 * the link start time (which should be contained in the send event, as the
 * link happend after SEND_IN and before SEND_OUT). This is essentially the
 * same as the --++ approach, but without having to register send_mark
 * (8 bytes per event) and some extra tests on the --++ step.
 */

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include "logging.h"
#include "ref.h"
#include "events.h"
#include "queue.h"

/* (see the explanation above) */
typedef struct State *** outter_t;

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

// TODO decompose this
/* Ad-hoc fun to resize the outer arrs/queues of size 'size' to 'new_size' */
static void
grow_outer(size_t *size, size_t new_size, struct Link_q ***links, outter_t
    *sends, struct State_q ***recvs, uint64_t **slens, uint64_t **scaps,
    uint64_t ocap)
{
    *recvs = realloc(*recvs, new_size * sizeof(**recvs));
    *links = realloc(*links, new_size * sizeof(**links));
    *sends = realloc(*sends, new_size * sizeof(**sends));
    *slens = realloc(*slens, new_size * sizeof(**slens));
    *scaps = realloc(*scaps, new_size * sizeof(**scaps));
    if (!*recvs || !*links || !*sends || !*slens || !*scaps)
      REPORT_AND_EXIT();
    for (size_t i = *size; i < new_size; i++) {
      (*scaps)[i] = ocap;
      (*slens)[i] = 0;
      (*sends)[i] = malloc((size_t)ocap * sizeof(*((*sends)[i])));
      (*links)[i] = NULL;
      (*recvs)[i] = NULL;
      if (!((*sends)[i]))
        REPORT_AND_EXIT();
    }
    *size = new_size;
}

// TODO don't need the index I guess, just the correct ptr
// TODO rename stuff
static void
grow_inner(outter_t sends, uint64_t *scaps)
{
  uint64_t new_cap = *scaps * 2;
  *sends = realloc(*sends, (size_t)new_cap * sizeof(**sends));
  if (!*sends)
    REPORT_AND_EXIT();
  *scaps = new_cap;
}

/*
 * Fills the arrays / queues with the states from the trace file and updates
 * counters. Assumes everything passed (except the filename) to be NULL/0.
 */
static void
read_events(char const *filename, size_t *ranks, struct State_q **state_q,
    struct Link_q ***links, outter_t *sends, struct State_q ***recvs, uint64_t
    **slens)
{
  /* Important for some (size_t) conversions from marks registered as uint64 */
  assert(SIZE_MAX <= UINT64_MAX);
  FILE *f = fopen(filename, "r");
  if (!f)
    LOG_AND_EXIT("Could not open %s: %s\n", filename, strerror(errno));
  char *line = gl(f);
  if (!line)
    REPORT_AND_EXIT();
  uint64_t *scaps = NULL;
  uint64_t const ocap = 10;
  /* Initialize all arrs/queues with one rank each */
  grow_outer(ranks, 1, links, sends, recvs, slens, &scaps, ocap);
  do {
    /* strtok shenanigans */
    char *state_line = strdup(line),
         *link_line = strdup(line),
         *etc_line = strdup(line);
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
          grow_outer(ranks, rank, links, sends, recvs, slens, &scaps, ocap);
        /* (grow outer aborts on failure) */
        link_q_push_ref((*links) + link->to, link);
        /* Toss away our local ref obtained on allocation */
        ref_dec(&(link->ref));
      }
    } else {
      size_t rank = (size_t)(state->rank + 1);
      if (rank > *ranks)
        grow_outer(ranks, rank, links, sends, recvs, slens, &scaps, ocap);
      state_q_push_ref(state_q, state);
      if (state_is_send(state)) {
        (*sends)[state->rank][(*slens)[state->rank]] = state;
        ref_inc(&(state->ref));
        if (++((*slens)[state->rank]) >= scaps[state->rank])
          grow_inner((*sends) + state->rank, scaps + state->rank);
      } else if (state_is_recv(state)) {
        state_q_push_ref((*recvs) + state->rank, state);
      } else if (state_is_wait(state)) {
        /*
         * For now we only support MPI_Wait for MPI_Isend (->mark). Thus,
         * MPI_Wait is always in the same rank as the matching MPI_Isend and
         * always comes after it, so we can assume the matching send has
         * already been processed and create a temporary comm in here, to be
         * used later to link the MPI_Wait to the MPI_Recv it is actually
         * waiting for (we assume MPI_Isend was synchronous, albeit
         * instantaneous, and we assert for that).
         */
        if ((*slens)[state->rank] <= state->mark) {
          LOG_CRITICAL("There is no Send for the Wait. Did you call MPI_Wait "
              "without (or before) a matching MPI_Isend? This is not "
              "supported.\n");
          exit(EXIT_FAILURE);
        }
        struct State *send = (*sends)[state->rank][state->mark];
        assert(send->mark == state->mark);
        send->comm = comm_new(state, NULL, 0);
      }
      ref_dec(&(state->ref));
    }
    free(state_line);
    free(link_line);
    free(etc_line);
    line = gl(f);
  } while (line);
  fclose(f);
  free(scaps);
}
