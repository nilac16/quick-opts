#include <alloca.h>
#include <stdlib.h>
#include <string.h>
#include "opt.h"


/** @brief Short option comparison function for qsort(3) and bsearch(3) */
static int optspec_shrtcmp(const void *p1, const void *p2)
{
    const struct optspec *const *os1 = p1, *const *os2 = p2;

    return ((*os1)->shrt > (*os2)->shrt) - ((*os1)->shrt < (*os2)->shrt);
}


/** @brief Long option comparison function for qsort(3) and bsearch(3) */
static int optspec_lngcmp(const void *p1, const void *p2)
{
    const struct optspec *const *os1 = p1, *const *os2 = p2;

    return strcmp((*os1)->lng, (*os2)->lng);
}


/** @brief An argument string classification */
enum argtype {
    ARG_TOKEN,  /* Not an option */
    ARG_END,    /* "--" to stop parsing options */
    ARG_SHORT,  /* A short option string */
    ARG_LONG,   /* A long option string */
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


/** @brief Check for some amount of arguments and invoke the option callback
 *  @param info
 *      Option information
 *  @param job
 *      The context for this particular option
 *  @returns Whatever the callback returns
 */
static int opt_call_back(struct optinfo *info, const struct optspec *job)
{
    char **args = info->argv;
    unsigned i, lim = (unsigned)job->args;
    struct arg arg;

    for (i = 0; i < lim; i++) {
        if (!arg_get(info, &arg)) {
            break;
        } else if (arg.type != ARG_TOKEN) {
            arg_unget(info);
            break;
        }
    }
    return job->func(i, args, info->data);
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
static int opt_short(struct optinfo       *info,
                     unsigned              nshrt,
                     const struct optspec *shrt[],
                     char                 *opt)
{
    struct optspec key, *kyptr = &key, **fnd;
    int res = 0, noargs;

    noargs = opt[1] != '\0';
    do {
        key.shrt = *opt;
        fnd = bsearch(&kyptr, shrt, nshrt, sizeof *shrt, optspec_shrtcmp);
        if (fnd) {
            res = noargs ? (*fnd)->func(0, info->argv, info->data)
                         : opt_call_back(info, *fnd);
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
static int opt_long(struct optinfo       *info,
                    unsigned              nlng,
                    const struct optspec *lng[],
                    char                 *opt)
{
    struct optspec key = {
        .lng = opt
    }, *kyptr = &key, **fnd;
    int res;

    fnd = bsearch(&kyptr, lng, nlng, sizeof *lng, optspec_lngcmp);
    if (fnd) {
        res = opt_call_back(info, *fnd);
    } else {
        res = info->errcb(1, '\0', opt, info->data);
    }
    return res;
}


/** @brief Read all arguments
 *  @param info
 *      Option information
 *  @param nshrt
 *      Short option count
 *  @param shrt
 *      Array of pointers to the option table, sorted by short option
 *  @param nlng
 *      Long option count
 *  @param lng
 *      Array of pointers to the option table, sorted by long option
 *  @returns Zero unless a callback says otherwise
 */
static int opt_read(struct optinfo       *info,
                    unsigned              nshrt,
                    const struct optspec *shrt[],
                    unsigned              nlng,
                    const struct optspec *lng[])
{
    struct arg arg;
    int res = 0;

    while (arg_get(info, &arg) && !res) {
        switch (arg.type) {
        case ARG_TOKEN:
            arg_unget(info);
            /* FALL THRU */
        case ARG_END:
            return info->poscb(info->argc, info->argv, info->data);
        case ARG_SHORT:
            res = opt_short(info, nshrt, shrt, arg.str + 1);
            break;
        case ARG_LONG:
            res = opt_long(info, nlng, lng, arg.str + 2);
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
    const struct optspec **shrt, **lng;
    unsigned i, nshrt = 0, nlng = 0;
    int res;

    /* There is no way an array of pointers is going to cause an #SS fault */
    shrt = alloca(sizeof *shrt * nopt);
    lng = alloca(sizeof *lng * nopt);
    for (i = 0; i < nopt; i++) {
        if (opts[i].shrt) {
            shrt[nshrt++] = &opts[i];
        }
        if (opts[i].lng) {
            lng[nlng++] = &opts[i];
        }
    }
    qsort(shrt, nshrt, sizeof *shrt, optspec_shrtcmp);
    qsort(lng, nlng, sizeof *lng, optspec_lngcmp);
    res = opt_first(info);
    return res ? res : opt_read(info, nshrt, shrt, nlng, lng);
}
