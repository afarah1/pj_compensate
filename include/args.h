/* Argument parsing */
#pragma once

#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

static char doc[] = "Outputs a trace compensating for Aky's intrusion";
static char args_doc[] = "ORIGINAL-TRACE COPYTIME-DATA OVERHEAD SYNC-BYTES";
static struct argp_option options[] = {
  {"version", 'v', 0, OPTION_ARG_OPTIONAL, "Print version", 0},
  { 0 }
};

#define NUM_ARGS 4

struct arguments {
  char *input[NUM_ARGS];
};

/* state should be zerod and errno should be zero */
static error_t
parse_options(int key, char *arg, struct argp_state *state)
{
  struct arguments *args = state->input;
  switch (key) {
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
