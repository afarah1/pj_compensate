/* pseudo ranodm double between 0 and 1, uniformally distributed */
#pragma once

#define MODULUS    2147483647
#define MULTIPLIER 48271
#define DEFAULT    123456789L
static long seed = DEFAULT;

/*
 * Returns a pseudo-random real uniformely distributed in [0, 1]. Based on
 * http://www.cs.wm.edu/~va/software/park/park.html
 */
double
rnd(void)
{
  const long Q = MODULUS / MULTIPLIER;
  const long R = MODULUS % MULTIPLIER;
  long t = MULTIPLIER * (seed % Q) - R * (seed / Q);
  if (t > 0)
    seed = t;
  else
    seed = t + MODULUS;
  return ((double) seed / MODULUS);
}
