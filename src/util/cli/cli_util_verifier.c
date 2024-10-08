/* SPDX-License-Identifier: BSD-3-Clause */
/*****************************************************************************
 * Copyright 2024, Fraunhofer Institute for Secure Information Technology SIT.
 * All rights reserved.
 ****************************************************************************/

/**
 * @file cli_util_verifier.c
 * @author Markus Horn (markus.horn@sit.fraunhofer.de)
 * @author Dominik Lorych (dominik.lorych@sit.fraunhofer.de)
 * @brief Provides command line parsing for verifier.
 * @version 0.1
 * @date 2024-04-22
 *
 * @copyright Copyright 2024, Fraunhofer Institute for Secure Information
 * Technology SIT. All rights reserved.
 *
 * @license BSD 3-Clause "New" or "Revised" License (SPDX-License-Identifier:
 * BSD-3-Clause).
 */

#include "cli_util_verifier.h"
#include "../io_util.h"
#include "cli_util_common.h"
#include <bits/getopt_core.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_NAME "verifier"
#define VERIFIER_SHORT_OPTIONS "vl:c:t:f:s:pk:i:rg:"

#define CLI_VERIFIER_PSK_IDENTITY_LONG "psk-identity"
#define CLI_VERIFIER_IP_LONG "ip"
#define CLI_VERIFIER_TIMEOUT_LONG "timeout"
#define CLI_VERIFIER_ATTESTATION_PUBLIC_KEY_LONG "attestation-public-key"
#define CLI_VERIFIER_PCR_FILE_LONG "pcr-file"
#define CLI_VERIFIER_PCR_SELECTION_LONG "pcr-selection"
#define CLI_VERIFIER_HASH_ALGORITHM_LONG "hash-algorithm"

typedef enum {
    CLI_VERIFIER_PSK_IDENTITY = 'i',
    CLI_VERIFIER_IP = 'a',
    CLI_VERIFIER_TIMEOUT = 't',
    CLI_VERIFIER_ATTESTATION_PUBLIC_KEY = '6',
    CLI_VERIFIER_PCR_FILE = 'f',
    CLI_VERIFIER_PCR_SELECTION = 's',
    CLI_VERIFIER_HASH_ALGORITHM = 'g',
} cli_util_verifier_args_e;

static const struct option verifier_options[] = {
        /* common options */
        {CLI_COMMON_VERBOSE_LONG, no_argument, 0, CLI_COMMON_VERBOSE},
        {CLI_COMMON_LOG_LEVEL_LONG, required_argument, 0, CLI_COMMON_LOG_LEVEL},
        {CLI_COMMON_COAP_LOG_LEVEL_LONG, required_argument, 0,
                CLI_COMMON_COAP_LOG_LEVEL},
        {CLI_COMMON_HELP_LONG, no_argument, 0, CLI_COMMON_HELP},
        /* port only has a specific help message */
        {CLI_COMMON_PORT_LONG, required_argument, 0, CLI_COMMON_PORT},
        /* pcr-log has only the same name */
        {CLI_COMMON_PCR_LOG_LONG, required_argument, 0, CLI_COMMON_PCR_LOG},
        /* common rpk group-options */
        {CLI_COMMON_RPK_LONG, no_argument, 0, CLI_COMMON_RPK},
        {CLI_COMMON_RPK_PRIVATE_KEY_LONG, required_argument, 0,
                CLI_COMMON_RPK_PRIVATE_KEY},
        {CLI_COMMON_RPK_PUBLIC_KEY_LONG, required_argument, 0,
                CLI_COMMON_RPK_PUBLIC_KEY},
        {CLI_COMMON_RPK_PEER_PUBLIC_KEY_LONG, required_argument, 0,
                CLI_COMMON_RPK_PEER_PUBLIC_KEY},
        {CLI_COMMON_RPK_VERIFY_PEER_LONG, required_argument, 0,
                CLI_COMMON_RPK_VERIFY_PEER},
        /* common psk group-options (they have specific help messages) */
        {CLI_COMMON_PSK_LONG, no_argument, 0, CLI_COMMON_PSK},
        {CLI_COMMON_PSK_KEY_LONG, required_argument, 0, CLI_COMMON_PSK_KEY},

        /* verifier specific psk group-options */
        {CLI_VERIFIER_PSK_IDENTITY_LONG, required_argument, 0,
                CLI_VERIFIER_PSK_IDENTITY},
        /* verifier specific options */
        {CLI_VERIFIER_IP_LONG, required_argument, 0, CLI_VERIFIER_IP},
        {CLI_VERIFIER_TIMEOUT_LONG, required_argument, 0, CLI_VERIFIER_TIMEOUT},
        {CLI_VERIFIER_ATTESTATION_PUBLIC_KEY_LONG, required_argument, 0,
                CLI_VERIFIER_ATTESTATION_PUBLIC_KEY},
        {CLI_VERIFIER_PCR_FILE_LONG, required_argument, 0,
                CLI_VERIFIER_PCR_FILE},
        {CLI_VERIFIER_PCR_SELECTION_LONG, required_argument, 0,
                CLI_VERIFIER_PCR_SELECTION},
        {CLI_VERIFIER_HASH_ALGORITHM_LONG, required_argument, 0,
                CLI_VERIFIER_HASH_ALGORITHM},
        {0}};

