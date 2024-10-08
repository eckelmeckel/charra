/* SPDX-License-Identifier: BSD-3-Clause */
/*****************************************************************************
 * Copyright 2019, Fraunhofer Institute for Secure Information Technology SIT.
 * All rights reserved.
 ****************************************************************************/

/**
 * @file verifier.c
 * @author Michael Eckel (michael.eckel@sit.fraunhofer.de)
 * @author Markus Horn (markus.horn@sit.fraunhofer.de)
 * @brief
 * @version 0.1
 * @date 2019-09-19
 *
 * @copyright Copyright 2019, Fraunhofer Institute for Secure Information
 * Technology SIT. All rights reserved.
 *
 * @license BSD 3-Clause "New" or "Revised" License (SPDX-License-Identifier:
 * BSD-3-Clause).
 */

#include <arpa/inet.h>
#include <coap3/coap.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <tss2/tss2_tctildr.h>
#include <tss2/tss2_tpm2_types.h>
#include <unistd.h>

#include "common/charra_log.h"
#include "common/charra_macro.h"
#include "core/charra_key_mgr.h"
#include "core/charra_rim_mgr.h"
#include "core/charra_tap/charra_tap_cbor.h"
#include "core/charra_tap/charra_tap_dto.h"
#include "util/charra_util.h"
#include "util/cli/cli_util_verifier.h"
#include "util/coap_util.h"
#include "util/crypto_util.h"
#include "util/io_util.h"
#include "util/tpm2_tools_util.h"
#include "util/tpm2_util.h"

#define CHARRA_UNUSED __attribute__((unused))

/* --- config ------------------------------------------------------------- */

/* quit signal */
static bool quit = false;
static bool processing_response = false;
static CHARRA_RC attestation_rc = CHARRA_RC_ERROR;

/* logging */
#define LOG_NAME "verifier"
coap_log_t coap_log_level = LOG_INFO;
// #define LOG_LEVEL_CBOR LOG_DEBUG
charra_log_t charra_log_level = CHARRA_LOG_INFO;

/* config */
char dst_host[16] = "127.0.0.1";      // 15 characters for IPv4 plus \0
unsigned int dst_port = 5683;         // default port
#define COAP_IO_PROCESS_TIME_MS 2000  // CoAP IO process time in milliseconds
#define PERIODIC_ATTESTATION_WAIT_TIME_S                                       \
    2  // Wait time between attestations in seconds
static const bool USE_TPM_FOR_RANDOM_NONCE_GENERATION = false;

#define TPM_SIG_KEY_ID_LEN 14
#define TPM_SIG_KEY_ID "PK.RSA.default"
// TODO: Make PCR selection configurable via CLI
// TODO: Implement integration of all PCR banks
static uint8_t tpm_pcr_selection[TPM2_PCR_BANK_COUNT][TPM2_MAX_PCRS] = {
        /* sha1 */
        {0},
        /* sha256 */
        {0, 1, 2, 3, 4, 5, 6, 7, 10},
        /* sha384 */
        {0},
        /* sha512 */
        {0}};
static uint32_t tpm_pcr_selection_len[TPM2_PCR_BANK_COUNT] = {0,  // sha1
        9,                                                        // sha256
        0,                                                        // sha384
        0};                                                       // sha512
uint16_t attestation_response_timeout =
        30;  // timeout when waiting for attestation answer in seconds
char* reference_pcr_file_path = NULL;
char* attestation_public_key_path = NULL;
cli_config_signature_hash_algorithm signature_hash_algorithm = {
        .mbedtls_hash_algorithm = MBEDTLS_MD_SHA256,
        .tpm2_hash_algorithm = TPM2_ALG_SHA256};

// for DTLS-PSK
bool use_dtls_psk = false;
char* dtls_psk_key = "Charra DTLS Key";
char* dtls_psk_identity = "Charra Verifier";

// for DTLS-RPK
bool use_dtls_rpk = false;
char* dtls_rpk_private_key_path = "keys/verifier.der";
char* dtls_rpk_public_key_path = "keys/verifier.pub.der";
char* dtls_rpk_peer_public_key_path = "keys/attester.pub.der";
bool dtls_rpk_verify_peer_public_key = true;

