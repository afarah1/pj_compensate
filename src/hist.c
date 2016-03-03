#include "hist.h"
#include <stddef.h>
#include <gsl/gsl_statistics_double.h>
#include <gsl/gsl_histogram.h>
#include <math.h>
#include <assert.h>

/* Returns the number of bins for a hist. arr should be trimmed and sorted */
static inline size_t
hist_bins(double *arr, size_t n)
{
  /* See https://en.wikipedia.org/wiki/Histogram#Number_of_bins_and_width */
  double h = (3.5 * gsl_stats_sd(arr, 1, n)) / pow((double)n, 1.0/3.0);
  return (size_t)ceil((arr[n - 1] - arr[0]) / h);
}

/* Used for qsort */
static int
cmp(const void *a, const void *b)
{
  double A = *((double *)a);
  double B = *((double *)b);
  return (A > B) - (A < B);
}

gsl_histogram_pdf *
hist_pdf(double *arr, size_t n, float trimming_factor)
{
  assert(arr);
  /* Trim measurements */
  qsort(arr, n, sizeof(*arr), cmp);
  size_t half = trim(n, trimming_factor);
  size_t m = n - 2 * half;
  arr += half;
  size_t k = hist_bins(arr, m);
  /* gsl aborts on failure for the following functions */
  gsl_histogram *hist = gsl_histogram_alloc(k);
  gsl_histogram_set_ranges_uniform(hist, arr[0], arr[m - 1]);
  /*
   * gsl sets the histogram such that
   *
   *    [ bin[0] )[ bin[1] )[ bin[2] )[ bin[3] )[ bin[4] )
   * ---|---------|---------|---------|---------|---------|---  x
   *  r[0]      r[1]      r[2]      r[3]      r[4]      r[5]
   *
   * Whereas the increment function only increases the count for a bin if
   * the value is < the bin's upper bound. I.e. we can't add the last (
   * greatest) measurements to the histogram withthe increment function
   * (they might be == r[5]). This can eaisly be countered by increasing
   * the max bin count manually. On the other hand if there is only one bin
   * all measurements are added to it manually.
   */
  if (k == 1)
    hist->bin[0] = (double)m;
  else
    for (size_t j = 0; j < m; ++j)
      if (gsl_histogram_increment(hist, arr[j])) {
        assert(arr[j] == gsl_histogram_max(hist));
        hist->bin[gsl_histogram_bins(hist) - 1] += 1;
      }
  gsl_histogram_pdf *ans = gsl_histogram_pdf_alloc(k);
  gsl_histogram_pdf_init(ans, hist);
  gsl_histogram_free(hist);
  arr -= half;
  return ans;
}