/**
 * @brief Checks whether all required options have been specified.
 *
 * @param caller the cli parser caller
 * @param LOG_NAME the log name
 * @param variables the cli config variables
 */
static int charra_check_required_options(const cli_config* const variables) {
    /* check if PCR reference file was specified */
    if (*(variables->specific_config.verifier_config.reference_pcr_file_path) ==
            NULL) {
        charra_log_error("[%s] ERROR: no PCR reference file", LOG_NAME);
        return -1;
    }
    /* check if attestation-public-key file was specified */
    if (*(variables->specific_config.verifier_config
                        .attestation_public_key_path) == NULL) {
        charra_log_error(
                "[%s] ERROR: no attestation public key file", LOG_NAME);
        return -1;
    }
    return 0;
}

static void charra_print_verifier_help_message(
        const cli_config* const variables) {
    /* print specific verifier options */
    printf("     --%s=IP:                    Connect to IP instead "
           "of doing the attestation on localhost.\n",
            CLI_VERIFIER_IP_LONG);
    printf("     --%s=PORT:                Connect to PORT "
           "instead of default port %u.\n",
            CLI_COMMON_PORT_LONG, *(variables->common_config.port));
    printf(" -%c, --%s=SECONDS:          Wait up to SECONDS "
           "for the attestation answer. Default is %d seconds.\n",
            CLI_VERIFIER_TIMEOUT, CLI_VERIFIER_TIMEOUT_LONG,
            *(variables->specific_config.verifier_config.timeout));
    printf("     --%s=PATH:      Specifies the path to "
           "the public portion of the attestation key.\n",
            CLI_VERIFIER_ATTESTATION_PUBLIC_KEY_LONG);
    printf(" -%c, --%s=FORMAT:PATH:     Read reference PCRs "
           "from PATH in a specified FORMAT. Available is: "
           "yaml.\n",
            CLI_VERIFIER_PCR_FILE, CLI_VERIFIER_PCR_FILE_LONG);
    printf(" -%c, --%s=X1[+X2...]: Specifies which PCRs "
           "to check on the attester. Each X refers to a PCR bank that "
           "begins with the algorithm, followed by a ':' and a comma-separated "
           "list of PCRs. \n"
           "                                 Each PCR bank is separated "
           "by a '+'. ",
            CLI_VERIFIER_PCR_SELECTION, CLI_VERIFIER_PCR_SELECTION_LONG);
    printf("By default these PCRs are checked: sha256:");
    const uint32_t tpm_pcr_selection_len =
            variables->specific_config.verifier_config.tpm_pcr_selection_len[1];
    for (uint32_t i = 0; i < tpm_pcr_selection_len; i++) {
        printf("%d", variables->specific_config.verifier_config
                             .tpm_pcr_selection[1][i]);
        if (i != variables->specific_config.verifier_config
                                 .tpm_pcr_selection_len[1] -
                         1) {
            printf(",");
        }
    }

    printf("\n");
    printf("     --%s=FORMAT:Start,Count: Specifies the desired PCR log "
           "format with a starting index and the number of logs. If 'Start' is "
           "0, an empty log is requested. If 'Count' is 0, all logs beginning "
           "with 'Start' are requested.\n"
           "                                 Available formats are: ima, "
           "tcg-boot.\n",
            CLI_COMMON_PCR_LOG_LONG);
    printf(" -%c, --%s=ALGORITHM: The hash algorithm used to digest "
           "the tpm quote.\n",
            CLI_VERIFIER_HASH_ALGORITHM, CLI_VERIFIER_HASH_ALGORITHM_LONG);

    /* print DTLS-PSK grouped options */
    printf("DTLS-PSK Options:\n");
    printf(" -%c, --%s:                      Enable DTLS protocol "
           "with PSK. By default the key '%s' and identity '%s' "
           "are used.\n",
            CLI_COMMON_PSK, CLI_COMMON_PSK_LONG,
            *variables->common_config.dtls_psk_key,
            *variables->specific_config.verifier_config.dtls_psk_identity);
    printf(" -%c, --%s=KEY:              Use KEY as pre-shared "
           "key for DTLS-PSK. Implicitly enables DTLS-PSK.\n",
            CLI_COMMON_PSK_KEY, CLI_COMMON_PSK_KEY_LONG);
    printf(" -%c, --%s=IDENTITY:    Use IDENTITY as "
           "identity for DTLS. Implicitly enables DTLS-PSK.\n",
            CLI_VERIFIER_PSK_IDENTITY, CLI_VERIFIER_PSK_IDENTITY_LONG);
}

