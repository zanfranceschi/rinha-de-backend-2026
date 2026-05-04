/*
 * engine.h — Interface do motor de deteccao de fraude
 *
 * O engine orquestra:
 *   1. Carga do index.bin via mmap (MAP_SHARED, PROT_READ)
 *   2. Vetorizacao da transacao (vectorize.h)
 *   3. Busca KNN na VP-Tree (vptree.h)
 *   4. Calculo do fraud_score e decisao approved
 *
 * Fallback: qualquer erro interno retorna {approved=1, fraud_score=0.0f}
 * (HTTP 500 tem peso 5 vs FP peso 1 — fallback sempre melhor que crash)
 */

#ifndef ENGINE_H
#define ENGINE_H

#include "vectorize.h"

typedef struct {
    int   approved;    /* 1 = aprovado, 0 = recusado */
    float fraud_score; /* [0.0, 1.0] */
} score_result_t;

/*
 * engine_init — Carrega index.bin via mmap e prepara a VP-Tree.
 *
 * Parametros:
 *   index_path — caminho absoluto ou relativo do index.bin
 *
 * Retorna 0 em sucesso, -1 em falha (arquivo nao encontrado, magic invalido,
 * versao incompativel, ou falha de mmap).
 *
 * Deve ser chamado uma unica vez no startup do servidor.
 * Nao e thread-safe durante a inicializacao (chamar antes de criar threads).
 */
int engine_init(const char *index_path);

/*
 * engine_score — Processa uma transacao e retorna o resultado de deteccao.
 *
 * Parametros:
 *   tx — transacao pre-parseada (todos os campos preenchidos)
 *
 * Retorna score_result_t com approved e fraud_score.
 * Em caso de erro interno, retorna fallback {approved=1, fraud_score=0.0f}.
 *
 * Thread-safe apos engine_init (leitura somente do mmap compartilhado).
 */
score_result_t engine_score(const transaction_t *tx);

/*
 * engine_shutdown — Libera recursos (munmap do index.bin).
 *
 * Opcional no processo principal (OS libera no exit), util em testes.
 */
void engine_shutdown(void);

/*
 * engine_ready — Retorna 1 se o engine foi inicializado com sucesso.
 */
int engine_ready(void);

#endif /* ENGINE_H */
