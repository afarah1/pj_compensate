/* See the header file for contracts and more docs */
// TODO end size_t/int disparity
/* For logging */
#define _POSIX_C_SOURCE 200112L
#include "reader.h"
#include "logging.h"
#include "hist.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "prng.h"
#include <gsl/gsl_statistics_double.h>
#include "uthash.h"

static double
overhead_hist(void const *pdf)
{
  return gsl_histogram_pdf_sample((gsl_histogram_pdf *)pdf, rnd());
}

static double
overhead_mean(void const *mean)
{
  return *((double *)mean);
}

struct Overhead *
overhead_read(char const *filename, char *method, float trimming_factor)
{
  if (!method)
    LOG_AND_EXIT("NULL method passed to overhead_read.\n");
  /* Read data from file */
  FILE *f = fopen(filename, "r");
  if (!f)
    LOG_AND_EXIT("Could not open %s: %s\n", filename, strerror(errno));
  struct Overhead *ans = malloc(sizeof(*ans));
  if (!ans)
    REPORT_AND_EXIT();
  if (fscanf(f, "%zu", &(ans->n_measurements)) != 1)
    LOG_AND_EXIT("Reading %s header: %s\n", filename, strerror(ferror(f)));
  double *data = malloc((size_t)(ans->n_measurements) * sizeof(*data));
  if (!data)
    REPORT_AND_EXIT();
  int rc = fscanf(f, "%lf", data);
  size_t i = 1;
  while (rc == 1 && i < ans->n_measurements) {
    rc = fscanf(f, "%lf", data + i);
    i++;
  }
  if (rc != 1)
    LOG_AND_EXIT("Reading %"PRIu64"th value from %s: %s\n", i, filename,
        rc == EOF ? "unexpected EOF" : strerror(ferror(f)));
  fclose(f);
  /* Generate estimators + cleanup */
  if (!strcmp(method, "histogram")) {
    ans->estimator = overhead_hist;
    /* gsl asserts success */
    ans->data = (void *)hist_pdf(data, ans->n_measurements, trimming_factor);
  } else {
    ans->estimator = overhead_mean;
    ans->data = malloc(sizeof(double));
    if (!ans->data)
      REPORT_AND_EXIT();
    size_t half = trim(ans->n_measurements, trimming_factor);
    double mean = gsl_stats_mean(data + half, 1, ans->n_measurements - 2 * half);
    if (mean <= 0)
      LOG_ERROR("Overhead <= 0. Frequency too high?\n");
    memcpy(ans->data, &mean, sizeof(double));
  }
  free(data);
  return ans;
}

void
overhead_del(struct Overhead *o)
{
  if (!o)
    return;
  assert(o->estimator && o->data);
  if (o->estimator == overhead_mean)
    free((double *)(o->data));
  else if (o->estimator == overhead_hist)
    gsl_histogram_pdf_free((gsl_histogram_pdf *)(o->data));
  free(o);
}

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
