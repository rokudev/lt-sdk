/*******************************************************************************
 * source/lt/net/x509/LTNetX509Types.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_NET_TLS_LTNETX509TYPES_H
#define ROKU_LT_SOURCE_LT_NET_TLS_LTNETX509TYPES_H

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/system/timezone/LTSystemTimeZone.h>

LT_EXTERN_C_BEGIN

/* Type-length-value structure that allows for ASN1 using DER */
typedef struct LTAsn1Buf {
    u8                       tag;                   /**< ASN1 type, e.g. MBEDTLS_ASN1_UTF8_STRING. */
    u16                      len;                   /**< ASN1 length, in octets. */
    u8                      *p;                     /**< ASN1 data, e.g. in ASCII. */
} LTAsn1Buf;

/* Container for ASN1 bit strings */
typedef struct LTAsn1BitString {
    u16                      len;                   /**< ASN1 length, in octets. */
    u8                       unusedBits;            /**< Number of unused bits at the end of the string */
    u8                      *p;                     /**< Raw ASN1 data for the bit string */
} LTAsn1BitString;

/* Container for a sequence of ASN.1 items */
typedef struct LTAsn1Sequence {
    LTAsn1Buf                buf;                   /**< Buffer containing the given ASN.1 item. */

    /* The next entry in the sequence.
     * The details of memory management for sequences are not documented and
     * may change in future versions. Set this field to \p NULL when
     * initializing a structure, and do not modify it except via Mbed TLS
     * library functions.
     */
    struct LTAsn1Sequence   *next;
} LTAsn1Sequence;

/* Container for a sequence or list of 'named' ASN.1 data items  */
typedef struct LTAsn1NamedData {
    LTAsn1Buf                oid;                   /**< The object identifier. */
    LTAsn1Buf                val;                   /**< The named value. */
    struct LTAsn1NamedData  *next;                  /**< The next entry in the sequence */
} LTAsn1NamedData;

typedef struct LTAsn1GetSequenceCbCtx {
    int                      tag;
    LTAsn1Sequence          *cur;
} LTAsn1GetSequenceCbCtx;

/**
 * From RFC 5280 section 4.2.1.6:
 * OtherName ::= SEQUENCE {
 *      type-id    OBJECT IDENTIFIER,
 *      value      [0] EXPLICIT ANY DEFINED BY type-id }
 */
typedef struct LTX509SanOtherTime {
    // The type_id is an OID as defined in RFC 5280.
    LTAsn1Buf                typeId;                /**< The type id. */
    union {
        /**
         * From RFC 4108 section 5:
         * HardwareModuleName ::= SEQUENCE {
         *                         hwType OBJECT IDENTIFIER,
         *                         hwSerialNum OCTET STRING }
         */
        struct {
            LTAsn1Buf        oid;                   /**< The object identifier. */
            LTAsn1Buf        val;                   /**< The named value. */
        }                    hardwareModuleName;
    }                        value;
} LTX509SanOtherTime;

/* A structure for holding the parsed Subject Alternative Name, according to type. */
typedef struct LTX509SubjectAlternativeName {
    int                      type;                  /**< The SAN type, value of MBEDTLS_X509_SAN_XXX. */
    union {
        LTX509SanOtherTime   otherName;             /**< The otherName supported type. */
        LTAsn1Buf            unstructuredName;      /**< The buffer for the unstructed types. Only dnsName currently supported */
    }                        san;                   /**< A union of the supported SAN types */
} LTX509SubjectAlternativeName;

/* Base OID descriptor structure */
typedef struct LTOidDescriptor {
    char                    *asn1;                  /**< OID ASN.1 representation */
    u16                      asn1Len;               /**< length of asn1 */
    char                    *name;                  /**< official name (e.g. from RFC) */
    char                    *description;           /**< human friendly description */
} LTOidDescriptor;

/* For SignatureAlgorithmIdentifier */
typedef struct LTOidSigAlg {
    LTOidDescriptor          descriptor;            /**< OID ASN.1 descriptor */
    u8                       mdAlg;                 /**< message digest algorithm OID number */
    u16                      pkAlg;
} LTOidSigAlg;

/* For X509 extensions */
typedef struct LTOidX509Ext {
    LTOidDescriptor          descriptor;
    int                      extType;
} LTOidX509Ext;

/* For PublicKeyInfo (PKCS1, RFC 5480) */
typedef struct LTOidPkAlg {
    LTOidDescriptor          descriptor;
    u16                      pkAlg;
} LTOidPkAlg;

