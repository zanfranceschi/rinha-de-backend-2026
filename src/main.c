/*
 * main.c — HTTP server do motor de deteccao de fraude
 *
 * Endpoints:
 *   GET  /ready       → 200 OK (engine ok) ou 503 Service Unavailable
 *   POST /fraud-score → parse JSON → engine_score() → JSON response
 *
 * Fallback: qualquer erro de parse ou erro interno retorna:
 *   {"approved":true,"fraud_score":0.0000}
 * (HTTP 500 tem peso 5 vs FP peso 1 — fallback sempre melhor que crash)
 *
 * Compilacao:
 *   gcc -O3 -std=c11 -o server main.c engine.c vectorize.c distance.c vptree.c
 *       vendor/mongoose.c vendor/yyjson.c -lm -I. -Ivendor
 *
 * Uso:
 *   ./server [--port 8080] [--index index.bin]
 */

#define MG_ENABLE_POSIX_FS 1

#include "vendor/mongoose.h"
#include "vendor/yyjson.h"
#include "engine.h"
#include "vectorize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

/* =========================================================================
 * Configuracao global
 * ========================================================================= */

static int  g_running    = 1;
static char g_listen_url[64] = "http://0.0.0.0:8080";
static int  g_num_workers = 1;  /* default: single process */

/* Buffer de resposta pre-alocado por thread (no-alloc per request) */
#define RESP_BUF_SIZE 128

/* =========================================================================
 * Parse do payload JSON usando yyjson
 *
 * Estrutura esperada:
 * {
 *   "id": "...",
 *   "transaction": { "amount": 0.0, "installments": 0, "requested_at": "..." },
 *   "customer": {
 *     "avg_amount": 0.0,
 *     "tx_count_24h": 0,
 *     "known_merchants": ["MERC-001", ...]
 *   },
 *   "merchant": { "id": "...", "mcc": "...", "avg_amount": 0.0 },
 *   "terminal": { "is_online": false, "card_present": true, "km_from_home": 0.0 },
 *   "last_transaction": null | { "timestamp": "...", "km_from_current": 0.0 }
 * }
 *
 * Strings apontam para interior do doc yyjson — validas ate yyjson_doc_free().
 * O caller deve liberar o doc APOS usar as strings (apos engine_score()).
 * known_merchants: ponteiros armazenados em array local no stack (parsed->merchants_buf).
 * ========================================================================= */

#define MAX_KNOWN_MERCHANTS 64

typedef struct {
    transaction_t tx;
    /* Storage para ponteiros de known_merchants */
    const char *merchants_buf[MAX_KNOWN_MERCHANTS];
} parsed_tx_t;

/*
 * parse_payload — preenche parsed->tx e retorna o doc yyjson (para o caller liberar).
 * Retorna 1 em sucesso (e preenche *out_doc), 0 em falha.
 */