static int charra_parse_pcr_log_start_count(
        char* const value, uint64_t* start, uint64_t* count) {
    char* number1 = NULL;
    char* number2 = NULL;
    number1 = strtok(value, ",");
    number2 = strtok(NULL, ",");
    /* check if there is a comma */
    if (number2 == NULL) {
        return -1;
    }
    /* parse start and count */
    if (charra_cli_util_common_parse_option_as_ulong(number1, 10, start) != 0) {
        return -1;
    }
    if (charra_cli_util_common_parse_option_as_ulong(number2, 10, count) != 0) {
        return -1;
    }
    return 0;
}

static int charra_check_pcr_log_format(const char* const format) {
    if (strcmp(format, "ima") == 0 || strcmp(format, "tcg-boot") == 0) {
        return 0;
    }
    return -1;
}

static uint32_t charra_calculate_index_and_update_length(
        cli_config* const variables, const char* const identifier) {
    size_t len = strlen(identifier);
    uint32_t index = 0;
    pcr_log_dto* pcr_logs =
            *(variables->specific_config.verifier_config.pcr_logs);
    uint32_t* pcr_log_len =
            variables->specific_config.verifier_config.pcr_log_len;
    while (index < SUPPORTED_PCR_LOGS_COUNT) {
        /* identifier is new */
        if (pcr_logs[index].identifier == NULL) {
            *pcr_log_len = (*pcr_log_len) + 1;
            break;
        }
        /* identifier is already in the list and should be overridden */
        if (strncmp(identifier, pcr_logs[index].identifier, len) == 0) {
            break;
        }
        index++;
    }
    return index;
}

