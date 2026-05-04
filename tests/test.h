#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <math.h>

/* Global counters — reset per test binary run */
static int _tests_run    = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

/* Float comparison with epsilon */
#define ASSERT_FLOAT_EQ(actual, expected, epsilon)                          \
    do {                                                                     \
        float _a = (float)(actual);                                          \
        float _e = (float)(expected);                                        \
        if (fabsf(_a - _e) > (float)(epsilon)) {                             \
            printf("  FAIL %s:%d — expected %.6f, got %.6f (eps=%.6f)\n",   \
                   __FILE__, __LINE__, (double)_e, (double)_a,               \
                   (double)(epsilon));                                        \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/* Integer equality */
#define ASSERT_INT_EQ(actual, expected)                                      \
    do {                                                                      \
        int _a = (int)(actual);                                               \
        int _e = (int)(expected);                                             \
        if (_a != _e) {                                                       \
            printf("  FAIL %s:%d — expected %d, got %d\n",                   \
                   __FILE__, __LINE__, _e, _a);                               \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* Boolean: condition must be non-zero */
#define ASSERT_TRUE(cond)                                                    \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("  FAIL %s:%d — expected true, got false\n",              \
                   __FILE__, __LINE__);                                        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* Boolean: condition must be zero */
#define ASSERT_FALSE(cond)                                                   \
    do {                                                                      \
        if ((cond)) {                                                         \
            printf("  FAIL %s:%d — expected false, got true\n",              \
                   __FILE__, __LINE__);                                        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* Run a test function: int fn(void), returns 0=pass 1=fail */
#define RUN_TEST(fn)                                                         \
    do {                                                                      \
        _tests_run++;                                                         \
        printf("  %-55s", #fn);                                               \
        int _result = fn();                                                   \
        if (_result == 0) {                                                   \
            printf("PASS\n");                                                 \
            _tests_passed++;                                                  \
        } else {                                                              \
            _tests_failed++;                                                  \
        }                                                                     \
    } while (0)

/* Print final summary and return exit code (0=all pass, 1=any fail) */
#define TEST_REPORT()                                                        \
    do {                                                                      \
        printf("\n%d passed, %d failed (of %d total)\n",                     \
               _tests_passed, _tests_failed, _tests_run);                    \
        return (_tests_failed > 0) ? 1 : 0;                                  \
    } while (0)

#endif /* TEST_H */
