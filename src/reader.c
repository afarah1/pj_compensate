/* See the header file for contracts and more docs */
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
overhead_read(char const *filename, int method, float trimming_factor)
{
  /* Read data from file */
  FILE *f = fopen(filename, "r");
  if (!f)
    LOG_AND_EXIT("Could not open %s: %s\n", filename, strerror(errno));
  struct Overhead *ans = malloc(sizeof(*ans));
  if (!ans)
    REPORT_AND_EXIT();
  // TODO currently the sizes are hardcoded in akypuera
  if (fread(&(ans->hostname), 1, 256, f) < 256 ||
      fread(&(ans->n_measurments), sizeof(ans->n_measurments), 1, f) < 1)
    LOG_AND_EXIT("Err reading %s header: %s\n", filename, strerror(ferror(f)));
  double *data = malloc(ans->n_measurments * sizeof(*data));
  if (!data)
    REPORT_AND_EXIT();
  if (fread(data, sizeof(*data), (size_t)(ans->n_measurments), f) <
      ans->n_measurments)
    LOG_AND_EXIT("Err reading %s header: %s\n", filename, strerror(ferror(f)));
  fclose(f);
  /* Generate estimators + cleanup */
  if (method == 0) {
    ans->estimator = overhead_hist;
    /* gsl asserts success */
    ans->data = (void *)hist_pdf(data, ans->n_measurments, trimming_factor);
  } else {
    ans->estimator = overhead_mean;
    ans->data = malloc(sizeof(double));
    if (!ans->data)
      REPORT_AND_EXIT();
    size_t half = trim(ans->n_measurments, trimming_factor);
    double mean = gsl_stats_mean(data + half, 1, ans->n_measurments - 2 * half);
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
  for (size_t i = 0; i < bytes; i++)
    ans[i] = gsl_stats_mean(data + i * iters, 1, iters);
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
  if (fread(&(ans->minbytes), sizeof(ans->minbytes), 1, f) < 1 ||
      fread(&(ans->maxbytes), sizeof(ans->maxbytes), 1, f) < 1 ||
      fread(&(ans->iters), sizeof(ans->iters), 1, f) < 1)
    LOG_AND_EXIT("Err reading %s header: %s\n", filename, strerror(ferror(f)));
  assert(ans->maxbytes > ans->minbytes);
  assert(ans->iters > 0);
  size_t size = (size_t)((ans->maxbytes + 1 - ans->minbytes) *
      ans->iters);
  double *data = malloc(size * sizeof(*data));
  if (!data)
    REPORT_AND_EXIT();
  if (fread(data, sizeof(*data), size, f) < size)
    LOG_AND_EXIT("Corrupt measurements file: %s\n", strerror(ferror(f)));
  fclose(f);
  /* Generate estimator + cleanup */
  ans->data = copytime_means(data, size, (size_t)(ans->iters));
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