// for PCR logs
uint32_t pcr_log_len = 0;
pcr_log_dto pcr_logs[SUPPORTED_PCR_LOGS_COUNT] = {0};

/* --- function forward declarations -------------------------------------- */

/**
 * @brief SIGINT handler: set quit to 1 for graceful termination.
 *
 * @param signum the signal number.
 */
static void handle_sigint(int signum);

static CHARRA_RC create_attestation_request(
        charra_tap_msg_attestation_request_dto* attestation_request);

static coap_response_t coap_attest_handler(coap_session_t* session,
        const coap_pdu_t* sent, const coap_pdu_t* received,
        const coap_mid_t mid);

/* --- static variables --------------------------------------------------- */

static charra_tap_msg_attestation_request_dto last_request = {0};
static charra_tap_msg_attestation_response_dto last_response = {0};

/* --- main --------------------------------------------------------------- */

int main(int argc, char** argv) {
    CHARRA_RC result = EXIT_FAILURE;

    /* handle SIGINT */
    signal(SIGINT, handle_sigint);

    /* check environment variables */
    charra_log_level_from_str(
            (const char*)getenv("LOG_LEVEL_CHARRA"), &charra_log_level);
    charra_coap_log_level_from_str(
            (const char*)getenv("LOG_LEVEL_COAP"), &coap_log_level);

    /* initialize structures to pass to the CLI parser */
    /* clang-format off */
    cli_config cli_config = {
        .caller = VERIFIER,
        .common_config = {
            .charra_log_level = &charra_log_level,
            .coap_log_level = &coap_log_level,
            .port = &dst_port,
            .use_dtls_psk = &use_dtls_psk,
            .dtls_psk_key = &dtls_psk_key,
            .use_dtls_rpk = &use_dtls_rpk,
            .dtls_rpk_private_key_path =
                    &dtls_rpk_private_key_path,
            .dtls_rpk_public_key_path =
                    &dtls_rpk_public_key_path,
            .dtls_rpk_peer_public_key_path =
                    &dtls_rpk_peer_public_key_path,
            .dtls_rpk_verify_peer_public_key =
                    &dtls_rpk_verify_peer_public_key,
        },
        .specific_config.verifier_config = {
            .dst_host = dst_host,
            .timeout = &attestation_response_timeout,
            .attestation_public_key_path = &attestation_public_key_path,
            .reference_pcr_file_path = &reference_pcr_file_path,
            .tpm_pcr_selection = tpm_pcr_selection,
            .tpm_pcr_selection_len = tpm_pcr_selection_len,
            .dtls_psk_identity = &dtls_psk_identity,
            .signature_hash_algorithm = &signature_hash_algorithm,
            .pcr_log_len = &pcr_log_len,
            .pcr_logs = &pcr_logs
        },
    };
    /* clang-format on */

    /* set log level before parsing CLI to be able to print errors. */
    charra_log_set_level(charra_log_level);
    coap_set_log_level(coap_log_level);

    /* parse CLI arguments */
    if ((result = charra_parse_command_line_verifier_arguments(
                 argc, argv, &cli_config)) != 0) {
        // 1 means help message was displayed (thus exit), -1 means error
        return (result == 1) ? CHARRA_RC_SUCCESS : CHARRA_RC_CLI_ERROR;
    }

    /* set CHARRA and libcoap log levels again in case CLI changed these */
    charra_log_set_level(charra_log_level);
    coap_set_log_level(coap_log_level);

    charra_log_debug("[" LOG_NAME "] Verifier Configuration:");
    charra_log_debug("[" LOG_NAME "]     Destination port: %d", dst_port);
    charra_log_debug("[" LOG_NAME "]     Destination host: %s", dst_host);
    charra_log_debug("[" LOG_NAME
                     "]     Timeout when waiting for attestation response: %ds",
            attestation_response_timeout);
    charra_log_debug("[" LOG_NAME "]     Reference PCR file path: '%s'",
            reference_pcr_file_path);
    charra_log_debug("[" LOG_NAME "]     PCR selection with length %d:",
            tpm_pcr_selection_len);
    charra_log_log_raw(CHARRA_LOG_DEBUG,
            "                                                      ");
    for (uint32_t i = 0; i < tpm_pcr_selection_len[1]; i++) {
        if (i != tpm_pcr_selection_len[1] - 1) {
            charra_log_log_raw(CHARRA_LOG_DEBUG, "%d, ", tpm_pcr_selection[i]);
        } else {
            charra_log_log_raw(CHARRA_LOG_DEBUG, "%d\n", tpm_pcr_selection[i]);
        }
    }
    charra_log_debug("[" LOG_NAME "]     DTLS with PSK enabled: %s",
            (use_dtls_psk == true) ? "true" : "false");
    if (use_dtls_psk) {
        charra_log_debug(
                "[" LOG_NAME "]         Pre-shared key: '%s'", dtls_psk_key);
        charra_log_debug(
                "[" LOG_NAME "]         Identity: '%s'", dtls_psk_identity);
    }
    charra_log_debug("[" LOG_NAME "]     DTLS-RPK enabled: %s",
            (use_dtls_rpk == true) ? "true" : "false");
    if (use_dtls_rpk) {
        charra_log_debug("[" LOG_NAME "]         Private key path: '%s'",
                dtls_rpk_private_key_path);
        charra_log_debug("[" LOG_NAME "]         Public key path: '%s'",
                dtls_rpk_public_key_path);
        charra_log_debug("[" LOG_NAME "]         Peers' public key path: '%s'",
                dtls_rpk_peer_public_key_path);
    }

    /* set varaibles here such that they are valid in case of an 'goto cleanup'
     */
    coap_context_t* coap_context = NULL;
    coap_session_t* coap_session = NULL;
    coap_optlist_t* coap_options = NULL;
    uint8_t* req_buf = NULL;  // TODO(any): make dynamic

    if (use_dtls_psk && use_dtls_rpk) {
        charra_log_error(
                "[" LOG_NAME "] Configuration enables both DTSL with PSK "
                "and DTSL with PKI. Aborting!");
        goto cleanup;
    }

    if (use_dtls_psk || use_dtls_rpk) {
        // print TLS version when in debug mode
        coap_show_tls_version(LOG_DEBUG);
    }

    if (use_dtls_psk && !coap_dtls_is_supported()) {
        charra_log_error("[" LOG_NAME "] CoAP does not support DTLS but the "
                         "configuration enables DTLS. Aborting!");
        goto cleanup;
    }

    /* create CoAP context */

    charra_log_info("[" LOG_NAME "] Initializing CoAP in block-wise mode.");
    if ((coap_context = charra_coap_new_context(true)) == NULL) {
        charra_log_error("[" LOG_NAME "] Cannot create CoAP context.");
        result = CHARRA_RC_COAP_ERROR;
        goto cleanup;
    }

    /* register CoAP response handler */
    charra_log_info("[" LOG_NAME "] Registering CoAP response handler.");
    coap_register_response_handler(coap_context, coap_attest_handler);

    if (use_dtls_psk) {
        charra_log_info("[" LOG_NAME
                        "] Creating CoAP client session using DTLS with PSK.");
        if ((coap_session = charra_coap_new_client_session_psk(coap_context,
                     dst_host, dst_port, COAP_PROTO_DTLS, dtls_psk_identity,
                     (uint8_t*)dtls_psk_key, strlen(dtls_psk_key))) == NULL) {
            charra_log_error(
                    "[" LOG_NAME
                    "] Cannot create client session based on DTLS-PSK.");
            result = CHARRA_RC_ERROR;
            goto cleanup;
        }
    } else if (use_dtls_rpk) {
        charra_log_info(
                "[" LOG_NAME "] Creating CoAP client session using DTLS-RPK.");
        coap_dtls_pki_t dtls_pki = {0};

        result = charra_coap_setup_dtls_pki_for_rpk(&dtls_pki,
                dtls_rpk_private_key_path, dtls_rpk_public_key_path,
                dtls_rpk_peer_public_key_path, dtls_rpk_verify_peer_public_key);
        if (result != CHARRA_RC_SUCCESS) {
            charra_log_error("[" LOG_NAME
                             "] Error while setting up DTLS-RPK structure.");
            goto cleanup;
        }

        if ((coap_session = charra_coap_new_client_session_pki(coap_context,
                     dst_host, dst_port, COAP_PROTO_DTLS, &dtls_pki)) == NULL) {
            charra_log_error(
                    "[" LOG_NAME
                    "] Cannot create client session based on DTLS-RPK.");
            result = CHARRA_RC_ERROR;
            goto cleanup;
        }
    } else {
        charra_log_info(
                "[" LOG_NAME "] Creating CoAP client session using UDP.");
        if ((coap_session = charra_coap_new_client_session(coap_context,
                     dst_host, dst_port, COAP_PROTO_UDP)) == NULL) {
            charra_log_error("[" LOG_NAME
                             "] Cannot create client session based on UDP.");
            result = CHARRA_RC_COAP_ERROR;
            goto cleanup;
        }
    }

    /* define needed variables */
    charra_tap_msg_attestation_request_dto req = {0};
    uint32_t req_buf_len = 0;
    coap_pdu_t* pdu = NULL;
    coap_mid_t mid = COAP_INVALID_MID;
    int coap_io_process_time = -1;

    /* create CoAP option for content type */
    uint8_t coap_mediatype_cbor_buf[4] = {0};
    unsigned int coap_mediatype_cbor_buf_len = 0;
    if ((coap_mediatype_cbor_buf_len = coap_encode_var_safe(
                 coap_mediatype_cbor_buf, sizeof(coap_mediatype_cbor_buf),
                 COAP_MEDIATYPE_APPLICATION_CBOR)) == 0) {
        charra_log_error(
                "[" LOG_NAME "] Cannot create option for CONTENT_TYPE.");
        result = CHARRA_RC_COAP_ERROR;
        goto cleanup;
    }

    /* enter  periodic attestation loop */
    // TODO(any): Enable periodic attestations.
    // charra_log_info("[" LOG_NAME "] Entering periodic attestation loop.");
    // while (!quit) {
    //     /* cleanup */
    //     memset(&req, 0, sizeof(req));
    //     if (coap_options != NULL) {
    //         coap_delete_optlist(coap_options);
    //         coap_options = NULL;
    //     }

    /* create attestation request */
    charra_log_info("[" LOG_NAME "] Creating attestation request.");
    if ((result = create_attestation_request(&req)) != CHARRA_RC_SUCCESS) {
        charra_log_error("[" LOG_NAME "] Cannot create attestation request.");
        goto cleanup;
    } else {
        /* store request data */
        last_request = req;
    }

    /* marshal attestation request */
    charra_log_info(
            "[" LOG_NAME "] Marshaling attestation request data to CBOR.");
    if ((result = charra_tap_marshal_attestation_request(
                 &req, &req_buf_len, &req_buf)) != CHARRA_RC_SUCCESS) {
        charra_log_error(
                "[" LOG_NAME "] Marshaling attestation request data failed.");
        goto cleanup;
    }

    /* CoAP options */
    charra_log_info("[" LOG_NAME "] Adding CoAP option URI_PATH.");
    if (coap_insert_optlist(
                &coap_options, coap_new_optlist(COAP_OPTION_URI_PATH, 6,
                                       (const uint8_t*)"attest")) != 1) {
        charra_log_error("[" LOG_NAME "] Cannot add CoAP option URI_PATH.");
        result = CHARRA_RC_COAP_ERROR;
        goto cleanup;
    }
    charra_log_info("[" LOG_NAME "] Adding CoAP option CONTENT_TYPE.");
    if (coap_insert_optlist(
                &coap_options, coap_new_optlist(COAP_OPTION_CONTENT_TYPE,
                                       coap_mediatype_cbor_buf_len,
                                       coap_mediatype_cbor_buf)) != 1) {
        charra_log_error("[" LOG_NAME "] Cannot add CoAP option CONTENT_TYPE.");
        result = CHARRA_RC_COAP_ERROR;
        goto cleanup;
    }

    /* new CoAP request PDU */
    charra_log_info("[" LOG_NAME "] Creating request PDU.");
    if ((pdu = charra_coap_new_request(coap_session, COAP_MESSAGE_CON,
                 COAP_REQUEST_CODE_FETCH, &coap_options, req_buf,
                 req_buf_len)) == NULL) {
        charra_log_error("[" LOG_NAME "] Cannot create request PDU.");
        result = CHARRA_RC_ERROR;
        goto cleanup;
    }

    /* set timeout length */
    coap_fixed_point_t coap_timeout = {attestation_response_timeout, 0};
    coap_session_set_ack_timeout(coap_session, coap_timeout);

    /* send CoAP PDU */
    charra_log_info("[" LOG_NAME "] Sending CoAP message.");
    if ((mid = coap_send_large(coap_session, pdu)) == COAP_INVALID_MID) {
        charra_log_error("[" LOG_NAME "] Cannot send CoAP message.");
        result = CHARRA_RC_COAP_ERROR;
        goto cleanup;
    }

    /* processing and waiting for response */
    charra_log_info("[" LOG_NAME "] Processing and waiting for response ...");
    uint16_t response_wait_time = 0;
    while (!processing_response && coap_io_pending(coap_context)) {
        /* process CoAP I/O */
        if ((coap_io_process_time = coap_io_process(
                     coap_context, COAP_IO_PROCESS_TIME_MS)) == -1) {
            charra_log_error(
                    "[" LOG_NAME "] Error during CoAP I/O processing.");
            result = CHARRA_RC_COAP_ERROR;
            goto cleanup;
        }
        /* This wait time is not 100% accurate, it only includes the elapsed
         * time inside the coap_io_process function. But should be good enough.
         */
        response_wait_time += coap_io_process_time;
        if (response_wait_time >= (attestation_response_timeout * 1000)) {
            charra_log_error("[" LOG_NAME
                             "] Timeout after %d ms while waiting for or "
                             "processing attestation response.",
                    response_wait_time);
            result = CHARRA_RC_TIMEOUT;
            goto cleanup;
        }
    }

    // normal exit from processing loop, set result to result of attestation
    result = attestation_rc;

    /* wait until next attestation */
    // TODO(any): Enable periodic attestations.
    //     charra_log_info(
    //             "[" LOG_NAME
    //             "] Waiting %d seconds until next attestation request ...",
    //             PERIODIC_ATTESTATION_WAIT_TIME_S);
    //     sleep(PERIODIC_ATTESTATION_WAIT_TIME_S);
    // }

cleanup:
    /* free CoAP memory */
    charra_free_if_not_null_ex(coap_options, coap_delete_optlist);
    charra_free_if_not_null_ex(coap_session, coap_session_release);
    charra_free_if_not_null_ex(coap_context, coap_free_context);

    /* free variables */
    charra_free_if_not_null(req_buf);

    coap_cleanup();

    return result;
}