static int parse_payload(const char *body, size_t body_len,
                             parsed_tx_t *parsed, yyjson_doc **out_doc)
{
    yyjson_doc *doc = yyjson_read(body, body_len, 0);
    if (!doc) return 0;

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return 0;
    }

    transaction_t *tx = &parsed->tx;
    memset(tx, 0, sizeof(*tx));
    tx->known_merchants     = parsed->merchants_buf;
    tx->num_known_merchants = 0;

    yyjson_val *v;

    /* --- transaction --- */
    yyjson_val *txn = yyjson_obj_get(root, "transaction");
    if (!yyjson_is_obj(txn)) {
        yyjson_doc_free(doc);
        return 0;
    }

    v = yyjson_obj_get(txn, "amount");
    tx->amount = yyjson_is_num(v) ? (float)yyjson_get_num(v) : 0.0f;

    v = yyjson_obj_get(txn, "installments");
    tx->installments = yyjson_is_int(v) ? (int)yyjson_get_int(v) : 0;

    v = yyjson_obj_get(txn, "requested_at");
    tx->requested_at = yyjson_is_str(v) ? yyjson_get_str(v) : "1970-01-01T00:00:00Z";

    /* --- customer --- */
    yyjson_val *cust = yyjson_obj_get(root, "customer");
    if (yyjson_is_obj(cust)) {
        v = yyjson_obj_get(cust, "avg_amount");
        tx->customer_avg_amount = yyjson_is_num(v) ? (float)yyjson_get_num(v) : 0.0f;

        v = yyjson_obj_get(cust, "tx_count_24h");
        tx->customer_tx_count_24h = yyjson_is_int(v) ? (int)yyjson_get_int(v) : 0;

        yyjson_val *km_arr = yyjson_obj_get(cust, "known_merchants");
        if (yyjson_is_arr(km_arr)) {
            size_t idx, max_idx;
            yyjson_val *item;
            yyjson_arr_foreach(km_arr, idx, max_idx, item) {
                if (yyjson_is_str(item) &&
                    tx->num_known_merchants < MAX_KNOWN_MERCHANTS) {
                    parsed->merchants_buf[tx->num_known_merchants++] =
                        yyjson_get_str(item);
                }
            }
        }
    }

    /* --- merchant --- */
    yyjson_val *merch = yyjson_obj_get(root, "merchant");
    if (yyjson_is_obj(merch)) {
        v = yyjson_obj_get(merch, "id");
        tx->merchant_id = yyjson_is_str(v) ? yyjson_get_str(v) : "";

        v = yyjson_obj_get(merch, "mcc");
        tx->merchant_mcc = yyjson_is_str(v) ? yyjson_get_str(v) : "";

        v = yyjson_obj_get(merch, "avg_amount");
        tx->merchant_avg_amount = yyjson_is_num(v) ? (float)yyjson_get_num(v) : 0.0f;
    }

    /* --- terminal --- */
    yyjson_val *term = yyjson_obj_get(root, "terminal");
    if (yyjson_is_obj(term)) {
        v = yyjson_obj_get(term, "is_online");
        tx->is_online = yyjson_is_bool(v) ? (yyjson_get_bool(v) ? 1 : 0) : 0;

        v = yyjson_obj_get(term, "card_present");
        tx->card_present = yyjson_is_bool(v) ? (yyjson_get_bool(v) ? 1 : 0) : 0;

        v = yyjson_obj_get(term, "km_from_home");
        tx->km_from_home = yyjson_is_num(v) ? (float)yyjson_get_num(v) : 0.0f;
    }

    /* --- last_transaction (pode ser null) --- */
    yyjson_val *last = yyjson_obj_get(root, "last_transaction");
    if (yyjson_is_obj(last)) {
        tx->has_last_tx = 1;

        v = yyjson_obj_get(last, "timestamp");
        tx->last_tx_timestamp = yyjson_is_str(v) ? yyjson_get_str(v) : NULL;

        v = yyjson_obj_get(last, "km_from_current");
        tx->last_tx_km = yyjson_is_num(v) ? (float)yyjson_get_num(v) : 0.0f;
    } else {
        tx->has_last_tx       = 0;
        tx->last_tx_timestamp = NULL;
        tx->last_tx_km        = 0.0f;
    }

    *out_doc = doc;
    return 1;
}

/* =========================================================================
 * Handlers HTTP
 * ========================================================================= */

static void handle_ready(struct mg_connection *c)
{
    if (engine_ready()) {
        mg_http_reply(c, 200,
                      "Content-Type: application/json\r\n",
                      "{\"status\":\"ok\"}");
    } else {
        mg_http_reply(c, 503,
                      "Content-Type: application/json\r\n",
                      "{\"status\":\"not_ready\"}");
    }
}

static void handle_fraud_score(struct mg_connection *c,
                                struct mg_http_message *hm)
{
    static const char *FALLBACK_RESP =
        "{\"approved\":true,\"fraud_score\":0.0000}";

    /* Fallback rapido se engine nao inicializado */
    if (!engine_ready()) {
        mg_http_reply(c, 200,
                      "Content-Type: application/json\r\n",
                      "%s", FALLBACK_RESP);
        return;
    }

    if (hm->body.len == 0) {
        mg_http_reply(c, 200,
                      "Content-Type: application/json\r\n",
                      "%s", FALLBACK_RESP);
        return;
    }

    parsed_tx_t parsed;
    yyjson_doc *doc = NULL;

    if (!parse_payload(hm->body.buf, hm->body.len, &parsed, &doc)) {
        mg_http_reply(c, 200,
                      "Content-Type: application/json\r\n",
                      "%s", FALLBACK_RESP);
        return;
    }

    /* engine_score usa strings do doc (sincrono) — doc ainda valido */
    score_result_t result = engine_score(&parsed.tx);

    /* Libera doc apos score (strings invalidas a partir daqui) */
    yyjson_doc_free(doc);

    /*
     * Formata fraud_score com snprintf (mongoose mg_dtoa trunca "0.0000" para "0").
     * Buffer no stack — zero alocacao.
     */
    char score_buf[RESP_BUF_SIZE];
    snprintf(score_buf, sizeof(score_buf),
             "{\"approved\":%s,\"fraud_score\":%.4f}",
             result.approved ? "true" : "false",
             (double)result.fraud_score);

    mg_http_reply(c, 200,
                  "Content-Type: application/json\r\n",
                  "%s", score_buf);
}

/* =========================================================================
 * Event handler principal
 * ========================================================================= */

