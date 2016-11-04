/* Routines to read the copytime file */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "uthash.h"

/*
 * The copytime hash table read from a csv file with the first column being the
 * byte count and the second the transfer time (each row being one byte t time)
 */
struct Copytime {
  int bytes;
  double mean;
  UT_hash_handle hh;
};

/*
 * Read the copytime data from filename. Returns 0 on success, -1 on failure,
 * in which case it also sets errno.
 */
int
copytime_read(char const *filename, struct Copytime **head);

void
copytime_del(struct Copytime **head);
