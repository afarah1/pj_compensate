/* Routines to read binaries generated by Aky and structs to store the data */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "uthash.h"

/*
 * The copytime hash table
 */
struct Copytime {
  int bytes;
  double mean;
  UT_hash_handle hh;
};

/*
 * Read the copytime data from filename and summarize it (get the mean).
 * Aborts on failure.
 */
void
copytime_read(char const *filename, struct Copytime **head);

void
copytime_del(struct Copytime **head);
