#ifndef VPTREE_H
#define VPTREE_H

#include <stdint.h>
#include "vectorize.h"

/* Um vizinho retornado pela busca KNN */
typedef struct {
    int   index;    /* indice do vizinho no dataset */
    float distance; /* distancia euclidiana ao query vector */
} neighbor_t;

/* Um node da VP-Tree (16 bytes, compativel com file_format.h) */
typedef struct {
    int   vantage_idx; /* indice do vantage point no dataset */
    float radius;      /* mediana das distancias dos pontos filhos */
    int   left_child;  /* indice do filho esquerdo (-1 = folha) */
    int   right_child; /* indice do filho direito  (-1 = folha) */
} vptree_node_t;

/* A VP-Tree completa — nao possui os dados, apenas ponteiros */
typedef struct {
    float        *vectors;     /* ponteiro para array flat de vetores (NAO copia) */
    uint8_t      *labels;      /* ponteiro para array de labels (NAO copia) */
    int           num_vectors; /* numero de vetores no dataset */
    vptree_node_t *nodes;      /* array de nodes construido em heap */
    int           num_nodes;   /* numero de nodes alocados */
} vptree_t;

/*
 * vptree_build — Constroi a VP-Tree a partir de vetores pre-existentes.
 *
 * Parametros:
 *   tree        — estrutura a ser preenchida (saida)
 *   vectors     — array flat: vectors[i*VECTOR_DIMS .. (i+1)*VECTOR_DIMS-1]
 *   labels      — array de labels paralelo a vectors (0=legit, 1=fraud)
 *   num_vectors — numero de vetores
 *
 * A funcao NAO copia os vetores — tree->vectors aponta para o array original.
 * A memoria de tree->nodes e alocada internamente e deve ser liberada com
 * vptree_free().
 *
 * Retorna 0 em sucesso, -1 em falha de alocacao.
 */
int vptree_build(vptree_t *tree, float *vectors, uint8_t *labels,
                 int num_vectors);

/*
 * vptree_search — Busca os k vizinhos mais proximos ao query vector.
 *
 * Parametros:
 *   tree    — VP-Tree construida por vptree_build()
 *   query   — vetor de busca com VECTOR_DIMS dimensoes
 *   k       — numero de vizinhos desejados (tipicamente KNN_K=5)
 *   results — array de saida com pelo menos k entradas
 *
 * Retorna o numero de vizinhos encontrados (deve ser igual a k, exceto
 * se num_vectors < k).
 *
 * Os resultados NAO sao garantidos em ordem de distancia (use qsort se
 * precisar de ordem). O objetivo e retornar os k mais proximos, nao
 * necessariamente ordenados.
 */
int vptree_search(const vptree_t *tree, const float query[VECTOR_DIMS],
                  int k, neighbor_t *results);

/*
 * vptree_free — Libera a memoria dos nodes da arvore.
 *
 * NAO libera vectors nem labels (que pertencem ao chamador).
 */
void vptree_free(vptree_t *tree);

#endif /* VPTREE_H */
