#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _CONFIGURATION {
    char key_dir[4096];
} CONFIGURATION;

extern CONFIGURATION config;

#ifdef __cplusplus
}
#endif
