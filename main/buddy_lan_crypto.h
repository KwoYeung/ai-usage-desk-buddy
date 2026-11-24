#pragma once

#include <psa/crypto.h>

static inline psa_status_t buddy_gcm_key(const uint8_t key[32], mbedtls_svc_key_id_t *id)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
    return psa_import_key(&attributes, key, 32, id);
}

static inline bool buddy_hmac(const uint8_t key[32], const uint8_t *data,
                             size_t size, uint8_t out[32])
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t id = MBEDTLS_SVC_KEY_ID_INIT;
    size_t length;
    const psa_algorithm_t algorithm = PSA_ALG_HMAC(PSA_ALG_SHA_256);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, algorithm);
    if (psa_import_key(&attributes, key, 32, &id) != PSA_SUCCESS) return false;
    bool ok = psa_mac_compute(id, algorithm, data, size, out, 32, &length) == PSA_SUCCESS
              && length == 32;
    psa_destroy_key(id);
    return ok;
}
