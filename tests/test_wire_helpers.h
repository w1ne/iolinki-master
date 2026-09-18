#ifndef IOLINKI_MASTER_TESTS_WIRE_HELPERS_H
#define IOLINKI_MASTER_TESTS_WIRE_HELPERS_H

#include <stdint.h>

#include "iolinki/crc.h"

/**
 * @brief A.1.6 checksum of a TYPE_0 reply body @c [value, CKS=0].
 *
 * A TYPE_0 reply is one OD octet plus CKS (A.1.5). The checksum covers the data
 * octet and the CKS octet with its checksum bits zeroed.
 */
static inline uint8_t test_ck6_type0(uint8_t value)
{
    uint8_t msg[2] = {value, 0x00U};

    return iolink_checksum6(msg, sizeof(msg));
}

/**
 * @brief A.1.6 checksum of a reply body ending in a CKS octet with @p flags set.
 *
 * @p body holds every message octet except the final CKS; @p body_len is its
 * length (0..42). The CKS octet carries @p flags in bits 7/6 and the computed
 * checksum in bits 0-5.
 */
static inline uint8_t test_ck6_reply(const uint8_t* body, uint8_t body_len, uint8_t flags)
{
    uint8_t msg[43] = {0U};
    uint8_t i;

    for (i = 0U; i < body_len; i++) {
        msg[i] = body[i];
    }
    msg[body_len] = (uint8_t) (flags & 0xC0U);

    return (uint8_t) (iolink_checksum6(msg, (size_t) body_len + 1U) | (flags & 0xC0U));
}

#endif /* IOLINKI_MASTER_TESTS_WIRE_HELPERS_H */
