/* See the header file for contracts and more docs */
// TODO end size_t/int disparity
/* For logging */
#define _POSIX_C_SOURCE 200112L
#include "copytime.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "prng.h"
#include "uthash.h"

int
copytime_read(char const *filename, struct Copytime **head)
{
  /* (used after a label) */
  struct Copytime *it = NULL,
                  *tmp = NULL;
  int ans = -1;
  /* Read data from file */
  FILE *f = fopen(filename, "r");
  if (!f)
    goto read_none;
  uint64_t lines = 0,
           bytes = 0;
  int byte;
  double measurement;
  int rc = fscanf(f, "%d %lf", &byte, &measurement);
  while (rc == 2) {
    lines++;
    HASH_FIND_INT(*head, &byte, tmp);
    if (tmp) {
      tmp->mean += measurement;
    } else {
      struct Copytime *e = malloc(sizeof(*e));
      if (!e)
        goto read_ht;
      e->bytes = byte;
      e->mean = measurement;
      HASH_ADD_INT(*head, bytes, e);
      bytes++;
    }
    rc = fscanf(f, "%d %lf", &byte, &measurement);
  }
  if (rc != EOF) {
    LOG_ERROR("%d items at line %"PRIu64" of %s\n", rc, lines, filename);
    goto read_ht;
  } else if (errno) {
    goto read_ht;
  } else if (!bytes) {
    LOG_ERROR("%s: no bytes read\n", filename);
    goto read_ht;
  }
  /* Get the mean */
  for (it = *head; it != NULL; it = it->hh.next)
    it->mean /= (double)bytes;
  ans = 0;
  goto read_fopen;
read_ht:
  HASH_ITER(hh, *head, tmp, it) {
    HASH_DELETE(hh, *head, tmp);
    free(tmp);
  }
read_fopen:
  fclose(f);
read_none:
  return ans;
}

void
copytime_del(struct Copytime **head)
{
  struct Copytime *tmp1 = NULL,
                  *tmp2 = NULL;
  HASH_ITER(hh, *head, tmp1, tmp2) {
    HASH_DEL(*head, tmp1);
    free(tmp1);
  }
}
