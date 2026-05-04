/*
 * test_main.c — Runner de todas as suites de teste
 *
 * Executa em ordem: vetorizacao, distancia, scoring.
 * Imprime resumo final: "X passed, Y failed".
 * Retorna 0 se todos passam, 1 se algum falha.
 *
 * Os contadores _tests_run/_tests_passed/_tests_failed definidos em test.h
 * sao globais por translation unit. Este arquivo e o unico que inclui test.h
 * diretamente para poder usar TEST_REPORT() no final. As suites de teste
 * incluem test.h e usam RUN_TEST() mas NAO chamam TEST_REPORT() — apenas
 * retornam _tests_failed para que o runner accumule o total.
 */

#include <stdio.h>

/* Declaracoes das suites — cada arquivo de suite expoe um runner */
int run_vectorize_tests(void);
int run_distance_tests(void);
int run_scoring_tests(void);
int run_vptree_tests(void);
int run_quantize_tests(void);
int run_ivf_tests(void);

int main(void)
{
    int total_failed = 0;

    printf("Rinha de Backend 2026 — TDD Suite (RED)\n");
    printf("=========================================\n");

    total_failed += run_vectorize_tests();
    total_failed += run_distance_tests();
    total_failed += run_scoring_tests();
    total_failed += run_vptree_tests();
    total_failed += run_quantize_tests();
    total_failed += run_ivf_tests();

    printf("\n=========================================\n");
    if (total_failed == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("FAILED: %d test(s) failed\n", total_failed);
        return 1;
    }
}
