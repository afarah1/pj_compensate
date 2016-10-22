/* Argument parsing */
#pragma once

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

static char doc[] = "Outputs a trace compensating for Aky's intrusion";
static char args_doc[] = "ORIGINAL-TRACE COPYTIME-DATA OVERHEAD-DATA SYNC-BYTES";
static struct argp_option options[] = {
  {"estimator", 'e', "ESTIMATOR", 0, "Either 'mean' (default) or 'histogram'", 0},
  {"trimming", 't', "FACTOR", 0, "Trim outliers by FACTOR (default 0.1)", 0},
  {"version", 'v', 0, OPTION_ARG_OPTIONAL, "Print version", 0},
  { 0 }
};

#define NUM_ARGS 4

struct arguments {
  char *input[NUM_ARGS];
  float trimming;
  char *estimator;
};

#define ASSERTSTRTO(nptr, endptr)\
  do {\
    if (errno || (endptr) == (nptr)) {\
      fprintf(stderr, "Invalid argument: %s. Error: %s.\n", (nptr), errno ?\
          strerror(errno) : "No digits were found");\
      exit(EXIT_FAILURE);\
    }\
  } while(0)

/* state should be zerod and errno should be zero */
static error_t
parse_options(int key, char *arg, struct argp_state *state)
{
  struct arguments *args = state->input;
  char *endptr;
  switch (key) {
    case 'e':
      if (strcmp(arg, "mean") && strcmp(arg, "histogram")) {
        fprintf(stderr, "Invalid estimator.\n");
        argp_usage(state);
        exit(EXIT_FAILURE);
      }
      /* Is freed later in main */
      args->estimator = strdup(arg);
      if (!args->estimator) {
        perror("strdup");
        exit(EXIT_FAILURE);
      }
      break;
    case 't':
      args->trimming = strtof(arg, &endptr);
      ASSERTSTRTO(arg, endptr);
      if (args->trimming < 0 || args->trimming >= 1) {
        fprintf(stderr, "Invalid trimming.\n");
        argp_usage(state);
        if (args->estimator)
          free(args->estimator);
        exit(EXIT_FAILURE);
      }
      break;
    case 'v':
      printf("%s\n", VERSION);
      exit(EXIT_SUCCESS);
    case ARGP_KEY_ARG:
      /* Too many arguments. */
      if (state->arg_num == NUM_ARGS)
        argp_usage(state);
      args->input[state->arg_num] = arg;
      break;
    case ARGP_KEY_END:
      /* Not enough arguments. */
      if (state->arg_num < NUM_ARGS)
        argp_usage(state);
      break;
    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = { options, parse_options, args_doc, doc, 0, 0, 0 };