/* --- function definitions ----------------------------------------------- */

static void handle_sigint(int signum CHARRA_UNUSED) { quit = true; }

static CHARRA_RC create_attestation_request(
        charra_tap_msg_attestation_request_dto* attestation_request) {
    CHARRA_RC err = CHARRA_RC_ERROR;

    /* generate nonce */
    const uint32_t nonce_len = 20;
    uint8_t nonce[nonce_len];
    if (USE_TPM_FOR_RANDOM_NONCE_GENERATION) {
        if ((err = charra_random_bytes_from_tpm(nonce_len, nonce) !=
                    CHARRA_RC_SUCCESS)) {
            charra_log_error("Could not get random bytes from TPM for nonce.");
            return err;
        }
    } else {
        if ((err = charra_random_bytes(nonce_len, nonce) !=
                    CHARRA_RC_SUCCESS)) {
            charra_log_error("Could not get random bytes for nonce.");
            return err;
        }
    }
    charra_log_info("[" LOG_NAME
                    "] Generated random qualifying data (nonce) of length %d:",
            nonce_len);
    charra_print_hex(CHARRA_LOG_INFO, nonce_len, nonce,
            "                                              0x", "\n", false);

    /* build attestation request */
    charra_tap_msg_attestation_request_dto req = {
            .tap_spec_version = CHARRA_TAP_SPEC_VERSION,
            .hello = false,
            .sig_key_id_len = TPM_SIG_KEY_ID_LEN,
            .sig_key_id = {0},  // must be memcpy'd, see below
            .nonce_len = nonce_len,
            .nonce = {0},  // must be memcpy'd, see below
            .pcr_selections_len = 1,
            .pcr_selections = {{
                    .tcg_hash_alg_id = TPM2_ALG_SHA256,
                    /* TODO: add other PCR banks */
                    .pcrs_len = tpm_pcr_selection_len[1],
                    .pcrs = {0}  // must be memcpy'd, see below
            }},
            .pcr_log_len = pcr_log_len,
            .pcr_logs = pcr_logs,
    };
    memcpy(req.sig_key_id, TPM_SIG_KEY_ID, TPM_SIG_KEY_ID_LEN);
    memcpy(req.nonce, nonce, nonce_len);
    /* TODO: add other PCR banks */
    memcpy(req.pcr_selections->pcrs, tpm_pcr_selection[1],
            tpm_pcr_selection_len[1]);

    /* set output param(s) */
    *attestation_request = req;

    /* return result */
    return CHARRA_RC_SUCCESS;
}

