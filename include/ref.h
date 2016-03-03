/*
 * A simple reference counting data structure for embedding, for internal use.
 * Inspired by http://nullprogram.com/blog/2015/02/17/
 */

#pragma once

#include <stddef.h>

/*
 * From the kernel. More details at:
 * http://radek.io/2012/11/10/magical-container_of-macro/
 *
 * Modified to be ISO C complient. See:
 * http://stackoverflow.com/a/10269766
 *
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:  the pointer to the member.
 * @type: the type of the container struct this is embedded in.
 * @member: the name of the member within the struct.
 */
#define container_of(ptr, type, member) \
  ((type *)((char *)(ptr) - offsetof(type, member)))

struct ref {
  void (*free)(struct ref const *ref);
  size_t count;
};

/* Assumes the counting starts at one, otherwise bugs shall arise. */
static inline void
ref_dec(struct ref const *ref)
{
  if (--(((struct ref *)ref)->count) == 0)
    ref->free(ref);
}

static inline void
ref_inc(struct ref const *ref)
{
  ((struct ref *)ref)->count++;
}
