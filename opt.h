#pragma once

#ifndef OPT_H
#define OPT_H

#if defined(__cplusplus) && __cplusplus
extern "C" {
#endif


/** @details This function signature is invoked for individual options, with
 *      their parsed arguments *only*. You must disambiguate which option this
 *      function is being called for by using a specific function per option, or
 *      using the @p idx parameter.
 *
 *      This signature is also used for the remaining positional arguments, in
 *      which case @p count is the remainder of argc and @p args is the
 *      remainder of argv.
 *  @brief Option callback
 *  @param idx
 *      The index of this option in the list supplied to the topmost call. This
 *      will allow easy disambiguation from a single callback function that can
 *      then dispatch (i.e. a static member function dispatching to member
 *      functions in C++) as described in the Win32 API tutorial.
 *
 *      Note that using a different callback function for each option obviates
 *      any need to even inspect this value, as you will know which option was
 *      pulled for which function.
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
typedef int optcbfn_t(int idx, unsigned count, char *args[], void *data);


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
    /* int         req; *//* Total required arguments, regardless of type */
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

    opterrfn_t *errcb;  /* Error callback invoked on unrecognized options */
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
 *      TODO: Add a workaround for this, because this prevents negative numbers
 *            from being used as option arguments
 *
 *      This function does not use any heap memory nor issue any stdio calls.
 *      This function uses qsort(3), bsearch(3), and potentially alloca(3). If
 *      alloca(3) is not allowed (-DUSE_ALLOCA=0) or unavailable, then VLAs are
 *      used instead.
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



#if defined(__cplusplus) && __cplusplus
}
#endif

#endif /* OPT_H */


#if defined(OPT_IMPLEMENTATION) && OPT_IMPLEMENTATION

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifndef USE_ALLOCA
#   define USE_ALLOCA 1

#endif

#if USE_ALLOCA
#   if __has_include(<alloca.h>)
#       include <alloca.h>

#   elif defined(_MSC_VER) && _MSC_VER
#       include <malloc.h>
#       ifndef alloca
#           define alloca(size) _alloca(size)
#       endif

#   else
#       undef USE_ALLOCA
#       define USE_ALLOCA 0
#   endif

#endif


/** @brief Short option comparison function for qsort(3) and bsearch(3) */
static int optspec_shrtcmp(const void *p1, const void *p2)
{
    const struct optspec *const *os1, *const *os2;

    os1 = (const struct optspec *const *)p1;
    os2 = (const struct optspec *const *)p2;
    return ((*os1)->shrt > (*os2)->shrt) - ((*os1)->shrt < (*os2)->shrt);
}


/** @brief Long option comparison function for qsort(3) and bsearch(3) */
static int optspec_lngcmp(const void *p1, const void *p2)
{
    const struct optspec *const *os1, *const *os2;

    os1 = (const struct optspec *const *)p1;
    os2 = (const struct optspec *const *)p2;
    return strcmp((*os1)->lng, (*os2)->lng);
}


/** @brief An argument string classification */
enum argtype {
    ARG_TOKEN,  /* Not an option */
    ARG_END,    /* "--" to stop parsing options */
    ARG_SHORT,  /* A short option string */
    ARG_LONG    /* A long option string */
};


/** @brief Find the classification of @p arg
 *  @param arg
 *      Argument string
 *  @returns The argument class of @p arg
 */
static enum argtype arg_classify(const char *arg)
/* Should update this with ctype(3) functions */
{
    const char dash = '-';

    if (arg[0] == dash) {
        if (arg[1] == dash) {
            return arg[2] ? ARG_LONG : ARG_END;
        } else if (arg[1]) {
            return ARG_SHORT;
        }
    }
    return ARG_TOKEN;
}


/** @brief A single classified cmdarg */
struct arg {
    char        *str;   /* The argument string itself */
    enum argtype type;  /* The type of argument */
};


/** @brief Attempt to read the next argument in @p info and advance the cmdarg
 *      pointer
 *  @param info
 *      Option information including cmdargs
 *  @param arg
 *      Argument buffer
 *  @returns Nonzero if an argument was successfully read, zero if there are no
 *      more arguments
 */
static int arg_get(struct optinfo *info, struct arg *arg)
{
    if (!info->argc) {
        return 0;
    }
    arg->str = info->argv[0];
    arg->type = arg_classify(arg->str);
    info->argc--;
    info->argv++;
    return 1;
}


/** @brief Rewind the argument list by one
 *  @param info
 *      Option information
 *  @param arg
 *      Extracted option
 */
static void arg_unget(struct optinfo *info)
{
    info->argc++;
    info->argv--;
}


/** @brief All of the option information wrapped up neatly */
struct opttbl {
    unsigned               nopt;    /* Total option count */
    unsigned               nshrt;   /* Short option count */
    unsigned               nlng;    /* Long option count */
    const struct optspec  *opts;    /* The original list */
    const struct optspec **shrt;    /* SORTED short options */
    const struct optspec **lng;     /* SORTED long options */
};


/** @brief Find @p key in @p tbl
 *  @param tbl
 *      Options table
 *  @param key
 *      Option being searched
 *  @param len
 *      Nonzero if this is a long option
 *  @returns A pointer to the found option or NULL if not found
 */
static const struct optspec *opt_find(const struct opttbl  *tbl,
                                      const struct optspec *key,
                                      int                   len)
{
    typedef int cmpfn_t(const void *, const void *);
    const size_t size = sizeof tbl->opts;
    const void *base = len ? tbl->lng       : tbl->shrt;
    size_t nmemb     = len ? tbl->nlng      : tbl->nshrt;
    cmpfn_t *cmpfn   = len ? optspec_lngcmp : optspec_shrtcmp;
    const struct optspec **res;

    res = (const struct optspec **)bsearch(&key, base, nmemb, size, cmpfn);
    return res ? *res : NULL;
}