static int charra_cli_verifier_pcr_log(cli_config* const variables) {
    char* format = NULL;
    char* value = NULL;
    uint64_t start = 0;
    uint64_t count = 0;
    if (charra_cli_util_common_split_option_string(optarg, &format, &value) !=
            0) {
        charra_log_error("[%s] Argument syntax error: please use "
                         "'--%s=FORMAT:START,COUNT'",
                LOG_NAME, CLI_COMMON_PCR_LOG_LONG);
        return -1;
    }
    if (charra_parse_pcr_log_start_count(value, &start, &count) != 0) {
        charra_log_error("[%s] Argument syntax error: please use "
                         "'--%s=FORMAT:START,COUNT'",
                LOG_NAME, CLI_COMMON_PCR_LOG_LONG);
        return -1;
    }
    if (charra_check_pcr_log_format(format) != 0) {
        charra_log_error("[%s] Unknown format '%s'", LOG_NAME, format);
        return -1;
    }
    uint32_t index =
            charra_calculate_index_and_update_length(variables, format);
    if (index >= SUPPORTED_PCR_LOGS_COUNT) {
        charra_log_error(
                "[%s] Too many pcr logs. This should never happen.", LOG_NAME);
        return -1;
    }
    pcr_log_dto* pcr_logs =
            *(variables->specific_config.verifier_config.pcr_logs);
    pcr_logs[index].identifier = format;
    pcr_logs[index].start = start;
    pcr_logs[index].count = count;
    return 0;
}

static void charra_cli_verifier_identity(cli_config* const variables) {
    *variables->common_config.use_dtls_psk = true;
    *(variables->specific_config.verifier_config.dtls_psk_identity) = optarg;
}

static int charra_cli_verifier_ip(cli_config* const variables) {
    int argument_length = strlen(optarg);
    if (argument_length > 15) {
        charra_log_error("[%s] Error while parsing '--%s': Input too long "
                         "for IPv4 address",
                LOG_NAME, CLI_VERIFIER_IP_LONG);
        return -1;
    }
    strncpy(variables->specific_config.verifier_config.dst_host, optarg, 16);
    return 0;
}

static int charra_cli_verifier_timeout(cli_config* const variables) {
    char* end;
    *(variables->specific_config.verifier_config.timeout) =
            (uint16_t)strtoul(optarg, &end, 10);
    if (*(variables->specific_config.verifier_config.timeout) == 0 ||
            end == optarg) {
        charra_log_error("[%s] Error while parsing '--%s': Port "
                         "could not be parsed",
                LOG_NAME, CLI_COMMON_PORT_LONG);
        return -1;
    }
    return 0;
}

static int charra_cli_verifier_attestation_public_key(
        cli_config* const variables) {
    if (charra_io_file_exists(optarg) != CHARRA_RC_SUCCESS) {
        charra_log_error("[%s] Attestation key: file '%s' does not exist.",
                LOG_NAME, optarg);
        return -1;
    }
    *(variables->specific_config.verifier_config.attestation_public_key_path) =
            optarg;
    return 0;
}

static int charra_cli_verifier_pcr_file(cli_config* const variables) {
    char* token = NULL;
    const char delimiter[] = ":";
    char* format = NULL;
    char* path = NULL;

    /* get the token representing the file format */
    token = strtok(optarg, delimiter);
    format = token;

    /* get the token representing the path of the file */
    token = strtok(NULL, delimiter);
    /* check if there is a delimiter */
    if (token == NULL) {
        charra_log_error("[%s] Argument syntax error: please use "
                         "'--%s=FORMAT:PATH'",
                LOG_NAME, CLI_VERIFIER_PCR_FILE_LONG);
        return -1;
    }
    path = token;

    /* check if format is valid */
    if (strcmp(format, "yaml") != 0) {
        charra_log_error(
                "[%s] File format '%s' is not supported.", LOG_NAME, format);
        return -1;
    }
    /* check if file exists */
    if (charra_io_file_exists(path) == CHARRA_RC_SUCCESS) {
        *(variables->specific_config.verifier_config.reference_pcr_file_path) =
                path;
        return 0;
    } else {
        charra_log_error(
                "[%s] Reference PCR file '%s' does not exist.", LOG_NAME, path);
        return -1;
    }
}

static char* charra_cli_verifier_strtok(
        char* str, const char* const delim, char** saveptr) {
    char* token = NULL;

    if (str == NULL) {
        /* get string from saveptr */
        str = *saveptr;
    }

    if (*str == '\0') {
        /* check if string reached its end */
        *saveptr = str;
        return NULL;
    }

    token = str;
    /* get first character which is part of the delimiter */
    str += strcspn(token, delim);
    if (*str != '\0') {
        /* setup next token */
        *str = '\0';
        *saveptr = str + 1;
    } else {
        /* there is no more token */
        *saveptr = str;
    }

    return token;
}

