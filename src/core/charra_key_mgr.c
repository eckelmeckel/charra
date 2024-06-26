/* SPDX-License-Identifier: BSD-3-Clause */
/*****************************************************************************
 * Copyright 2019, Fraunhofer Institute for Secure Information Technology SIT.
 * All rights reserved.
 ****************************************************************************/

/**
 * @file charra_key_mgr.c
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

#include "charra_key_mgr.h"

#include <inttypes.h>
#include <string.h>
#include <tss2/tss2_esys.h>
#include <tss2/tss2_tpm2_types.h>

#include "../common/charra_error.h"
#include "../common/charra_log.h"
#include "../util/tpm2_util.h"

CHARRA_RC charra_load_tpm2_key(ESYS_CONTEXT* const ctx,
        ESYS_TR* const key_handle, const char* const path) {
    TSS2_RC r = TSS2_RC_SUCCESS;

    /* load TPM2 attestation key */
    if ((r = tpm2_load_tpm_context_from_path(ctx, key_handle, path)) !=
            TSS2_RC_SUCCESS) {
        return CHARRA_RC_ERROR;
    }

    return CHARRA_RC_SUCCESS;
}

CHARRA_RC charra_load_external_public_key(ESYS_CONTEXT* ctx,
        TPM2B_PUBLIC* external_public_key, ESYS_TR* key_handle,
        const char* path) {
    TSS2_RC r = TSS2_RC_SUCCESS;
    if (path == NULL) {
        charra_log_error("Invalid path for external public key.");
        return CHARRA_RC_ERROR;
    }
    if (external_public_key == NULL) {
        charra_log_error("Invalid pointer for external public key.");
        return CHARRA_RC_ERROR;
    }

    // charra_log_info("Loading TPM key from file.");
    if (!tpm2_load_external_public_key_from_path(path, external_public_key)) {
        charra_log_error("Loading external public key from file failed.");
        return CHARRA_RC_ERROR;
    }

    r = Esys_LoadExternal(ctx, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, NULL,
            external_public_key, TPM2_RH_OWNER, key_handle);
    if (r != TSS2_RC_SUCCESS) {
        charra_log_error("Loading external public key to TPM failed.");
        return CHARRA_RC_ERROR;
    }

    return CHARRA_RC_SUCCESS;
}
