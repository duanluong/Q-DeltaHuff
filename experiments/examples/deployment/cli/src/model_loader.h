#ifndef MODEL_LOADER_H
#define MODEL_LOADER_H

#include <stddef.h>
#include <stdint.h>

/* A model image held in RAM (the firmware links it as a const array) */
typedef struct
{
    uint8_t * data;
    size_t    len;
    char      name[128]; /* C array name, or file name for binary input */
} model_t;

/*
 * Loads a model from:
 *   - a C header produced by xxd / the model export scripts
 *     (`[const] unsigned char NAME[] = {0x.., ...};` + optional `NAME_len`),
 *   - or a raw binary file (.tflite, .bin, ...).
 * The format is chosen from the extension (.h/.hpp/.c/.inc/.txt = text),
 * otherwise by sniffing the first bytes.
 *
 * Returns 0 on success, prints the reason and returns -1 on failure.
 */
int  model_load(const char * path, model_t * out);
void model_free(model_t * m);

/*
 * Writes `len` bytes to `path`: a C header (same layout as the firmware's
 * write_array_to_header()) when the extension is .h, raw binary otherwise.
 */
int model_write(const char * path, const uint8_t * data, size_t len);

#endif /* MODEL_LOADER_H */
