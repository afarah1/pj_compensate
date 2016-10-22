/* See the header file for contracts and more docs */
// TODO end size_t/int disparity
/* For logging */
#define _POSIX_C_SOURCE 200112L
#include "reader.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "prng.h"
#include "uthash.h"

void
copytime_read(char const *filename, struct Copytime **head)
{
  /* Read data from file */
  FILE *f = fopen(filename, "r");
  if (!f)
    LOG_AND_EXIT("Could not open %s: %s\n", filename, strerror(errno));
  uint64_t lines = 0, bytes = 0;
  int byte;
  double measurement;
  int rc = fscanf(f, "%d %lf", &byte, &measurement);
  while (rc == 2) {
    lines++;
    struct Copytime *tmp = NULL;
    HASH_FIND_INT(*head, &byte, tmp);
    if (tmp) {
      tmp->mean += measurement;
    } else {
      struct Copytime *e = malloc(sizeof(*e));
      e->bytes = byte;
      e->mean = measurement;
      HASH_ADD_INT(*head, bytes, e);
      bytes++;
    }
    rc = fscanf(f, "%d %lf", &byte, &measurement);
  }
  if (rc != EOF)
    LOG_AND_EXIT("%d items at line %"PRIu64" of %s\n", rc, lines, filename);
  else if (errno)
    LOG_AND_EXIT("Reading %s: %s\n", filename, strerror(errno));
  else if (!bytes)
    LOG_AND_EXIT("%s: no bytes read\n", filename);
  fclose(f);
  /* Get the mean */
  for (struct Copytime *it = *head; it != NULL; it = it->hh.next)
    it->mean /= (double)bytes;
}

void
copytime_del(struct Copytime **head)
{
  struct Copytime *tmp1, *tmp2;
  HASH_ITER(hh, *head, tmp1, tmp2) {
    HASH_DEL(*head, tmp1);
    free(tmp1);
  }
}