static void ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    if (ev != MG_EV_HTTP_MSG) return;

    struct mg_http_message *hm = (struct mg_http_message *)ev_data;

    /* GET /ready */
    if (mg_match(hm->uri, mg_str("/ready"), NULL) &&
        mg_match(hm->method, mg_str("GET"), NULL)) {
        handle_ready(c);
        return;
    }

    /* POST /fraud-score */
    if (mg_match(hm->uri, mg_str("/fraud-score"), NULL) &&
        mg_match(hm->method, mg_str("POST"), NULL)) {
        handle_fraud_score(c, hm);
        return;
    }

    /* 404 para qualquer outra rota */
    mg_http_reply(c, 404, "", "Not Found");
}

/* =========================================================================
 * Signal handler
 * ========================================================================= */

static void sig_handler(int signo)
{
    (void)signo;
    g_running = 0;
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(int argc, char *argv[])
{
    const char *index_path = "index.bin";

    /* Parse de argumentos simples */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            snprintf(g_listen_url, sizeof(g_listen_url),
                     "http://0.0.0.0:%s", argv[++i]);
        } else if (strcmp(argv[i], "--index") == 0 && i + 1 < argc) {
            index_path = argv[++i];
        } else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            g_num_workers = atoi(argv[++i]);
            if (g_num_workers < 1) g_num_workers = 1;
            if (g_num_workers > 8) g_num_workers = 8;
        } else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                    "Uso: %s [--port PORT] [--index INDEX_PATH] [--workers N]\n"
                    "  PORT       porta HTTP (default: 8080)\n"
                    "  INDEX_PATH caminho do index.bin (default: index.bin)\n"
                    "  N          numero de workers (default: 1, max: 8)\n",
                    argv[0]);
            return 0;
        }
    }

    /* Tambem aceita PORT, INDEX_PATH e WORKERS via variavel de ambiente */
    const char *env_port    = getenv("PORT");
    const char *env_index   = getenv("INDEX_PATH");
    const char *env_workers = getenv("WORKERS");
    if (env_port)
        snprintf(g_listen_url, sizeof(g_listen_url),
                 "http://0.0.0.0:%s", env_port);
    if (env_index)
        index_path = env_index;
    if (env_workers) {
        g_num_workers = atoi(env_workers);
        if (g_num_workers < 1) g_num_workers = 1;
        if (g_num_workers > 8) g_num_workers = 8;
    }

    /* Inicializa engine */
    fprintf(stderr, "Carregando index: %s\n", index_path);
    if (engine_init(index_path) != 0) {
        fprintf(stderr, "AVISO: engine_init falhou — rodando em modo fallback\n");
        /* Continua rodando: /ready retornara 503, /fraud-score retornara fallback */
    }

    /* Configura sinais */
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /*
     * Multi-worker mode: create listener first, then fork N workers.
     * Each worker inherits the listener socket via fork().
     * All workers call mg_mgr_poll on the same socket — the OS kernel
     * handles the "thundering herd" with EPOLLEXCLUSIVE/accept().
     *
     * The mmap (MAP_SHARED, PROT_READ) is shared between all workers
     * with zero memory overhead (OS deduplicates physical pages).
     */

    /* Create listener BEFORE fork — all children inherit the socket fd */
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    struct mg_connection *lc = mg_http_listen(&mgr, g_listen_url,
                                               ev_handler, NULL);
    if (!lc) {
        fprintf(stderr, "Falha ao escutar em %s\n", g_listen_url);
        mg_mgr_free(&mgr);
        engine_shutdown();
        return 1;
    }

    if (g_num_workers > 1) {
        fprintf(stderr, "Forking %d workers...\n", g_num_workers);

        for (int w = 1; w < g_num_workers; w++) {
            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "Erro: fork worker %d\n", w);
            } else if (pid == 0) {
                /* Child: run own event loop on inherited socket */
                fprintf(stderr, "Worker %d (pid=%d) iniciado\n", w, getpid());
                while (g_running) {
                    mg_mgr_poll(&mgr, 5);
                }
                mg_mgr_free(&mgr);
                engine_shutdown();
                _exit(0);
            }
            /* Parent continues forking */
        }
    }

    /* Main process (worker 0) also runs event loop */
    fprintf(stderr, "Worker 0 (pid=%d) escutando em %s (%d workers total)\n",
            getpid(), g_listen_url, g_num_workers);

    while (g_running) {
        mg_mgr_poll(&mgr, 5);
    }

    /* Cleanup — children die when parent exits (SIGTERM propagation) */
    if (g_num_workers > 1) {
        /* Send SIGTERM to process group */
        kill(0, SIGTERM);
        /* Reap children */
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
    }

    fprintf(stderr, "Encerrando...\n");
    mg_mgr_free(&mgr);
    engine_shutdown();
    return 0;
}
