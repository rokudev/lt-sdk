/**
 * \file ecp_alt.h
 *
 * \brief Function declarations for alternative implementation of elliptic curve
 * point arithmetic.
 */
/*
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * References:
 *
 * [1] BERNSTEIN, Daniel J. Curve25519: new Diffie-Hellman speed records.
 *     <http://cr.yp.to/ecdh/curve25519-20060209.pdf>
 *
 * [2] CORON, Jean-S'ebastien. Resistance against differential power analysis
 *     for elliptic curve cryptosystems. In : Cryptographic Hardware and
 *     Embedded Systems. Springer Berlin Heidelberg, 1999. p. 292-302.
 *     <http://link.springer.com/chapter/10.1007/3-540-48059-5_25>
 *
 * [3] HEDABOU, Mustapha, PINEL, Pierre, et B'EN'ETEAU, Lucien. A comb method to
 *     render ECC resistant against Side Channel Attacks. IACR Cryptology
 *     ePrint Archive, 2004, vol. 2004, p. 342.
 *     <http://eprint.iacr.org/2004/342.pdf>
 *
 * [4] Certicom Research. SEC 2: Recommended Elliptic Curve Domain Parameters.
 *     <http://www.secg.org/sec2-v2.pdf>
 *
 * [5] HANKERSON, Darrel, MENEZES, Alfred J., VANSTONE, Scott. Guide to Elliptic
 *     Curve Cryptography.
 *
 * [6] Digital Signature Standard (DSS), FIPS 186-4.
 *     <http://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.186-4.pdf>
 *
 * [7] Elliptic Curve Cryptography (ECC) Cipher Suites for Transport Layer
 *     Security (TLS), RFC 4492.
 *     <https://tools.ietf.org/search/rfc4492>
 *
 * [8] <http://www.hyperelliptic.org/EFD/g1p/auto-shortw-jacobian.html>
 *
 * [9] COHEN, Henri. A Course in Computational Algebraic Number Theory.
 *     Springer Science & Business Media, 1 Aug 2000
 */

#ifndef MBEDTLS_ECP_ALT_H
#define MBEDTLS_ECP_ALT_H

#include "build_info.h"

typedef struct mbedtls_ecp_group {
    mbedtls_ecp_group_id id;    /*!< An internal group identifier. */
    mbedtls_mpi P;              /*!< The prime modulus of the base field. */
    mbedtls_mpi A;              /*!< For Short Weierstrass: \p A in the equation. For
                                     Montgomery curves: <code>(A + 2) / 4</code>. */
    mbedtls_mpi B;              /*!< For Short Weierstrass: \p B in the equation.
                                     For Montgomery curves: unused. */
    mbedtls_ecp_point G;        /*!< The generator of the subgroup used. */
    mbedtls_mpi N;              /*!< The order of \p G. */
    size_t pbits;               /*!< The number of bits in \p P.*/
    size_t nbits;               /*!< For Short Weierstrass: The number of bits in \p P.
                                     For Montgomery curves: the number of bits in the
                                     private keys. */
    /* End of public fields */

    unsigned int MBEDTLS_PRIVATE(h);             /*!< \internal 1 if the constants are static. */
    int(*MBEDTLS_PRIVATE(modp))(mbedtls_mpi *);  /*!< The function for fast pseudo-reduction
                                                    mod \p P (see above).*/
    int(*MBEDTLS_PRIVATE(t_pre))(mbedtls_ecp_point *, void *);   /*!< Unused. */
    int(*MBEDTLS_PRIVATE(t_post))(mbedtls_ecp_point *, void *);  /*!< Unused. */
    void *MBEDTLS_PRIVATE(t_data);               /*!< Unused. */
    mbedtls_ecp_point *MBEDTLS_PRIVATE(T);       /*!< Pre-computed points for ecp_mul_comb(). */
    size_t MBEDTLS_PRIVATE(T_size);              /*!< The number of dynamic allocated pre-computed points. */
}
mbedtls_ecp_group;

#endif /* ecp_alt.h */