static int charra_cli_verifier_parse_pcr_bank(uint8_t* tpm_pcr_selection_bank,
        uint32_t* tpm_pcr_selection_len, char* pcr_list) {
    if (strcmp(pcr_list, "all") == 0) {
        for (uint8_t i = 0; i < TPM2_MAX_PCRS; i++) {
            tpm_pcr_selection_bank[i] = true;
        }
        *tpm_pcr_selection_len = TPM2_MAX_PCRS;
        return 0;
    }
    char* pcr_token = NULL;
    char* next_token = pcr_list;
    uint8_t tpm_pcr_selection_hash_set[TPM2_MAX_PCRS] = {0};
    uint8_t pcr = 0;
    uint64_t parse_value = 0;
    /* fill the hash_set with  */
    while ((pcr_token = charra_cli_verifier_strtok(NULL, ",", &next_token)) !=
            NULL) {
        if (charra_cli_util_common_parse_option_as_ulong(
                    pcr_token, 10, &parse_value) != 0) {
            charra_log_error("[%s] Could not parse '%s'.", LOG_NAME, pcr_token);
            return -1;
        }
        if (parse_value >= TPM2_MAX_PCRS) {
            charra_log_error(
                    "[%s] Unsupported handle '%s'.", LOG_NAME, pcr_token);
            return -1;
        }
        pcr = (uint8_t)parse_value;
        tpm_pcr_selection_hash_set[pcr] = true;
    }
    /* add hash set values sorted into tpm_pcr_selection_bank */
    *tpm_pcr_selection_len = 0;
    for (uint8_t i = 0; i < TPM2_MAX_PCRS; i++) {
        if (tpm_pcr_selection_hash_set[i] == 0) {
            continue;
        }
        tpm_pcr_selection_bank[*tpm_pcr_selection_len] = i;
        *tpm_pcr_selection_len = *tpm_pcr_selection_len + 1;
    }
    return 0;
}

static int charra_cli_verifier_parse_pcr_bank_to_index(
        const char* const pcr_bank) {
    if (strcmp(pcr_bank, "sha1") == 0) {
        return 0;
    } else if (strcmp(pcr_bank, "sha256") == 0) {
        return 1;
    } else if (strcmp(pcr_bank, "sha384") == 0) {
        return 2;
    } else if (strcmp(pcr_bank, "sha512") == 0) {
        return 3;
    }
    return -1;
}

static int charra_cli_verifier_parse_pcr_selection(
        cli_config* const variables, char* pcr_selections) {
    /*
    Syntax of PCR selections is: "bank1:pcr1,pcr2,pcr3+bank2:pcr4,pcr5"
    best way to parse is by splitting the string by '+' for each bank
    */
    char* bank_token = NULL;
    char* next_token = pcr_selections;
    char* bank_name = NULL;
    char* pcr_list = NULL;
    int bank = -1;
    while ((bank_token = charra_cli_verifier_strtok(NULL, "+", &next_token)) !=
            NULL) {
        bank_name = charra_cli_verifier_strtok(bank_token, ":", &pcr_list);
        if (bank_name == NULL) {
            charra_log_error("[%s] No bank defined '%s'", LOG_NAME);
            return -1;
        }
        bank = charra_cli_verifier_parse_pcr_bank_to_index(bank_name);
        if (bank < 0 || bank >= TPM2_PCR_BANK_COUNT) {
            charra_log_error("[%s] Invalid PCR bank '%s'", LOG_NAME, bank_name);
            return -1;
        }
        if (charra_cli_verifier_parse_pcr_bank(
                    variables->specific_config.verifier_config
                            .tpm_pcr_selection[bank],
                    &variables->specific_config.verifier_config
                             .tpm_pcr_selection_len[bank],
                    pcr_list) != 0) {
            return -1;
        }
    }
    return 0;
}

