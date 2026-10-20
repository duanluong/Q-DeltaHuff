/*
 * qhufdel_cli: applies a Q-HUFDEL update package to a model on the PC and
 * checks the result, with the same decoder library and decode flow as the
 * STM32 / ESP32-S3 benchmark firmware.
 *
 *   qhufdel_cli -c current.h -n next.h -p update.bin [options]
 *
 * Exit codes: 0 OK, 1 mismatch, 2 package/CRC error, 3 input/usage error.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_timer.h"
#include "model_loader.h"
#include "package_runner.h"

#define QHUFDEL_CLI_VERSION "1.0.0"

enum
{
    EXIT_OK            = 0,
    EXIT_MISMATCH      = 1,
    EXIT_PACKAGE_ERROR = 2,
    EXIT_INPUT_ERROR   = 3,
};

typedef struct
{
    const char *    current_path;
    const char *    next_path;
    const char *    package_path;
    const char *    out_path;
    runner_config_t run;
} cli_args_t;

static void
print_usage (const char * prog)
{
    printf(
        "qhufdel_cli %s - Q-HUFDEL update package decoder / checker\n"
        "\n"
        "Usage:\n"
        "  %s -c <current> -n <next> -p <package.bin> [options]\n"
        "\n"
        "Required:\n"
        "  -c, --current <file>   current model (C header .h, or raw "
        ".tflite/.bin)\n"
        "  -n, --next <file>      expected next model (C header .h, or "
        "raw)\n"
        "  -p, --package <file>   Q-HUFDEL update package (.bin)\n"
        "\n"
        "Options:\n"
        "  -i, --iterations <N>   decode N times for timing (default 1)\n"
        "      --chunk-in <B>     input chunk size in bytes (default %u)\n"
        "      --chunk-out <N>    values decoded per chunk (default %u)\n"
        "  -o, --out <file>       write the restored model (.h = C header, "
        "else raw)\n"
        "  -m, --in-memory        load the package in RAM first (no file "
        "I/O)\n"
        "  -v, --verbose          log every iteration, not only the first\n"
        "  -h, --help             show this help\n"
        "      --version          show the version\n"
        "\n"
        "Exit codes:\n"
        "  0  restored model matches the next model, CRC OK\n"
        "  1  restored model differs from the next model\n"
        "  2  package error (magic, header, codebook, payload or CRC)\n"
        "  3  input / usage error (missing file, bad header, wrong model "
        "pair)\n",
        QHUFDEL_CLI_VERSION,
        prog,
        RUNNER_DEFAULT_IN_CHUNK_SIZE,
        RUNNER_DEFAULT_OUT_CHUNK_VALUES);
}

static bool
parse_uint (const char * opt, const char * text, unsigned long min,
            unsigned long max, unsigned long * out)
{
    char *        end = NULL;
    unsigned long v   = strtoul(text, &end, 0);
    if (text[0] == '\0' || text[0] == '-' || *end != '\0' || v < min
        || v > max)
    {
        printf("Invalid value for %s: '%s' (expected %lu..%lu)\n",
               opt,
               text,
               min,
               max);
        return false;
    }
    *out = v;
    return true;
}

static bool
opt_is (const char * opt, const char * short_opt, const char * long_opt)
{
    return (short_opt != NULL && strcmp(opt, short_opt) == 0)
           || strcmp(opt, long_opt) == 0;
}

/* Returns the value following option argv[*i], or NULL if it is missing */
static const char *
opt_value (int argc, char ** argv, int * i)
{
    if (*i + 1 >= argc)
    {
        printf("Missing value for %s\n", argv[*i]);
        return NULL;
    }
    return argv[++(*i)];
}

