/* See the header file for contracts and more docs */
// TODO end size_t/int disparity
/* For logging */
#define _POSIX_C_SOURCE 200112L
#include "reader.h"
#include "logging.h"
#include "hist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "prng.h"
#include <gsl/gsl_statistics_double.h>

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
    LOG_AND_EXIT("Reading %dth value from %s: %s\n", i, filename,
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

static double
copytime_mean(struct Copytime const *ct, size_t bytes)
{
  if (bytes < (size_t)(ct->minbytes) || bytes > (size_t)(ct->maxbytes))
    LOG_AND_EXIT("msg bytes (%zu) beyond copytime table bounds [%d,%d]\n",
        bytes, ct->minbytes, ct->maxbytes);
  return ct->data[bytes - (size_t)(ct->minbytes)];
}

/* Generate a mean for each byte size */
static double *
copytime_means(double const *data, size_t size, size_t iters)
{
  assert(data);
  size_t bytes = size / iters;
  double *ans = malloc(bytes * sizeof(*ans));
  if (!ans)
    REPORT_AND_EXIT();
  for (size_t i = 0; i < bytes; i++) {
    ans[i] = gsl_stats_mean(data + i * iters, 1, iters);
    if (ans[i] <= 0)
      LOG_WARNING("Copytime for %zu bytes was <= 0\n", i);
  }
  return ans;
}

struct Copytime *
copytime_read(char const *filename)
{
  /* Read data from file */
  FILE *f = fopen(filename, "r");
  if (!f)
    LOG_AND_EXIT("Could not open %s: %s\n", filename, strerror(errno));
  struct Copytime *ans = malloc(sizeof(*ans));
  if (!ans)
    REPORT_AND_EXIT();
  if (fscanf(f, "%d %d %d", &(ans->minbytes), &(ans->maxbytes), &(ans->iters))
      != 3)
    LOG_AND_EXIT("Reading %s header: %s\n", filename, strerror(ferror(f)));
  assert(ans->maxbytes >= ans->minbytes);
  assert(ans->iters > 0);
  int bytes = ans->maxbytes + 1 - ans->minbytes;
  int size = bytes * ans->iters;
  double *data = malloc((size_t)size * sizeof(*data));
  if (!data)
    REPORT_AND_EXIT();
  int byte;
  double tmp;
  int rc = fscanf(f, "%d %lf", &byte, &tmp),
      i = 0;
  while (rc == 2 && i < size) {
    if (byte < ans->minbytes || byte > ans->maxbytes)
      LOG_AND_EXIT("Reading %dth value from %s: Byte outside of "
        "range\n", i, filename);
    int index = (i / bytes) * bytes + (byte - ans->minbytes);
    data[index] = tmp;
    i++;
    rc = fscanf(f, "%d %lf", &byte, &tmp);
  }
  if (i != size)
    LOG_AND_EXIT("Reading %dth value from %s: %s\n", i, filename,
        rc == EOF ? "unexpected EOF" : strerror(ferror(f)));
  fclose(f);
  /* Generate estimator + cleanup */
  ans->data = copytime_means(data, (size_t)size, (size_t)(ans->iters));
  ans->estimator = copytime_mean;
  free(data);
  return ans;
}

void
copytime_del(struct Copytime *ct)
{
  if (!ct)
    return;
  assert(ct->data);
  free(ct->data);
  free(ct);
}
