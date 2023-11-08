#pragma once

#ifndef OPT_H
#define OPT_H


/** @brief Option callback
 *  @param count
 *      The number of positional arguments pulled for this option. If this was
 *      a short option that was part of a short option string, this will always
 *      be zero! Positional arguments to short options are only parsed if the
 *      option is isolated
 *  @param args
 *      The positional arguments pulled for this option. These are all non-
 *      option tokens pulled immediately after parsing this option, up to the
 *      limit provided in the option specification or the first option token,
 *      whichever is reached first
 *  @param data
 *      User data provided at the top level
 *  @return Nonzero to immmediately terminate all argument parsing and return
 *      control
 */
typedef int optcbfn_t(unsigned count, char *args[], void *data);


/** @brief Error function invoked when an argument is unrecognized
 *  @param type
 *      Zero if this is a short option and one if it is long
 *  @param shrt
 *      The offending character if @p type is zero, otherwise nul
 *  @param lng
 *      A pointer to the offending string if @p type is one, otherwise NULL
 *  @param data
 *      User data provided at the top call
 *  @return Nonzero to terminate argument parsing
 */
typedef int opterrfn_t(int type, char shrt, char *lng, void *data);


/** @brief Option specification */
struct optspec {
    char        shrt;   /* The short option character (nul for no short opt) */
    const char *lng;    /* The long option string (NULL for no long opt) */
    int         args;   /* The total possible positional args (-1: no limit) */
    optcbfn_t  *func;   /* Callback invoked on successful parsing */
};


/** @brief What to do with the first cmd arg */
enum optfst {
    OPT_FIRST_SKIP,     /* Do not parse */
    OPT_FIRST_PARSE     /* Parse it like a standard argument */
};


/** @brief What to do when encountering "--" as a cmdarg */
enum optend {
    OPT_END_ALLOW,      /* Allow "--" to end option parsing */
    OPT_END_DISALLOW    /* Do not allow "--" to end option parsing */
};


/** @brief Context structure */
struct optinfo {
    int         argc;   /* Cmdarg count (always include argv[0]) */
    char      **argv;   /* Cmdarg strings */

    enum optfst fstact; /* First argument disposition */
    enum optend endact; /* Endopt token disposition */

    opterrfn_t *errcb;  /* Error callback */
    optcbfn_t  *poscb;  /* Callback invoked after all options are parsed */
    void       *data;   /* Callback data */
};


/** @details The first argument is handled according to the disposition
 *      specified by the "fstact" member of @p info. Afterwards, cmdargs are
 *      parsed left-to-right.
 *
 *      Upon encountering a non-option token (or the end-of-option argument as
 *      specified by "endact"), the positional arguments callback "poscb" is
 *      invoked on the remainder of argv. This has the effect of forcing
 *      optional cmdargs to appear before positional args. Recursion from the
 *      positional argument callback can change this behavior.
 *
 *      Option arguments may not begin with a dash or they will stop argument
 *      parsing for the current option and be parsed as options themselves.
 *
 *      This function does not use any heap memory nor issue any stdio calls.
 *  @brief Parse command-line arguments according to @p opts
 *  @param info
 *      Option context structure
 *  @param nopt
 *      Length of @p opts
 *  @param opts
 *      Option specification table
 *  @returns Zero on complete success, nonzero if it was told to by a callback.
 *      The return value is exactly the same as the terminating callback's
 *      return value
 */
int opt_parse(struct optinfo *info, unsigned nopt, const struct optspec opts[]);


#endif /* OPT_H */