/* --- resource handler definitions --------------------------------------- */

static coap_response_t coap_attest_handler(
        coap_session_t* session CHARRA_UNUSED,
        const coap_pdu_t* sent CHARRA_UNUSED, const coap_pdu_t* received,
        const coap_mid_t mid CHARRA_UNUSED) {
    int coap_r = 0;
    TSS2_RC tss_r = 0;

    ESYS_TR sig_key_handle = ESYS_TR_NONE;
    TPMT_TK_VERIFIED* validation = NULL;

    processing_response = true;

    charra_log_info(
            "[" LOG_NAME "] Resource '%s': Received message.", "attest");
    coap_show_pdu(LOG_DEBUG, received);

    /* --- receive incoming data --- */

    /* get data */
    size_t data_len = 0;
    const uint8_t* data = NULL;
    size_t data_offset = 0;
    size_t data_total_len = 0;
    if ((coap_r = coap_get_data_large(received, &data_len, &data, &data_offset,
                 &data_total_len)) == 0) {
        charra_log_error("[" LOG_NAME "] Could not get CoAP PDU data.");
        attestation_rc = CHARRA_RC_ERROR;
        goto cleanup;
    } else {
        charra_log_info(
                "[" LOG_NAME "] Received data of length %zu.", data_len);
        charra_log_info("[" LOG_NAME "] Received data of total length %zu.",
                data_total_len);
    }

    /* unmarshal data */
    charra_log_info("[" LOG_NAME "] Parsing received CBOR data.");
    charra_tap_msg_attestation_response_dto res = {0};
    if ((attestation_rc = charra_tap_unmarshal_attestation_response(
                 data_len, data, &res)) != CHARRA_RC_SUCCESS) {
        charra_log_error("[" LOG_NAME "] Could not parse CBOR data.");
        goto cleanup;
    }

    /* store last response */
    last_response = res;

    /* verify data */
    if (res.tpm2_quote.attestation_data_len > sizeof(TPM2B_ATTEST)) {
        charra_log_error(
                "[" LOG_NAME
                "] Length of attestation data exceeds maximum allowed size.");
        attestation_rc = CHARRA_RC_ERROR;
        goto cleanup;
    }
    if (res.tpm2_quote.tpm2_signature_len > sizeof(TPMT_SIGNATURE)) {
        charra_log_error("[" LOG_NAME
                         "] Length of signature exceeds maximum allowed size.");
        attestation_rc = CHARRA_RC_ERROR;
        goto cleanup;
    }

    /* --- verify TPM Quote --- */
    charra_log_info("[" LOG_NAME "] Starting verification.");

    /* initialize ESAPI */
    ESYS_CONTEXT* esys_ctx = NULL;
    TSS2_TCTI_CONTEXT* tcti_ctx = NULL;
    if ((tss_r = Tss2_TctiLdr_Initialize(getenv("CHARRA_TCTI"), &tcti_ctx)) !=
            TSS2_RC_SUCCESS) {
        charra_log_error("[" LOG_NAME "] Tss2_TctiLdr_Initialize.");
        attestation_rc = CHARRA_RC_ERROR;
        goto cleanup;
    }
    if ((tss_r = Esys_Initialize(&esys_ctx, tcti_ctx, NULL)) !=
            TSS2_RC_SUCCESS) {
        charra_log_error("[" LOG_NAME "] Esys_Initialize.");
        attestation_rc = CHARRA_RC_ERROR;
        goto cleanup;
    }

    /* load TPM key */
    TPM2B_PUBLIC tpm2_public_key = {0};  // (TPM2B_PUBLIC*)res.tpm2_public_key;
    if ((attestation_rc = charra_load_external_public_key(esys_ctx,
                 &tpm2_public_key, &sig_key_handle,
                 attestation_public_key_path)) != CHARRA_RC_SUCCESS) {
        charra_log_error("[" LOG_NAME "] Loading external public key failed.");
        goto cleanup;
    } else {
        charra_log_info("[" LOG_NAME "] External public key loaded.");
    }

    /* prepare verification */
    charra_log_info("[" LOG_NAME "] Preparing TPM2 Quote verification.");
    TPM2B_ATTEST attest = {0};
    attest.size = res.tpm2_quote.attestation_data_len;
    memcpy(attest.attestationData, res.tpm2_quote.attestation_data,
            res.tpm2_quote.attestation_data_len);
    TPMT_SIGNATURE signature = {0};
    memcpy(&signature, res.tpm2_quote.tpm2_signature,
            res.tpm2_quote.tpm2_signature_len);

    /* --- verify attestation signature --- */
    bool attestation_result_signature = false;
    {
        charra_log_info(
                "[" LOG_NAME "] Verifying TPM2 Quote signature with TPM ...");
        /* verify attestation signature with TPM */
        if ((attestation_rc = charra_verify_tpm2_quote_signature_with_tpm(
                     esys_ctx, sig_key_handle,
                     signature_hash_algorithm.tpm2_hash_algorithm, &attest,
                     &signature, &validation)) == CHARRA_RC_SUCCESS) {
            charra_log_info(
                    "[" LOG_NAME "]     => TPM2 Quote signature is valid!");
            attestation_result_signature = true;
        } else {
            charra_log_error(
                    "[" LOG_NAME "]     => TPM2 Quote signature is NOT valid!");
        }
    }
    {
        /* convert TPM public key to mbedTLS public key */
        charra_log_info(
                "[" LOG_NAME
                "] Converting TPM2 public key to mbedTLS public key ...");
        mbedtls_rsa_context mbedtls_rsa_pub_key = {0};
        if ((attestation_rc = charra_crypto_tpm_pub_key_to_mbedtls_pub_key(
                     &tpm2_public_key, &mbedtls_rsa_pub_key)) !=
                CHARRA_RC_SUCCESS) {
            charra_log_error("[" LOG_NAME "] mbedTLS RSA error");
            goto cleanup;
        }

        /* verify attestation signature with mbedTLS */
        charra_log_info("[" LOG_NAME
                        "] Verifying TPM2 Quote signature with mbedTLS ...");
        if ((attestation_rc = charra_crypto_rsa_verify_signature(
                     &mbedtls_rsa_pub_key,
                     signature_hash_algorithm.mbedtls_hash_algorithm,
                     res.tpm2_quote.attestation_data,
                     (size_t)res.tpm2_quote.attestation_data_len,
                     signature.signature.rsapss.sig.buffer,
                     &tpm2_public_key)) == CHARRA_RC_SUCCESS) {
            charra_log_info(
                    "[" LOG_NAME "]     => TPM2 Quote signature is valid!");
        } else {
            charra_log_error(
                    "[" LOG_NAME "]     => TPM2 Quote signature is NOT valid!");
        }
        mbedtls_rsa_free(&mbedtls_rsa_pub_key);
    }

    /* unmarshal attestation data */
    TPMS_ATTEST attest_struct = {0};
    attestation_rc =
            charra_unmarshal_tpm2_quote(res.tpm2_quote.attestation_data_len,
                    res.tpm2_quote.attestation_data, &attest_struct);
    if (attestation_rc != CHARRA_RC_SUCCESS) {
        charra_log_error("[" LOG_NAME "] Error while unmarshaling TPM2 Quote.");
        goto cleanup;
    }

    /* --- verify TPM magic --- */
    bool attestation_result_tpm2_magic = false;
    {
        charra_log_info("[" LOG_NAME "] Verifying TPM magic ...");

        attestation_result_tpm2_magic =
                charra_verify_tpm2_magic(&attest_struct);
        if (attestation_result_tpm2_magic == true) {
            charra_log_info("[" LOG_NAME "]     =>  TPM2 magic is valid!");
        } else {
            charra_log_error("[" LOG_NAME "]     => TPM2 magic is NOT valid! "
                             "This might be a bogus TPM2 Quote!");
        }
    }

    /* --- verify qualifying data (nonce) --- */
    bool attestation_result_nonce = false;
    {
        charra_log_info("[" LOG_NAME "] Verifying qualifying data (nonce) ...");

        attestation_result_nonce = charra_verify_tpm2_quote_qualifying_data(
                last_request.nonce_len, last_request.nonce, &attest_struct);
        if (attestation_result_nonce == true) {
            charra_log_info(
                    "[" LOG_NAME "]     => Qualifying data (nonce) in TPM2 "
                    "Quote is valid (matches the one sent)!");
        } else {
            charra_log_error(
                    "[" LOG_NAME
                    "]     => Qualifying data (nonce) in TPM2 Quote is "
                    "NOT valid (does not match the one sent)!");
        }
    }

    /* --- verify PCRs --- */
    bool attestation_result_pcrs = false;
    {
        charra_log_info("[" LOG_NAME "] Verifying PCRs ...");

        charra_log_info("[" LOG_NAME
                        "] Actual PCR composite digest from TPM2 Quote is:");
        charra_print_hex(CHARRA_LOG_INFO,
                attest_struct.attested.quote.pcrDigest.size,
                attest_struct.attested.quote.pcrDigest.buffer,
                "                                              0x", "\n",
                false);
        /* TODO: add support for other hash algorithms */
        CHARRA_RC pcr_check = charra_check_pcr_digest_against_reference(
                reference_pcr_file_path, tpm_pcr_selection[1],
                tpm_pcr_selection_len[1], &attest_struct);
        if (pcr_check == CHARRA_RC_SUCCESS) {
            charra_log_info(
                    "[" LOG_NAME "]     => PCR composite digest is valid!");
            attestation_result_pcrs = true;
        } else {
            charra_log_error(
                    "[" LOG_NAME
                    "]     => PCR composite digest is NOT valid! (does "
                    "not match any of the digests from the set of reference "
                    "PCRs)");
        }
    }

    /* check pcr logs */
    if (res.pcr_log_len == 0) {
        charra_log_info("[" LOG_NAME "] No PCR logs received.");
    }

    for (uint32_t i = 0; i < res.pcr_log_len; i++) {
        charra_log_info("[" LOG_NAME "] Received PCR log %s [%lu Bytes]",
                pcr_logs[i].identifier, res.pcr_logs[i].content_len);
    }

    // TODO(any): Implement real verification.

    /* --- output result --- */

    bool attestation_result = attestation_result_signature &&
                              attestation_result_nonce &&
                              attestation_result_pcrs;

    /* print attestation result */
    charra_log_info("[" LOG_NAME "] +----------------------------+");
    if (attestation_result) {
        attestation_rc = CHARRA_RC_SUCCESS;
        charra_log_info("[" LOG_NAME "] |   ATTESTATION SUCCESSFUL   |");
    } else {
        attestation_rc = CHARRA_RC_VERIFICATION_FAILED;
        charra_log_info("[" LOG_NAME "] |     ATTESTATION FAILED     |");
    }
    charra_log_info("[" LOG_NAME "] +----------------------------+");

cleanup:
    /* free heap objects*/
    for (uint32_t i = 0; i < res.pcr_log_len; i++) {
        charra_free_if_not_null(res.pcr_logs[i].content);
        charra_free_if_not_null(res.pcr_logs[i].identifier);
    }
    charra_free_if_not_null(res.pcr_logs);

    /* flush handles */
    if (sig_key_handle != ESYS_TR_NONE) {
        if (Esys_FlushContext(esys_ctx, sig_key_handle) != TSS2_RC_SUCCESS) {
            charra_log_error(
                    "[" LOG_NAME "] TSS cleanup sig_key_handle failed.");
        }
    }

    /* free event log */
    // TODO(any): Provide function charra_free_msg_attestation_response_dto().

    /* free ESAPI objects */
    if (validation != NULL) {
        Esys_Free(validation);
    }

    /* finalize ESAPI & TCTI */
    if (esys_ctx != NULL) {
        Esys_Finalize(&esys_ctx);
    }
    if (tcti_ctx != NULL) {
        Tss2_TctiLdr_Finalize(&tcti_ctx);
    }

    processing_response = false;
    return COAP_RESPONSE_OK;
}
