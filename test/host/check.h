#pragma once
#include <cstdio>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (cond) {                                                            \
      ++g_pass;                                                            \
    } else {                                                               \
      ++g_fail;                                                            \
      std::printf("  실패 %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                      \
  } while (0)

#define RUN(fn)                    \
  do {                             \
    std::printf("- %s\n", #fn);    \
    fn();                          \
  } while (0)

static int finish() {
  std::printf("통과 %d, 실패 %d\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
