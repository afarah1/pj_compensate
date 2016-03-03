/* Argument parsing */
#pragma once

#include <argp.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

enum Estimator {
  MEAN = 0,
  HISTOGRAM = 1
};

static char doc[] = "Outputs a trace compensating for Aky's intrusion";
static char args_doc[] = "ORIGINAL-TRACE COPYTIME-DATA RSTEVENT-DATA";
static struct argp_option options[] = {
  {"estimator", 'o', "O", 0, "Overhead estimator: 0 (mean) or 1 (histogram,"
    "default)", 0},
  {"trimming", 't', "FACTOR", 0, "Trim data by FACTOR (default 0.1)", 0},
  {"start", 's', "TIME", 0, "Compensation starts at START (instead of 0)", 0},
  {"end", 'e', "TIME", 0, "Compensation ends at END (instead of EOF)", 0},
  //TODO {"version", 'v', 0, OPTION_ARG_OPTIONAL, "Print version", 0},
  { 0 }
};

#define VALIDATE_INPUT_SIZE 3

struct arguments {
  char *input[VALIDATE_INPUT_SIZE];
  double start, end;
  float trimming;
  enum Estimator estimator;
  int input_size;
  //int quiet;
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
    case 's':
      args->start = strtod(arg, &endptr);
      ASSERTSTRTO(arg, endptr);
      break;
    case 'e':
      args->end = strtod(arg, &endptr);
      ASSERTSTRTO(arg, endptr);
      break;
    case 't':
      args->trimming = strtof(arg, &endptr);
      ASSERTSTRTO(arg, endptr);
      break;
    case 'o':
      /* Is verified later in main */
      args->estimator = (enum Estimator)strtol(arg, &endptr, 10);
      ASSERTSTRTO(arg, endptr);
      break;
    //TODO case 'v': printf("%s\n", LIBPAJE_VERSION_STRING); exit(EXIT_SUCCESS);
    case ARGP_KEY_ARG:
      /* Too many arguments. */
      if (args->input_size == VALIDATE_INPUT_SIZE)
        argp_usage(state);
      args->input[state->arg_num] = arg;
      args->input_size++;
      break;
    case ARGP_KEY_END:
      /* Not enough arguments. */
      if (state->arg_num < VALIDATE_INPUT_SIZE)
        argp_usage(state);
      break;
    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = { options, parse_options, args_doc, doc, 0, 0, 0 };