/** @brief Check if @p arg represents a valid option argument string 
 *  @param info
 *      Option information
 *  @param tbl
 *      Option table
 *  @param arg
 *      The cmdarg in question
 *  @returns Nonzero if @p arg is a valid option argument string for this
 *      program
 */
static int opt_valid_argument(const struct optinfo *info,
                              const struct opttbl  *tbl,
                              const struct arg     *arg)
{
    (void)info;
    (void)tbl;
    if (arg->type == ARG_TOKEN) {
        return 1;
    } else if (arg->type == ARG_SHORT) {
        /* Quick fix to allow negative numbers */
        return isdigit(arg->str[1]);
    } else {
        return 0;
    }
}


/** @brief Check for some amount of arguments and invoke the option callback
 *  @param info
 *      Option information
 *  @param tbl
 *      Option table
 *  @param job
 *      The context for this particular option
 *  @returns Whatever the callback returns
 */
static int opt_call_back(struct optinfo       *info,
                         const struct opttbl  *tbl,
                         const struct optspec *job)
{
    char **args = info->argv;
    unsigned i, lim = (unsigned)job->args;
    struct arg arg;

    for (i = 0; i < lim; i++) {
        if (!arg_get(info, &arg)) {
            break;
        } else if (!opt_valid_argument(info, tbl, &arg)) {
            arg_unget(info);
            break;
        }
    }
    return job->func((int)(job - tbl->opts), i, args, info->data);
}


/** @brief Read a short option string
 *  @param info
 *      Option information
 *  @param nshrt
 *      Short option count
 *  @param shrt
 *      Sorted short option information
 *  @param opt
 *      The short option string
 *  @returns Nonzero if told to do so
 */
static int opt_short(struct optinfo      *info,
                     const struct opttbl *tbl,
                     char                *opt)
{
    const struct optspec *fnd;
    struct optspec key;
    int res = 0, noargs, idx;

    noargs = opt[1] != '\0';
    do {
        key.shrt = *opt;
        fnd = opt_find(tbl, &key, 0);
        if (fnd) {
            idx = (int)(fnd - tbl->opts);
            res = noargs ? fnd->func(idx, 0, info->argv, info->data)
                         : opt_call_back(info, tbl, fnd);
        } else {
            res = info->errcb(0, key.shrt, NULL, info->data);
        }
    } while (*++opt && !res);
    return res;
}


/** @brief Read a long option string
 *  @param info
 *      Option information
 *  @param nlng
 *      Long option count
 *  @param shrt
 *      Sorted long option information
 *  @param opt
 *      The long option string
 *  @returns Nonzero if told to do so
 */
static int opt_long(struct optinfo      *info,
                    const struct opttbl *tbl,
                    char                *opt)
{
    const struct optspec *fnd;
    struct optspec key;
    int res;

    key.lng = opt;
    fnd = opt_find(tbl, &key, 1);
    if (fnd) {
        res = opt_call_back(info, tbl, fnd);
    } else {
        res = info->errcb(1, '\0', opt, info->data);
    }
    return res;
}


/** @brief Read all arguments
 *  @param info
 *      Option information
 *  @param tbl
 *      Sorted options table
 *  @returns Zero unless a callback says otherwise
 */
static int opt_read(struct optinfo *info, const struct opttbl *tbl)
{
    struct arg arg;
    int res = 0;

    while (arg_get(info, &arg) && !res) {
        switch (arg.type) {
        case ARG_TOKEN:
            arg_unget(info);
            /* FALL THRU */
        case ARG_END:
            return info->poscb(-1, info->argc, info->argv, info->data);
        case ARG_SHORT:
            res = opt_short(info, tbl, arg.str + 1);
            break;
        case ARG_LONG:
            res = opt_long(info, tbl, arg.str + 2);
            break;
        }
    }
    return res;
}


/** @brief Determine what to do with the first argument
 *  @param info
 *      Option information
 */
static int opt_first(struct optinfo *info)
{
    struct arg arg;
    int res = 0;

    switch (info->fstact) {
    case OPT_FIRST_SKIP:
        arg_get(info, &arg);
        break;
    case OPT_FIRST_PARSE:
    default:
        break;
    }
    return res;
}


int opt_parse(struct optinfo *info, unsigned nopt, const struct optspec opts[])
{
    struct opttbl tbl;
    unsigned i;
    int res;

#if USE_ALLOCA
    tbl.shrt = (const struct optspec **)alloca(sizeof *tbl.shrt * nopt);
    tbl.lng = (const struct optspec **)alloca(sizeof *tbl.lng * nopt);

#else
    const struct optspec *shrtbuf[nopt], *lngbuf[nopt];

    tbl.shrt = shrtbuf;
    tbl.lng = lngbuf;

#endif

    tbl.nopt = nopt;
    tbl.opts = opts;
    tbl.nlng = 0;
    tbl.nshrt = 0;
    for (i = 0; i < nopt; i++) {
        if (isgraph(opts[i].shrt)) {
            tbl.shrt[tbl.nshrt++] = &opts[i];
        }
        if (opts[i].lng && *opts[i].lng) {
            tbl.lng[tbl.nlng++] = &opts[i];
        }
    }
    qsort(tbl.shrt, tbl.nshrt, sizeof *tbl.shrt, optspec_shrtcmp);
    qsort(tbl.lng, tbl.nlng, sizeof *tbl.lng, optspec_lngcmp);
    res = opt_first(info);
    return res ? res : opt_read(info, &tbl);
}

#endif /* OPT_IMPLEMENTATION */