/* Returns -1 to continue, otherwise the exit code */
static int
parse_args (int argc, char ** argv, cli_args_t * a)
{
    memset(a, 0, sizeof(*a));
    a->run.iterations       = 1;
    a->run.in_chunk_size    = RUNNER_DEFAULT_IN_CHUNK_SIZE;
    a->run.out_chunk_values = RUNNER_DEFAULT_OUT_CHUNK_VALUES;

    for (int i = 1; i < argc; i++)
    {
        const char * opt = argv[i];

        unsigned long v;
        if (opt_is(opt, "-h", "--help"))
        {
            print_usage(argv[0]);
            return EXIT_OK;
        }
        else if (opt_is(opt, NULL, "--version"))
        {
            printf("qhufdel_cli %s\n", QHUFDEL_CLI_VERSION);
            return EXIT_OK;
        }
        else if (opt_is(opt, "-c", "--current"))
        {
            if ((a->current_path = opt_value(argc, argv, &i)) == NULL)
            {
                return EXIT_INPUT_ERROR;
            }
        }
        else if (opt_is(opt, "-n", "--next"))
        {
            if ((a->next_path = opt_value(argc, argv, &i)) == NULL)
            {
                return EXIT_INPUT_ERROR;
            }
        }
        else if (opt_is(opt, "-p", "--package"))
        {
            if ((a->package_path = opt_value(argc, argv, &i)) == NULL)
            {
                return EXIT_INPUT_ERROR;
            }
        }
        else if (opt_is(opt, "-o", "--out"))
        {
            if ((a->out_path = opt_value(argc, argv, &i)) == NULL)
            {
                return EXIT_INPUT_ERROR;
            }
        }
        else if (opt_is(opt, "-i", "--iterations"))
        {
            const char * val = opt_value(argc, argv, &i);
            if (val == NULL || !parse_uint(opt, val, 1, 1000000, &v))
            {
                return EXIT_INPUT_ERROR;
            }
            a->run.iterations = (uint32_t) v;
        }
        else if (opt_is(opt, NULL, "--chunk-in"))
        {
            /* The bit reader needs room for at least one 8-byte frame */
            const char * val = opt_value(argc, argv, &i);
            if (val == NULL || !parse_uint(opt, val, 8, 16u * 1024u * 1024u, &v))
            {
                return EXIT_INPUT_ERROR;
            }
            a->run.in_chunk_size = (size_t) v;
        }
        else if (opt_is(opt, NULL, "--chunk-out"))
        {
            const char * val = opt_value(argc, argv, &i);
            if (val == NULL || !parse_uint(opt, val, 1, 16u * 1024u * 1024u, &v))
            {
                return EXIT_INPUT_ERROR;
            }
            a->run.out_chunk_values = (size_t) v;
        }
        else if (opt_is(opt, "-m", "--in-memory"))
        {
            a->run.in_memory = true;
        }
        else if (opt_is(opt, "-v", "--verbose"))
        {
            a->run.verbose = true;
        }
        else
        {
            printf("Unknown option: %s (see --help)\n", opt);
            return EXIT_INPUT_ERROR;
        }
    }

    if (a->current_path == NULL || a->next_path == NULL
        || a->package_path == NULL)
    {
        if (argc > 1)
        {
            printf("Missing required option: --current, --next and --package "
                   "are all required\n\n");
        }
        print_usage(argv[0]);
        return EXIT_INPUT_ERROR;
    }
    a->run.keep_restored = (a->out_path != NULL);
    return -1;
}

int
main (int argc, char ** argv)
{
    cli_args_t args;
    int        rc = parse_args(argc, argv, &args);
    if (rc >= 0)
    {
        return rc;
    }

    bench_timer_init();
    printf("qhufdel_cli %s (timer resolution %.1f ns)\n",
           QHUFDEL_CLI_VERSION,
           bench_timer_resolution_ns());

    model_t current = {0};
    model_t next    = {0};
    if (model_load(args.current_path, &current) != 0
        || model_load(args.next_path, &next) != 0)
    {
        model_free(&current);
        model_free(&next);
        return EXIT_INPUT_ERROR;
    }
    printf("Current model: %s (%zu bytes) from %s\n",
           current.name,
           current.len,
           args.current_path);
    printf("Next model   : %s (%zu bytes) from %s\n",
           next.name,
           next.len,
           args.next_path);

    runner_result_t result;
    runner_run(args.package_path, &current, &next, &args.run, &result);
    runner_print(&result);

    int exit_code;
    switch (result.status)
    {
        case RUN_OK:
            exit_code = EXIT_OK;
            break;
        case RUN_MISMATCH:
            exit_code = EXIT_MISMATCH;
            break;
        case RUN_PACKAGE_ERROR:
        case RUN_CRC_ERROR:
            exit_code = EXIT_PACKAGE_ERROR;
            break;
        case RUN_INPUT_ERROR:
        default:
            exit_code = EXIT_INPUT_ERROR;
            break;
    }

    if (args.out_path != NULL)
    {
        if (result.restored != NULL)
        {
            if (model_write(args.out_path, result.restored, result.restored_len)
                    != 0
                && exit_code == EXIT_OK)
            {
                exit_code = EXIT_INPUT_ERROR;
            }
        }
        else
        {
            printf("Restored model not written: decoding did not complete\n");
        }
    }

    runner_result_free(&result);
    model_free(&current);
    model_free(&next);
    return exit_code;
}
