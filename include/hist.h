/* Formulas for histograms */
#pragma once
#include <stddef.h>
#include <gsl/gsl_histogram.h>

// TODO this should be separate from the hist file
/*
 * Returns the number of elements to be trimmed from head and tail of an array
 * of n elements with trimming factor p. E.g. trim(10, 0.3) will return 1.
 * Since 0.3 * 10 = 3, 2 elements in total will be trimmed so that an equal
 * number is trimmed from head and tail -- 1 from the head and one from the
 * tail.
 */
static inline size_t
trim(size_t n, float p)
{
  size_t num_trimmed = (size_t)((float)n * p);
  return (num_trimmed % 2 ? (num_trimmed - 1) / 2 : num_trimmed / 2);
}

gsl_histogram_pdf *
hist_pdf(double *arr, size_t n, float trimming_factor);
