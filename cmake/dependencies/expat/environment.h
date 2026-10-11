#ifndef COT_EXPAT_ENVIRONMENT_H
#define COT_EXPAT_ENVIRONMENT_H

/* Read the CRT declaration before binding Expat's diagnostic lookups. */
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
char *cot_expat_getenv(const char *name);
#ifdef __cplusplus
}
#endif

#define getenv cot_expat_getenv

#endif