/* Container for an X.509 certificate */
typedef struct LTX509Cert {
    LTAsn1Buf                raw;                    /**< The raw certificate data (DER). */
    LTAsn1Buf                tbs;                    /**< The raw certificate body (DER). The part that is To Be Signed. */
    LTAsn1Buf                issuerRaw;              /**< The raw issuer data (DER). Used for quick comparison. */
    LTAsn1Buf                subjectRaw;             /**< The raw subject data (DER). Used for quick comparison. */

    // tbsCertificate
    int                      version;               /**< The X.509 version. In cert, version is (0=v1, 1=v2, 2=v3), But after parsing cert successfully, version is (1=v1, 2=v2, 3=v3) */
    LTAsn1Buf                serial;                /**< Unique id for certificate issued by a specific CA. */
    LTAsn1Buf                sigOid;                /**< Signature algorithm, e.g. sha1RSA */
    u8                       sigMd;                 /**< Internal representation of the MD algorithm of the signature algorithm, e.g. MBEDTLS_MD_SHA256 */
    u16                      sigPk;                 /**< Internal representation of the Public Key algorithm of the signature algorithm, e.g. MBEDTLS_PK_RSA */
    void                    *sigOpts;               /**< in heap, Signature options */
    LTAsn1NamedData          issuer;                /**< in heap, The parsed issuer data (named information object). */
    LTCalendarTime           validFrom;             /**< Start time of certificate validity. */
    LTCalendarTime           validTo;               /**< End time of certificate validity. */
    LTAsn1NamedData          subject;               /**< in heap, The parsed subject data (named information object). */
    LTAsn1Buf                issuerId;              /**< Optional X.509 v2/v3 issuer unique identifier. */
    LTAsn1Buf                subjectId;             /**< Optional X.509 v2/v3 subject unique identifier. */
    u16                      pkAlg;                 /**< The subject public key type */
    LTAsn1Buf                pkParams;              /**< Optional public key parameters */
    LTAsn1Buf                pubKey;                /**< Public key. Then end public key will be copied to session. */

    // signature
    LTAsn1Buf                sig;                   /**< Signature: hash of the tbs part signed with the private key. */
    union {
        u8                   EcdsaSig[ECDSA_P256_SIGNATURE_LENGTH];
        u8                   EddsaSig[EdDSA_SIGNATURE_LENGTH];
    };

    // extension
    LTAsn1Buf                v3Ext;                 /**< Optional X.509 v3 extensions.  */
    LTAsn1Buf                subjectKeyId;          /**< Subject key identifier */
    LTAsn1Buf                authorityKeyId;        /**< Authority key identifier */
    int                      extTypes;              /**< Bit string containing detected and parsed extensions */
    int                      caIsTrue;              /**< Optional Basic Constraint extension value: 1 if this certificate belongs to a CA, 0 otherwise. */
    int                      maxPathLen;            /**< Optional Basic Constraint extension value: The maximum path length to the root certificate. Path length is 1 higher than RFC 5280 'meaning', so 1+ */
    u32                      keyUsage;              /**< Optional key usage extension value: See the values in x509.h */
    u8                       nsCertType;            /**< Optional Netscape certificate type extension value: See the values in x509.h */
    LTAsn1Sequence           extKeyUsage;           /**< in heap, Optional list of extended key usage OIDs. */
    LTAsn1Sequence           subjectAltNames;       /**< in heap, Optional list of raw entries of Subject Alternative Names extension (currently only dNSName and OtherName are listed). */
    #if 0
    LTAsn1Sequence           certificatePolicies;   /**< in heap, Optional list of certificate policies (Only anyPolicy is printed and enforced, however the rest of the policies are still listed). */
    #endif
} LTX509Cert;

/* We support at most 6 certs in a chain.
 * We only keep the end certificate, and don't keep any chain.
 * The received certificate chain must be in this order:
 *      End cert, intermediate certs, root cert
 * The certificates are saved in the chain in the same order.
 * We verify the received chain from the root cert (last) to the end cert (first).
 */
enum {
    kLTNetX509_MaxInterMediateCa   = 2,
    kLTNetX509_MaxVerifyChainSize  = 4,
};

typedef struct LTX509Chain {
    u8 count;                                       /**< The number of certs in the chain */
    struct {
        u16        certLen;                         /**< The length of a cert */
        const u8  *cert;                            /**< The pointer to the buffer of the chain */
    } certs[kLTNetX509_MaxVerifyChainSize];
} LTX509Chain;

LT_EXTERN_C_END
#endif  // ROKU_LT_SOURCE_LT_NET_TLS_LTNETX509TYPES_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  16-Oct-24   gallienus   created
 */