static int charra_cli_verifier_pcr_selection(cli_config* const variables) {
    uint8_t(*tpm_pcr_selection)[TPM2_MAX_PCRS] =
            variables->specific_config.verifier_config.tpm_pcr_selection;
    uint32_t* tpm_pcr_selection_len =
            variables->specific_config.verifier_config.tpm_pcr_selection_len;
    for (uint32_t i = 0; i < TPM2_PCR_BANK_COUNT; i++) {
        for (uint32_t j = 0; j < TPM2_MAX_PCRS; j++) {
            /*
             * overwrite static config with zeros in case CLI config uses
             * less PCRs
             */
            tpm_pcr_selection[i][j] = 0;
        }
        tpm_pcr_selection_len[i] = 0;
    }
    if (charra_cli_verifier_parse_pcr_selection(variables, optarg) != 0) {
        return -1;
    }
    return 0;
}

static int charra_cli_verifier_hash_algorithm(cli_config* const variables) {
    cli_config_signature_hash_algorithm* const hash_algo =
            variables->specific_config.verifier_config.signature_hash_algorithm;
    if (strcmp(optarg, "sha1") == 0) {
        hash_algo->mbedtls_hash_algorithm = MBEDTLS_MD_SHA1;
        hash_algo->tpm2_hash_algorithm = TPM2_ALG_SHA1;
    } else if (strcmp(optarg, "sha256") == 0) {
        hash_algo->mbedtls_hash_algorithm = MBEDTLS_MD_SHA256;
        hash_algo->tpm2_hash_algorithm = TPM2_ALG_SHA256;
    } else if (strcmp(optarg, "sha384") == 0) {
        hash_algo->mbedtls_hash_algorithm = MBEDTLS_MD_SHA384;
        hash_algo->tpm2_hash_algorithm = TPM2_ALG_SHA384;
    } else if (strcmp(optarg, "sha512") == 0) {
        hash_algo->mbedtls_hash_algorithm = MBEDTLS_MD_SHA512;
        hash_algo->tpm2_hash_algorithm = TPM2_ALG_SHA512;
    } else {
        /* This algorithms are not supported by mbedTLS:
        sm3_256, sha3_256, sha3_384, sha3_512 */
        charra_log_error(
                "[%s] Unsupported hash algorithm: '%s'", LOG_NAME, optarg);
        return -1;
    }
    return 0;
}

int charra_parse_command_line_verifier_arguments(
        const int argc, char** const argv, cli_config* const variables) {
    int rc = 0;
    for (;;) {
        int index = -1;
        int identifier = getopt_long(
                argc, argv, VERIFIER_SHORT_OPTIONS, verifier_options, &index);
        switch (identifier) {
        case -1:
            rc = charra_check_required_options(variables);
            goto cleanup;
        case CLI_COMMON_PCR_LOG:
            rc = charra_cli_verifier_pcr_log(variables);
            break;
        /* parse specific options */
        case CLI_VERIFIER_PSK_IDENTITY:
            charra_cli_verifier_identity(variables);
            break;
        case CLI_VERIFIER_IP:
            rc = charra_cli_verifier_ip(variables);
            break;
        case CLI_VERIFIER_TIMEOUT:
            rc = charra_cli_verifier_timeout(variables);
            break;
        case CLI_VERIFIER_ATTESTATION_PUBLIC_KEY:
            rc = charra_cli_verifier_attestation_public_key(variables);
            break;
        case CLI_VERIFIER_PCR_FILE:
            rc = charra_cli_verifier_pcr_file(variables);
            break;
        case CLI_VERIFIER_PCR_SELECTION:
            rc = charra_cli_verifier_pcr_selection(variables);
            break;
        case CLI_VERIFIER_HASH_ALGORITHM:
            rc = charra_cli_verifier_hash_algorithm(variables);
            break;
        /* parse common options */
        default:
            rc = charra_cli_util_common_parse_command_line_argument(identifier,
                    variables, LOG_NAME, charra_print_verifier_help_message);
            break;
        }
        if (rc != 0) {
            goto cleanup;
        }
    }
cleanup:
    return rc;
}
