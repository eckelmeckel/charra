/* SPDX-License-Identifier: BSD-3-Clause */
/*****************************************************************************
 * Copyright 2024, Fraunhofer Institute for Secure Information Technology SIT.
 * All rights reserved.
 ****************************************************************************/

/**
 * @file cli_util_common.h
 * @author Markus Horn (markus.horn@sit.fraunhofer.de)
 * @author Dominik Lorych (dominik.lorych@sit.fraunhofer.de)
 * @brief Provides command line parsing for verifier & attester.
 * @version 0.1
 * @date 2024-04-22
 *
 * @copyright Copyright 2021, Fraunhofer Institute for Secure Information
 * Technology SIT. All rights reserved.
 *
 * @license BSD 3-Clause "New" or "Revised" License (SPDX-License-Identifier:
 * BSD-3-Clause).
 */

#ifndef CLI_UTIL_COMMON_H
#define CLI_UTIL_COMMON_H

#include "../../common/charra_log.h"
#include <coap3/coap.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <tss2/tss2_esys.h>

typedef enum {
    VERIFIER,
    ATTESTER,
} cli_parser_caller;

/**
 * A structure holding pointers to common variables of attester and verifier
 * which might geht modified by the CLI parser
 */
typedef struct {
    charra_log_t* charra_log_level;
    coap_log_t* coap_log_level;
    unsigned int* port;
    bool* use_dtls_psk;
    char** dtls_psk_key;
    bool* use_dtls_rpk;
    char** dtls_rpk_private_key_path;
    char** dtls_rpk_public_key_path;
    char** dtls_rpk_peer_public_key_path;
    bool* dtls_rpk_verify_peer_public_key;
} cli_config_common;

/**
 * An enum containing the possible formats of the attestation key.
 */
typedef enum {
    CLI_UTIL_ATTESTATION_KEY_FORMAT_FILE = 'f',
    CLI_UTIL_ATTESTATION_KEY_FORMAT_HANDLE = 'h',
    CLI_UTIL_ATTESTATION_KEY_FORMAT_UNKNOWN = '0',
} cli_config_attester_attestation_key_format_e;

/**
 * A structure holding pointers to variables of the attester
 * which might geht modified by the CLI parser
 */
typedef struct {
    char** dtls_psk_hint;
    cli_config_attester_attestation_key_format_e attestation_key_format;
    union {
        char* ctx_path;
        ESYS_TR tpm2_handle;
    } attestation_key;
} cli_config_attester;

/**
 * A structure holding pointers to variables of the verifier
 * which might geht modified by the CLI parser
 */
typedef struct {
    char* dst_host;
    uint16_t* timeout;
    char** attestation_public_key_path;
    char** reference_pcr_file_path;
    uint8_t* tpm_pcr_selection;
    uint32_t* tpm_pcr_selection_len;
    bool* use_ima_event_log;
    char** ima_event_log_path;
    char** dtls_psk_identity;
} cli_config_verifier;

/**
 * A structure holding the pointers to all config parameters which might get
 * modified by the CLI parser
 */
typedef struct {
    cli_parser_caller caller;
    cli_config_common common_config;
    union {
        cli_config_attester attester_config;
        cli_config_verifier verifier_config;
    } specific_config;
} cli_config;

/**
 * @brief Parses an option as an unsigned long
 *
 * @param[in] option The option to parse
 * @param[in] base The base to use
 * @param[out] value The parsed value
 * @return 0 on success, -1 on error
 */
int cli_util_common_parse_option_as_ulong(
        const char* const option, int base, uint64_t* value);

/**
 * @brief Splits an option string into format and value
 *
 * @param[in] option The option string
 * @param[out] format The format
 * @param[out] value The value
 * @return 0 on success, -1 on error
 */
int cli_util_common_split_option_string(
        char* option, char** format, char** value);

/**
 * @brief Creates an option array which combines cli common options and specific
 * options. Combined_options has to be freed by the caller.
 *
 * @param[out] combined_options Array containing the combined options
 * @param[in] specific_options Array containing the specific options
 * @param[in] specific_option_length The length of the specific options array
 * @param[in] log_name The log_name of the caller
 * @return 0 on success, -1 on error
 */
int cli_util_common_get_combined_option_array(struct option** combined_options,
        const struct option* const specific_options,
        const size_t specific_option_length, const char* const log_name);

/**
 * @brief Parses a single command line argument
 *
 * @param[in] identifier The identifier of the argument
 * @param[in,out] variables A struct holding a caller identifier and pointers to
 * config variables which might get modified depending on the CLI
 * @param[in] log_name The log_name of the caller
 * @param[in] print_specific_help_message The function to print the help message
 * of the specific options
 * @return 0 on success, -1 on parse error, 1 when help message was displayed
 */
int cli_util_common_parse_command_line_argument(const int identifier,
        const cli_config* variables, const char* const log_name,
        void (*print_specific_help_message)(const cli_config* const variables));

#endif /* CLI_UTIL_COMMON_H */
