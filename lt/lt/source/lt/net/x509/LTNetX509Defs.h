/*******************************************************************************
 * source/lt/net/x509/LTNetX509Defs.h
 * 
 * Macros for X509.
 * Macros starting with MBEDTLS are ported from MbedTLS.
 * Macros starting with LT are added for LT.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 * 
 * This file has been created to complete X509 operations for LT, reduce code
 * and data sizes, and be compatible with LT coding standard.
 * 
 * This file has modified the source code from https://github.com/Mbed-TLS/mbedtls
 * 
 * The modified source files are asn1.h, error.h, oid.h, pk.h, x509.h, 
 * x509_crl.h, x509_crt.h, and x509_csr.h in Mbed TLS.
 *
 * The copyright and license in the headers of the original source files are as follows.
 * 
 *******************************************************************************
 *
 * Copyright The Mbed TLS Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 ( the "License" ); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 ******************************************************************************/
#ifndef ROKU_LT_SOURCE_LT_NET_X509_LTNETX509DEFS_H
#define ROKU_LT_SOURCE_LT_NET_X509_LTNETX509DEFS_H

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/system/timezone/LTSystemTimeZone.h>

LT_EXTERN_C_BEGIN

// HASH
#define MBEDTLS_MD_NONE                                       0    /**< None. */
#define MBEDTLS_MD_MD5                                        1    /**< The MD5 message digest. */
#define MBEDTLS_MD_SHA1                                       2    /**< The SHA-1 message digest. */
#define MBEDTLS_MD_SHA224                                     3    /**< The SHA-224 message digest. */
#define MBEDTLS_MD_SHA256                                     4    /**< The SHA-256 message digest. */
#define MBEDTLS_MD_SHA384                                     5    /**< The SHA-384 message digest. */
#define MBEDTLS_MD_SHA512                                     6    /**< The SHA-512 message digest. */
#define MBEDTLS_MD_RIPEMD160                                  7    /**< The RIPEMD-160 message digest. */

// Public key
#define MBEDTLS_PK_NONE                                       0
#define MBEDTLS_PK_RSA                                        1  
#define MBEDTLS_PK_ECKEY                                      2
#define MBEDTLS_PK_ECKEY_DH                                   3
#define MBEDTLS_PK_ECDSA                                      4
#define MBEDTLS_PK_RSA_ALT                                    5
#define MBEDTLS_PK_RSASSA_PSS                                 6
#define LTTLS_PK_ED25519                                      7
#define MBEDTLS_PK_OPAQUE                                     8

#define OID_DESCRIPTOR(s, name, description)  { s, (sizeof(s) - 1), name, description }
#define NULL_OID_DESCRIPTOR                   { NULL, 0, NULL, NULL }

#define MBEDTLS_OID_SIZE(x) (sizeof(x) - 1)  /**< Returns the size of the binary string, without the trailing \\0 */

/**
 * Compares an LTAsn1Buf structure to a reference OID.
 *
 * Only works for 'defined' oid_str values (MBEDTLS_OID_HMAC_SHA1), you cannot use a
 * 'unsigned char *oid' here!
 */
#define MBEDTLS_OID_CMP(oidStr, oidBuf)                             \
        ((MBEDTLS_OID_SIZE(oidStr) != (oidBuf)->len) ||            \
         lt_memcmp((oidStr), (oidBuf)->p, (oidBuf)->len) != 0)

#define MBEDTLS_OID_CMP_RAW(oidStr, oidBuf, oidBufLen)              \
        ((MBEDTLS_OID_SIZE(oidStr) != (oidBufLen)) ||               \
        lt_memcmp((oidStr), (oidBuf), (oidBufLen)) != 0)

/* X.509 extension types (internal, arbitrary values for bitsets) */
#define MBEDTLS_OID_X509_EXT_AUTHORITY_KEY_IDENTIFIER        (1 << 0)
#define MBEDTLS_OID_X509_EXT_SUBJECT_KEY_IDENTIFIER          (1 << 1)
#define MBEDTLS_OID_X509_EXT_KEY_USAGE                       (1 << 2)
#define MBEDTLS_OID_X509_EXT_CERTIFICATE_POLICIES            (1 << 3)
#define MBEDTLS_OID_X509_EXT_POLICY_MAPPINGS                 (1 << 4)
#define MBEDTLS_OID_X509_EXT_SUBJECT_ALT_NAME                (1 << 5)
#define MBEDTLS_OID_X509_EXT_ISSUER_ALT_NAME                 (1 << 6)
#define MBEDTLS_OID_X509_EXT_SUBJECT_DIRECTORY_ATTRS         (1 << 7)
#define MBEDTLS_OID_X509_EXT_BASIC_CONSTRAINTS               (1 << 8)
#define MBEDTLS_OID_X509_EXT_NAME_CONSTRAINTS                (1 << 9)
#define MBEDTLS_OID_X509_EXT_POLICY_CONSTRAINTS              (1 << 10)
#define MBEDTLS_OID_X509_EXT_EXTENDED_KEY_USAGE              (1 << 11)
#define MBEDTLS_OID_X509_EXT_CRL_DISTRIBUTION_POINTS         (1 << 12)
#define MBEDTLS_OID_X509_EXT_INIHIBIT_ANYPOLICY              (1 << 13)
#define MBEDTLS_OID_X509_EXT_FRESHEST_CRL                    (1 << 14)
#define MBEDTLS_OID_X509_EXT_NS_CERT_TYPE                    (1 << 16)

/* Top level OID tuples */
#define MBEDTLS_OID_ISO_MEMBER_BODIES           "\x2a"          /* {iso(1) member-body(2)} */
#define MBEDTLS_OID_ISO_IDENTIFIED_ORG          "\x2b"          /* {iso(1) identified-organization(3)} */
#define MBEDTLS_OID_ISO_CCITT_DS                "\x55"          /* {joint-iso-ccitt(2) ds(5)} */
#define MBEDTLS_OID_ISO_ITU_COUNTRY             "\x60"          /* {joint-iso-itu-t(2) country(16)} */

/* ISO Member bodies OID parts */
#define MBEDTLS_OID_COUNTRY_US                  "\x86\x48"      /* {us(840)} */
#define MBEDTLS_OID_ORG_RSA_DATA_SECURITY       "\x86\xf7\x0d"  /* {rsadsi(113549)} */
#define MBEDTLS_OID_RSA_COMPANY                 MBEDTLS_OID_ISO_MEMBER_BODIES MBEDTLS_OID_COUNTRY_US \
                                                MBEDTLS_OID_ORG_RSA_DATA_SECURITY /* {iso(1) member-body(2) us(840) rsadsi(113549)} */
#define MBEDTLS_OID_ORG_ANSI_X9_62              "\xce\x3d" /* ansi-X9-62(10045) */
#define MBEDTLS_OID_ANSI_X9_62                  MBEDTLS_OID_ISO_MEMBER_BODIES MBEDTLS_OID_COUNTRY_US \
                                                MBEDTLS_OID_ORG_ANSI_X9_62

/* ISO Identified organization OID parts */
#define MBEDTLS_OID_ORG_DOD                     "\x06"          /* {dod(6)} */
#define MBEDTLS_OID_ORG_OIW                     "\x0e"
#define MBEDTLS_OID_OIW_SECSIG                  MBEDTLS_OID_ORG_OIW "\x03"
#define MBEDTLS_OID_OIW_SECSIG_ALG              MBEDTLS_OID_OIW_SECSIG "\x02"
#define MBEDTLS_OID_OIW_SECSIG_SHA1             MBEDTLS_OID_OIW_SECSIG_ALG "\x1a"
#define MBEDTLS_OID_ORG_CERTICOM                "\x81\x04"      /* certicom(132) */
#define MBEDTLS_OID_CERTICOM                    MBEDTLS_OID_ISO_IDENTIFIED_ORG MBEDTLS_OID_ORG_CERTICOM
#define MBEDTLS_OID_ORG_TELETRUST               "\x24"          /* teletrust(36) */
#define MBEDTLS_OID_TELETRUST                   MBEDTLS_OID_ISO_IDENTIFIED_ORG MBEDTLS_OID_ORG_TELETRUST

/* ISO ITU OID parts */
#define MBEDTLS_OID_ORGANIZATION                "\x01"          /* {organization(1)} */
#define MBEDTLS_OID_ISO_ITU_US_ORG              MBEDTLS_OID_ISO_ITU_COUNTRY MBEDTLS_OID_COUNTRY_US MBEDTLS_OID_ORGANIZATION /* {joint-iso-itu-t(2) country(16) us(840) organization(1)} */

#define MBEDTLS_OID_ORG_GOV                     "\x65"          /* {gov(101)} */
#define MBEDTLS_OID_GOV                         MBEDTLS_OID_ISO_ITU_US_ORG MBEDTLS_OID_ORG_GOV /* {joint-iso-itu-t(2) country(16) us(840) organization(1) gov(101)} */

#define MBEDTLS_OID_ORG_NETSCAPE                "\x86\xF8\x42"  /* {netscape(113730)} */
#define MBEDTLS_OID_NETSCAPE                    MBEDTLS_OID_ISO_ITU_US_ORG MBEDTLS_OID_ORG_NETSCAPE /* Netscape OID {joint-iso-itu-t(2) country(16) us(840) organization(1) netscape(113730)} */

/* ISO arc for standard certificate and CRL extensions */
#define MBEDTLS_OID_ID_CE                       MBEDTLS_OID_ISO_CCITT_DS "\x1D" /**< id-ce OBJECT IDENTIFIER  ::=  {joint-iso-ccitt(2) ds(5) 29} */

#define MBEDTLS_OID_NIST_ALG                    MBEDTLS_OID_GOV "\x03\x04" /** { joint-iso-itu-t(2) country(16) us(840) organization(1) gov(101) csor(3) nistAlgorithm(4) */

/**
 * Private Internet Extensions
 * { iso(1) identified-organization(3) dod(6) internet(1)
 *   security(5) mechanisms(5) pkix(7) }
 */
#define MBEDTLS_OID_INTERNET                    MBEDTLS_OID_ISO_IDENTIFIED_ORG MBEDTLS_OID_ORG_DOD "\x01"
#define MBEDTLS_OID_PKIX                        MBEDTLS_OID_INTERNET "\x05\x05\x07"

/* Arc for standard naming attributes */
#define MBEDTLS_OID_AT                          MBEDTLS_OID_ISO_CCITT_DS "\x04" /**< id-at OBJECT IDENTIFIER ::= {joint-iso-ccitt(2) ds(5) 4} */
#define MBEDTLS_OID_AT_CN                       MBEDTLS_OID_AT "\x03" /**< id-at-commonName AttributeType:= {id-at 3} */
#define MBEDTLS_OID_AT_SUR_NAME                 MBEDTLS_OID_AT "\x04" /**< id-at-surName AttributeType:= {id-at 4} */
#define MBEDTLS_OID_AT_SERIAL_NUMBER            MBEDTLS_OID_AT "\x05" /**< id-at-serialNumber AttributeType:= {id-at 5} */
#define MBEDTLS_OID_AT_COUNTRY                  MBEDTLS_OID_AT "\x06" /**< id-at-countryName AttributeType:= {id-at 6} */
#define MBEDTLS_OID_AT_LOCALITY                 MBEDTLS_OID_AT "\x07" /**< id-at-locality AttributeType:= {id-at 7} */
#define MBEDTLS_OID_AT_STATE                    MBEDTLS_OID_AT "\x08" /**< id-at-state AttributeType:= {id-at 8} */
#define MBEDTLS_OID_AT_ORGANIZATION             MBEDTLS_OID_AT "\x0A" /**< id-at-organizationName AttributeType:= {id-at 10} */
#define MBEDTLS_OID_AT_ORG_UNIT                 MBEDTLS_OID_AT "\x0B" /**< id-at-organizationalUnitName AttributeType:= {id-at 11} */
#define MBEDTLS_OID_AT_TITLE                    MBEDTLS_OID_AT "\x0C" /**< id-at-title AttributeType:= {id-at 12} */
#define MBEDTLS_OID_AT_POSTAL_ADDRESS           MBEDTLS_OID_AT "\x10" /**< id-at-postalAddress AttributeType:= {id-at 16} */
#define MBEDTLS_OID_AT_POSTAL_CODE              MBEDTLS_OID_AT "\x11" /**< id-at-postalCode AttributeType:= {id-at 17} */
#define MBEDTLS_OID_AT_GIVEN_NAME               MBEDTLS_OID_AT "\x2A" /**< id-at-givenName AttributeType:= {id-at 42} */
#define MBEDTLS_OID_AT_INITIALS                 MBEDTLS_OID_AT "\x2B" /**< id-at-initials AttributeType:= {id-at 43} */
#define MBEDTLS_OID_AT_GENERATION_QUALIFIER     MBEDTLS_OID_AT "\x2C" /**< id-at-generationQualifier AttributeType:= {id-at 44} */
#define MBEDTLS_OID_AT_UNIQUE_IDENTIFIER        MBEDTLS_OID_AT "\x2D" /**< id-at-uniqueIdentifier AttributType:= {id-at 45} */
#define MBEDTLS_OID_AT_DN_QUALIFIER             MBEDTLS_OID_AT "\x2E" /**< id-at-dnQualifier AttributeType:= {id-at 46} */
#define MBEDTLS_OID_AT_PSEUDONYM                MBEDTLS_OID_AT "\x41" /**< id-at-pseudonym AttributeType:= {id-at 65} */

#define MBEDTLS_OID_UID                         "\x09\x92\x26\x89\x93\xF2\x2C\x64\x01\x01" /** id-domainComponent AttributeType:= {itu-t(0) data(9) pss(2342) ucl(19200300) pilot(100) pilotAttributeType(1) uid(1)} */
#define MBEDTLS_OID_DOMAIN_COMPONENT            "\x09\x92\x26\x89\x93\xF2\x2C\x64\x01\x19" /** id-domainComponent AttributeType:= {itu-t(0) data(9) pss(2342) ucl(19200300) pilot(100) pilotAttributeType(1) domainComponent(25)} */

/* OIDs for standard certificate extensions */
#define MBEDTLS_OID_AUTHORITY_KEY_IDENTIFIER    MBEDTLS_OID_ID_CE "\x23" /**< id-ce-authorityKeyIdentifier OBJECT IDENTIFIER ::=  { id-ce 35 } */
#define MBEDTLS_OID_SUBJECT_KEY_IDENTIFIER      MBEDTLS_OID_ID_CE "\x0E" /**< id-ce-subjectKeyIdentifier OBJECT IDENTIFIER ::=  { id-ce 14 } */
#define MBEDTLS_OID_KEY_USAGE                   MBEDTLS_OID_ID_CE "\x0F" /**< id-ce-keyUsage OBJECT IDENTIFIER ::=  { id-ce 15 } */
#define MBEDTLS_OID_CERTIFICATE_POLICIES        MBEDTLS_OID_ID_CE "\x20" /**< id-ce-certificatePolicies OBJECT IDENTIFIER ::=  { id-ce 32 } */
#define MBEDTLS_OID_POLICY_MAPPINGS             MBEDTLS_OID_ID_CE "\x21" /**< id-ce-policyMappings OBJECT IDENTIFIER ::=  { id-ce 33 } */
#define MBEDTLS_OID_SUBJECT_ALT_NAME            MBEDTLS_OID_ID_CE "\x11" /**< id-ce-subjectAltName OBJECT IDENTIFIER ::=  { id-ce 17 } */
#define MBEDTLS_OID_ISSUER_ALT_NAME             MBEDTLS_OID_ID_CE "\x12" /**< id-ce-issuerAltName OBJECT IDENTIFIER ::=  { id-ce 18 } */
#define MBEDTLS_OID_SUBJECT_DIRECTORY_ATTRS     MBEDTLS_OID_ID_CE "\x09" /**< id-ce-subjectDirectoryAttributes OBJECT IDENTIFIER ::=  { id-ce 9 } */
#define MBEDTLS_OID_BASIC_CONSTRAINTS           MBEDTLS_OID_ID_CE "\x13" /**< id-ce-basicConstraints OBJECT IDENTIFIER ::=  { id-ce 19 } */
#define MBEDTLS_OID_NAME_CONSTRAINTS            MBEDTLS_OID_ID_CE "\x1E" /**< id-ce-nameConstraints OBJECT IDENTIFIER ::=  { id-ce 30 } */
#define MBEDTLS_OID_POLICY_CONSTRAINTS          MBEDTLS_OID_ID_CE "\x24" /**< id-ce-policyConstraints OBJECT IDENTIFIER ::=  { id-ce 36 } */
#define MBEDTLS_OID_EXTENDED_KEY_USAGE          MBEDTLS_OID_ID_CE "\x25" /**< id-ce-extKeyUsage OBJECT IDENTIFIER ::= { id-ce 37 } */
#define MBEDTLS_OID_CRL_DISTRIBUTION_POINTS     MBEDTLS_OID_ID_CE "\x1F" /**< id-ce-cRLDistributionPoints OBJECT IDENTIFIER ::=  { id-ce 31 } */
#define MBEDTLS_OID_INIHIBIT_ANYPOLICY          MBEDTLS_OID_ID_CE "\x36" /**< id-ce-inhibitAnyPolicy OBJECT IDENTIFIER ::=  { id-ce 54 } */
#define MBEDTLS_OID_FRESHEST_CRL                MBEDTLS_OID_ID_CE "\x2E" /**< id-ce-freshestCRL OBJECT IDENTIFIER ::=  { id-ce 46 } */

/* Certificate policies */
#define MBEDTLS_OID_ANY_POLICY                  MBEDTLS_OID_CERTIFICATE_POLICIES "\x00" /**< anyPolicy OBJECT IDENTIFIER ::= { id-ce-certificatePolicies 0 } */

/* Netscape certificate extensions */
#define MBEDTLS_OID_NS_CERT                     MBEDTLS_OID_NETSCAPE "\x01"
#define MBEDTLS_OID_NS_CERT_TYPE                MBEDTLS_OID_NS_CERT  "\x01"
#define MBEDTLS_OID_NS_BASE_URL                 MBEDTLS_OID_NS_CERT  "\x02"
#define MBEDTLS_OID_NS_REVOCATION_URL           MBEDTLS_OID_NS_CERT  "\x03"
#define MBEDTLS_OID_NS_CA_REVOCATION_URL        MBEDTLS_OID_NS_CERT  "\x04"
#define MBEDTLS_OID_NS_RENEWAL_URL              MBEDTLS_OID_NS_CERT  "\x07"
#define MBEDTLS_OID_NS_CA_POLICY_URL            MBEDTLS_OID_NS_CERT  "\x08"
#define MBEDTLS_OID_NS_SSL_SERVER_NAME          MBEDTLS_OID_NS_CERT  "\x0C"
#define MBEDTLS_OID_NS_COMMENT                  MBEDTLS_OID_NS_CERT  "\x0D"
#define MBEDTLS_OID_NS_DATA_TYPE                MBEDTLS_OID_NETSCAPE "\x02"
#define MBEDTLS_OID_NS_CERT_SEQUENCE            MBEDTLS_OID_NS_DATA_TYPE "\x05"

/* OIDs for CRL extensions */
#define MBEDTLS_OID_PRIVATE_KEY_USAGE_PERIOD    MBEDTLS_OID_ID_CE "\x10"
#define MBEDTLS_OID_CRL_NUMBER                  MBEDTLS_OID_ID_CE "\x14" /**< id-ce-cRLNumber OBJECT IDENTIFIER ::= { id-ce 20 } */

/* X.509 v3 Extended key usage OIDs */
#define MBEDTLS_OID_ANY_EXTENDED_KEY_USAGE      MBEDTLS_OID_EXTENDED_KEY_USAGE "\x00" /**< anyExtendedKeyUsage OBJECT IDENTIFIER ::= { id-ce-extKeyUsage 0 } */

#define MBEDTLS_OID_KP                          MBEDTLS_OID_PKIX "\x03" /**< id-kp OBJECT IDENTIFIER ::= { id-pkix 3 } */
#define MBEDTLS_OID_SERVER_AUTH                 MBEDTLS_OID_KP "\x01"   /**< id-kp-serverAuth OBJECT IDENTIFIER ::= { id-kp 1 } */
#define MBEDTLS_OID_CLIENT_AUTH                 MBEDTLS_OID_KP "\x02"   /**< id-kp-clientAuth OBJECT IDENTIFIER ::= { id-kp 2 } */
#define MBEDTLS_OID_CODE_SIGNING                MBEDTLS_OID_KP "\x03"   /**< id-kp-codeSigning OBJECT IDENTIFIER ::= { id-kp 3 } */
#define MBEDTLS_OID_EMAIL_PROTECTION            MBEDTLS_OID_KP "\x04"   /**< id-kp-emailProtection OBJECT IDENTIFIER ::= { id-kp 4 } */
#define MBEDTLS_OID_TIME_STAMPING               MBEDTLS_OID_KP "\x08"   /**< id-kp-timeStamping OBJECT IDENTIFIER ::= { id-kp 8 } */
#define MBEDTLS_OID_OCSP_SIGNING                MBEDTLS_OID_KP "\x09"   /**< id-kp-OCSPSigning OBJECT IDENTIFIER ::= { id-kp 9 } */

/**
 * Wi-SUN Alliance Field Area Network
 * { iso(1) identified-organization(3) dod(6) internet(1)
 *   private(4) enterprise(1) WiSUN(45605) FieldAreaNetwork(1) }
 */
#define MBEDTLS_OID_WISUN_FAN                   MBEDTLS_OID_INTERNET "\x04\x01\x82\xe4\x25\x01"
#define MBEDTLS_OID_ON                          MBEDTLS_OID_PKIX "\x08" /**< id-on OBJECT IDENTIFIER ::= { id-pkix 8 } */
#define MBEDTLS_OID_ON_HW_MODULE_NAME           MBEDTLS_OID_ON "\x04" /**< id-on-hardwareModuleName OBJECT IDENTIFIER ::= { id-on 4 } */

/*  PKCS definition OIDs */
#define MBEDTLS_OID_PKCS                        MBEDTLS_OID_RSA_COMPANY "\x01" /**< pkcs OBJECT IDENTIFIER ::= { iso(1) member-body(2) us(840) rsadsi(113549) 1 } */
#define MBEDTLS_OID_PKCS1                       MBEDTLS_OID_PKCS "\x01" /**< pkcs-1 OBJECT IDENTIFIER ::= { iso(1) member-body(2) us(840) rsadsi(113549) pkcs(1) 1 } */
#define MBEDTLS_OID_PKCS5                       MBEDTLS_OID_PKCS "\x05" /**< pkcs-5 OBJECT IDENTIFIER ::= { iso(1) member-body(2) us(840) rsadsi(113549) pkcs(1) 5 } */
#define MBEDTLS_OID_PKCS9                       MBEDTLS_OID_PKCS "\x09" /**< pkcs-9 OBJECT IDENTIFIER ::= { iso(1) member-body(2) us(840) rsadsi(113549) pkcs(1) 9 } */
#define MBEDTLS_OID_PKCS12                      MBEDTLS_OID_PKCS "\x0c" /**< pkcs-12 OBJECT IDENTIFIER ::= { iso(1) member-body(2) us(840) rsadsi(113549) pkcs(1) 12 } */

/* PKCS#1 OIDs */
#define MBEDTLS_OID_PKCS1_RSA                   MBEDTLS_OID_PKCS1 "\x01" /**< rsaEncryption OBJECT IDENTIFIER ::= { pkcs-1 1 } */
#define MBEDTLS_OID_PKCS1_SHA224                MBEDTLS_OID_PKCS1 "\x0e" /**< sha224WithRSAEncryption ::= { pkcs-1 14 } */
#define MBEDTLS_OID_PKCS1_SHA256                MBEDTLS_OID_PKCS1 "\x0b" /**< sha256WithRSAEncryption ::= { pkcs-1 11 } */
#define MBEDTLS_OID_PKCS1_SHA384                MBEDTLS_OID_PKCS1 "\x0c" /**< sha384WithRSAEncryption ::= { pkcs-1 12 } */
#define MBEDTLS_OID_PKCS1_SHA512                MBEDTLS_OID_PKCS1 "\x0d" /**< sha512WithRSAEncryption ::= { pkcs-1 13 } */
#define MBEDTLS_OID_RSA_SHA_OBS                 "\x2B\x0E\x03\x02\x1D"
#define MBEDTLS_OID_PKCS9_EMAIL                 MBEDTLS_OID_PKCS9 "\x01" /**< emailAddress AttributeType ::= { pkcs-9 1 } */

/* RFC 4055 */
#define MBEDTLS_OID_RSASSA_PSS                  MBEDTLS_OID_PKCS1 "\x0a" /**< id-RSASSA-PSS ::= { pkcs-1 10 } */
#define MBEDTLS_OID_MGF1                        MBEDTLS_OID_PKCS1 "\x08" /**< id-mgf1 ::= { pkcs-1 8 } */

/* RFC 8410 */
#define LTTLS_OID_X25519                        MBEDTLS_OID_ISO_IDENTIFIED_ORG MBEDTLS_OID_ORG_GOV "\x6E"
#define LTTLS_OID_ED25519                       MBEDTLS_OID_ISO_IDENTIFIED_ORG MBEDTLS_OID_ORG_GOV "\x70"

/* Digest algorithms  */
#define MBEDTLS_OID_DIGEST_ALG_SHA224           MBEDTLS_OID_NIST_ALG "\x02\x04" /**< id-sha224 OBJECT IDENTIFIER ::= { joint-iso-itu-t(2) country(16) us(840) organization(1) gov(101) csor(3) nistalgorithm(4) hashalgs(2) 4 } */
#define MBEDTLS_OID_DIGEST_ALG_SHA256           MBEDTLS_OID_NIST_ALG "\x02\x01" /**< id-sha256 OBJECT IDENTIFIER ::= { joint-iso-itu-t(2) country(16) us(840) organization(1) gov(101) csor(3) nistalgorithm(4) hashalgs(2) 1 } */
#define MBEDTLS_OID_DIGEST_ALG_SHA384           MBEDTLS_OID_NIST_ALG "\x02\x02" /**< id-sha384 OBJECT IDENTIFIER ::= { joint-iso-itu-t(2) country(16) us(840) organization(1) gov(101) csor(3) nistalgorithm(4) hashalgs(2) 2 } */
#define MBEDTLS_OID_DIGEST_ALG_SHA512           MBEDTLS_OID_NIST_ALG "\x02\x03" /**< id-sha512 OBJECT IDENTIFIER ::= { joint-iso-itu-t(2) country(16) us(840) organization(1) gov(101) csor(3) nistalgorithm(4) hashalgs(2) 3 } */
#define MBEDTLS_OID_DIGEST_ALG_RIPEMD160        MBEDTLS_OID_TELETRUST "\x03\x02\x01" /**< id-ripemd160 OBJECT IDENTIFIER :: { iso(1) identified-organization(3) teletrust(36) algorithm(3) hashAlgorithm(2) ripemd160(1) } */
#define MBEDTLS_OID_HMAC_SHA224                 MBEDTLS_OID_RSA_COMPANY "\x02\x08" /**< id-hmacWithSHA224 OBJECT IDENTIFIER ::= { iso(1) member-body(2) us(840) rsadsi(113549) digestAlgorithm(2) 8 } */
#define MBEDTLS_OID_HMAC_SHA256                 MBEDTLS_OID_RSA_COMPANY "\x02\x09" /**< id-hmacWithSHA256 OBJECT IDENTIFIER ::= { iso(1) member-body(2) us(840) rsadsi(113549) digestAlgorithm(2) 9 } */
#define MBEDTLS_OID_HMAC_SHA384                 MBEDTLS_OID_RSA_COMPANY "\x02\x0A" /**< id-hmacWithSHA384 OBJECT IDENTIFIER ::= { iso(1) member-body(2) us(840) rsadsi(113549) digestAlgorithm(2) 10 } */
#define MBEDTLS_OID_HMAC_SHA512                 MBEDTLS_OID_RSA_COMPANY "\x02\x0B" /**< id-hmacWithSHA512 OBJECT IDENTIFIER ::= { iso(1) member-body(2) us(840) rsadsi(113549) digestAlgorithm(2) 11 } */

/* Encryption algorithms */
#define MBEDTLS_OID_AES                         MBEDTLS_OID_NIST_ALG "\x01" /** aes OBJECT IDENTIFIER ::= { joint-iso-itu-t(2) country(16) us(840) organization(1) gov(101) csor(3) nistAlgorithm(4) 1 } */

/* Key Wrapping algorithms, RFC 5649 */
#define MBEDTLS_OID_AES128_KW                   MBEDTLS_OID_AES "\x05" /** id-aes128-wrap     OBJECT IDENTIFIER ::= { aes 5 } */
#define MBEDTLS_OID_AES128_KWP                  MBEDTLS_OID_AES "\x08" /** id-aes128-wrap-pad OBJECT IDENTIFIER ::= { aes 8 } */
#define MBEDTLS_OID_AES192_KW                   MBEDTLS_OID_AES "\x19" /** id-aes192-wrap     OBJECT IDENTIFIER ::= { aes 25 } */
#define MBEDTLS_OID_AES192_KWP                  MBEDTLS_OID_AES "\x1c" /** id-aes192-wrap-pad OBJECT IDENTIFIER ::= { aes 28 } */
#define MBEDTLS_OID_AES256_KW                   MBEDTLS_OID_AES "\x2d" /** id-aes256-wrap     OBJECT IDENTIFIER ::= { aes 45 } */
#define MBEDTLS_OID_AES256_KWP                  MBEDTLS_OID_AES "\x30" /** id-aes256-wrap-pad OBJECT IDENTIFIER ::= { aes 48 } */

/* PKCS#5 OIDs */
#define MBEDTLS_OID_PKCS5_PBKDF2                MBEDTLS_OID_PKCS5 "\x0c" /**< id-PBKDF2 OBJECT IDENTIFIER ::= {pkcs-5 12} */
#define MBEDTLS_OID_PKCS5_PBES2                 MBEDTLS_OID_PKCS5 "\x0d" /**< id-PBES2 OBJECT IDENTIFIER ::= {pkcs-5 13} */
#define MBEDTLS_OID_PKCS5_PBMAC1                MBEDTLS_OID_PKCS5 "\x0e" /**< id-PBMAC1 OBJECT IDENTIFIER ::= {pkcs-5 14} */

/* PKCS#8 OIDs */
#define MBEDTLS_OID_PKCS9_CSR_EXT_REQ           MBEDTLS_OID_PKCS9 "\x0e" /**< extensionRequest OBJECT IDENTIFIER ::= {pkcs-9 14} */

/* PKCS#12 PBE OIDs */
#define MBEDTLS_OID_PKCS12_PBE                  MBEDTLS_OID_PKCS12 "\x01" /**< pkcs-12PbeIds OBJECT IDENTIFIER ::= {pkcs-12 1} */

/* EC key algorithms from RFC 5480 */

/* id-ecPublicKey OBJECT IDENTIFIER ::= {
 * iso(1) member-body(2) us(840) ansi-X9-62(10045) keyType(2) 1 } */
#define MBEDTLS_OID_EC_ALG_UNRESTRICTED         MBEDTLS_OID_ANSI_X9_62 "\x02\01"

/* id-ecDH OBJECT IDENTIFIER ::= {
 * iso(1) identified-organization(3) certicom(132) schemes(1) ecdh(12) } */
#define MBEDTLS_OID_EC_ALG_ECDH                 MBEDTLS_OID_CERTICOM "\x01\x0c"

/* ECParameters namedCurve identifiers, from RFC 5480, RFC 5639, and SEC2 */

/* secp192r1 OBJECT IDENTIFIER ::= {
 *   iso(1) member-body(2) us(840) ansi-X9-62(10045) curves(3) prime(1) 1 } */
#define MBEDTLS_OID_EC_GRP_SECP192R1            MBEDTLS_OID_ANSI_X9_62 "\x03\x01\x01"

/* secp224r1 OBJECT IDENTIFIER ::= {
 *   iso(1) identified-organization(3) certicom(132) curve(0) 33 } */
#define MBEDTLS_OID_EC_GRP_SECP224R1            MBEDTLS_OID_CERTICOM "\x00\x21"

/* secp256r1 OBJECT IDENTIFIER ::= {
 *   iso(1) member-body(2) us(840) ansi-X9-62(10045) curves(3) prime(1) 7 } */
#define MBEDTLS_OID_EC_GRP_SECP256R1            MBEDTLS_OID_ANSI_X9_62 "\x03\x01\x07"

/* secp384r1 OBJECT IDENTIFIER ::= {
 *   iso(1) identified-organization(3) certicom(132) curve(0) 34 } */
#define MBEDTLS_OID_EC_GRP_SECP384R1            MBEDTLS_OID_CERTICOM "\x00\x22"

/* secp521r1 OBJECT IDENTIFIER ::= {
 *   iso(1) identified-organization(3) certicom(132) curve(0) 35 } */
#define MBEDTLS_OID_EC_GRP_SECP521R1            MBEDTLS_OID_CERTICOM "\x00\x23"

/* secp192k1 OBJECT IDENTIFIER ::= {
 *   iso(1) identified-organization(3) certicom(132) curve(0) 31 } */
#define MBEDTLS_OID_EC_GRP_SECP192K1            MBEDTLS_OID_CERTICOM "\x00\x1f"

/* secp224k1 OBJECT IDENTIFIER ::= {
 *   iso(1) identified-organization(3) certicom(132) curve(0) 32 } */
#define MBEDTLS_OID_EC_GRP_SECP224K1            MBEDTLS_OID_CERTICOM "\x00\x20"

/* secp256k1 OBJECT IDENTIFIER ::= {
 *   iso(1) identified-organization(3) certicom(132) curve(0) 10 } */
#define MBEDTLS_OID_EC_GRP_SECP256K1            MBEDTLS_OID_CERTICOM "\x00\x0a"

/* RFC 5639 4.1
 * ecStdCurvesAndGeneration OBJECT IDENTIFIER::= {iso(1)
 * identified-organization(3) teletrust(36) algorithm(3) signature-
 * algorithm(3) ecSign(2) 8}
 * ellipticCurve OBJECT IDENTIFIER ::= {ecStdCurvesAndGeneration 1}
 * versionOne OBJECT IDENTIFIER ::= {ellipticCurve 1} */
#define MBEDTLS_OID_EC_BRAINPOOL_V1             MBEDTLS_OID_TELETRUST "\x03\x03\x02\x08\x01\x01"

/* brainpoolP256r1 OBJECT IDENTIFIER ::= {versionOne 7} */
#define MBEDTLS_OID_EC_GRP_BP256R1              MBEDTLS_OID_EC_BRAINPOOL_V1 "\x07"

/* brainpoolP384r1 OBJECT IDENTIFIER ::= {versionOne 11} */
#define MBEDTLS_OID_EC_GRP_BP384R1              MBEDTLS_OID_EC_BRAINPOOL_V1 "\x0B"

/* brainpoolP512r1 OBJECT IDENTIFIER ::= {versionOne 13} */
#define MBEDTLS_OID_EC_GRP_BP512R1              MBEDTLS_OID_EC_BRAINPOOL_V1 "\x0D"

/* SEC1 C.1
 * prime-field OBJECT IDENTIFIER ::= { id-fieldType 1 }
 * id-fieldType OBJECT IDENTIFIER ::= { ansi-X9-62 fieldType(1)}
 */
#define MBEDTLS_OID_ANSI_X9_62_FIELD_TYPE       MBEDTLS_OID_ANSI_X9_62 "\x01"
#define MBEDTLS_OID_ANSI_X9_62_PRIME_FIELD      MBEDTLS_OID_ANSI_X9_62_FIELD_TYPE "\x01"

/* ECDSA signature identifiers, from RFC 5480 */
#define MBEDTLS_OID_ANSI_X9_62_SIG              MBEDTLS_OID_ANSI_X9_62 "\x04"     /* signatures(4) */
#define MBEDTLS_OID_ANSI_X9_62_SIG_SHA2         MBEDTLS_OID_ANSI_X9_62_SIG "\x03" /* ecdsa-with-SHA2(3) */

/* ecdsa-with-SHA224 OBJECT IDENTIFIER ::= {
 *   iso(1) member-body(2) us(840) ansi-X9-62(10045) signatures(4)
 *   ecdsa-with-SHA2(3) 1 } */
#define MBEDTLS_OID_ECDSA_SHA224                MBEDTLS_OID_ANSI_X9_62_SIG_SHA2 "\x01"

/* ecdsa-with-SHA256 OBJECT IDENTIFIER ::= {
 *   iso(1) member-body(2) us(840) ansi-X9-62(10045) signatures(4)
 *   ecdsa-with-SHA2(3) 2 } */
#define MBEDTLS_OID_ECDSA_SHA256                MBEDTLS_OID_ANSI_X9_62_SIG_SHA2 "\x02"

/* ecdsa-with-SHA384 OBJECT IDENTIFIER ::= {
 *   iso(1) member-body(2) us(840) ansi-X9-62(10045) signatures(4)
 *   ecdsa-with-SHA2(3) 3 } */
#define MBEDTLS_OID_ECDSA_SHA384                MBEDTLS_OID_ANSI_X9_62_SIG_SHA2 "\x03"

/* ecdsa-with-SHA512 OBJECT IDENTIFIER ::= {
 *   iso(1) member-body(2) us(840) ansi-X9-62(10045) signatures(4)
 *   ecdsa-with-SHA2(3) 4 } */
#define MBEDTLS_OID_ECDSA_SHA512                MBEDTLS_OID_ANSI_X9_62_SIG_SHA2 "\x04"

/*
 * Bit masks for each of the components of an ASN.1 tag as specified in
 * ITU X.690 (08/2015), section 8.1 "General rules for encoding",
 * paragraph 8.1.2.2:
 *
 * Bit  8     7   6   5          1
 *     +-------+-----+------------+
 *     | Class | P/C | Tag number |
 *     +-------+-----+------------+
 */
#define MBEDTLS_ASN1_TAG_CLASS_MASK               0xC0
#define MBEDTLS_ASN1_TAG_PC_MASK                  0x20
#define MBEDTLS_ASN1_TAG_VALUE_MASK               0x1F

/*
 * DER constants
 * These constants comply with the DER encoded ASN.1 type tags: type class (2 bits), constructed (1 bit), type (5 bits)
 * An example DER sequence is: 02 01 05
 * - 0x02 -- tag indicating INTEGER
 * - 0x01 -- length in octets
 * - 0x05 -- value
 * 
 * Tag examples in DER:
 * 0x30 : constructed universal sequence, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE
 * 0xa0 : constructed context-specific type, MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_CONSTRUCTED
 * 0x02 : universal type integer, MBEDTLS_ASN1_INTEGER
 * 0x82 : context-specifc type integer, MBEDTLS_ASN1_CONTEXT_SPECIFIC | MBEDTLS_ASN1_INTEGER
 * 0x06 : universal type object ID, MBEDTLS_ASN1_OID
 * 0x31 : constructed universal type set, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SET
 * 0x17 : universal type utctime, MBEDTLS_ASN1_UTC_TIME
 * 0x18 : universal type generalized time, MBEDTLS_ASN1_GENERALIZED_TIME
 * 0x03 : universal type bitstring, MBEDTLS_ASN1_BIT_STRING
 */
#define MBEDTLS_ASN1_BOOLEAN                      0x01
#define MBEDTLS_ASN1_INTEGER                      0x02
#define MBEDTLS_ASN1_BIT_STRING                   0x03
#define MBEDTLS_ASN1_OCTET_STRING                 0x04
#define MBEDTLS_ASN1_NULL                         0x05
#define MBEDTLS_ASN1_OID                          0x06
#define MBEDTLS_ASN1_ENUMERATED                   0x0A
#define MBEDTLS_ASN1_UTF8_STRING                  0x0C
#define MBEDTLS_ASN1_SEQUENCE                     0x10
#define MBEDTLS_ASN1_SET                          0x11
#define MBEDTLS_ASN1_PRINTABLE_STRING             0x13
#define MBEDTLS_ASN1_T61_STRING                   0x14
#define MBEDTLS_ASN1_IA5_STRING                   0x16
#define MBEDTLS_ASN1_UTC_TIME                     0x17
#define MBEDTLS_ASN1_GENERALIZED_TIME             0x18
#define MBEDTLS_ASN1_UNIVERSAL_STRING             0x1C
#define MBEDTLS_ASN1_BMP_STRING                   0x1E
#define MBEDTLS_ASN1_PRIMITIVE                    0x00
#define MBEDTLS_ASN1_CONSTRUCTED                  0x20
#define MBEDTLS_ASN1_CONTEXT_SPECIFIC             0x80

/* X509 Verify codes */
#define MBEDTLS_X509_BADCERT_EXPIRED              0x01     /**< The certificate validity has expired. */
#define MBEDTLS_X509_BADCERT_REVOKED              0x02     /**< The certificate has been revoked (is on a CRL). */
#define MBEDTLS_X509_BADCERT_CN_MISMATCH          0x04     /**< The certificate Common Name (CN) does not match with the expected CN. */
#define MBEDTLS_X509_BADCERT_NOT_TRUSTED          0x08     /**< The certificate is not correctly signed by the trusted CA. */
#define MBEDTLS_X509_BADCRL_NOT_TRUSTED           0x10     /**< The CRL is not correctly signed by the trusted CA. */
#define MBEDTLS_X509_BADCRL_EXPIRED               0x20     /**< The CRL is expired. */
#define MBEDTLS_X509_BADCERT_MISSING              0x40     /**< Certificate was missing. */
#define MBEDTLS_X509_BADCERT_SKIP_VERIFY          0x80     /**< Certificate verification was skipped. */
#define MBEDTLS_X509_BADCERT_OTHER              0x0100     /**< Other reason (can be used by verify callback) */
#define MBEDTLS_X509_BADCERT_FUTURE             0x0200     /**< The certificate validity starts in the future. */
#define MBEDTLS_X509_BADCRL_FUTURE              0x0400     /**< The CRL is from the future */
#define MBEDTLS_X509_BADCERT_KEY_USAGE          0x0800     /**< Usage does not match the keyUsage extension. */
#define MBEDTLS_X509_BADCERT_EXT_KEY_USAGE      0x1000     /**< Usage does not match the extendedKeyUsage extension. */
#define MBEDTLS_X509_BADCERT_NS_CERT_TYPE       0x2000     /**< Usage does not match the nsCertType extension. */
#define MBEDTLS_X509_BADCERT_BAD_MD             0x4000     /**< The certificate is signed with an unacceptable hash. */
#define MBEDTLS_X509_BADCERT_BAD_PK             0x8000     /**< The certificate is signed with an unacceptable PK alg (eg RSA vs ECDSA). */
#define MBEDTLS_X509_BADCERT_BAD_KEY          0x010000     /**< The certificate is signed with an unacceptable key (eg bad curve, RSA too short). */
#define MBEDTLS_X509_BADCRL_BAD_MD            0x020000     /**< The CRL is signed with an unacceptable hash. */
#define MBEDTLS_X509_BADCRL_BAD_PK            0x040000     /**< The CRL is signed with an unacceptable PK alg (eg RSA vs ECDSA). */
#define MBEDTLS_X509_BADCRL_BAD_KEY           0x080000     /**< The CRL is signed with an unacceptable key (eg bad curve, RSA too short). */

/*
 * X.509 v3 Subject Alternative Name types.
 *      otherName                       [0]     OtherName,
 *      rfc822Name                      [1]     IA5String,
 *      dNSName                         [2]     IA5String,
 *      x400Address                     [3]     ORAddress,
 *      directoryName                   [4]     Name,
 *      ediPartyName                    [5]     EDIPartyName,
 *      uniformResourceIdentifier       [6]     IA5String,
 *      iPAddress                       [7]     OCTET STRING,
 *      registeredID                    [8]     OBJECT IDENTIFIER
 */
#define MBEDTLS_X509_SAN_OTHER_NAME                      0
#define MBEDTLS_X509_SAN_RFC822_NAME                     1
#define MBEDTLS_X509_SAN_DNS_NAME                        2
#define MBEDTLS_X509_SAN_X400_ADDRESS_NAME               3
#define MBEDTLS_X509_SAN_DIRECTORY_NAME                  4
#define MBEDTLS_X509_SAN_EDI_PARTY_NAME                  5
#define MBEDTLS_X509_SAN_UNIFORM_RESOURCE_IDENTIFIER     6
#define MBEDTLS_X509_SAN_IP_ADDRESS                      7
#define MBEDTLS_X509_SAN_REGISTERED_ID                   8

/* X.509 v3 Key Usage Extension flags */
#define MBEDTLS_X509_KU_DIGITAL_SIGNATURE            (0x80)  /* bit 0 */
#define MBEDTLS_X509_KU_NON_REPUDIATION              (0x40)  /* bit 1 */
#define MBEDTLS_X509_KU_KEY_ENCIPHERMENT             (0x20)  /* bit 2 */
#define MBEDTLS_X509_KU_DATA_ENCIPHERMENT            (0x10)  /* bit 3 */
#define MBEDTLS_X509_KU_KEY_AGREEMENT                (0x08)  /* bit 4 */
#define MBEDTLS_X509_KU_KEY_CERT_SIGN                (0x04)  /* bit 5 */
#define MBEDTLS_X509_KU_CRL_SIGN                     (0x02)  /* bit 6 */
#define MBEDTLS_X509_KU_ENCIPHER_ONLY                (0x01)  /* bit 7 */
#define MBEDTLS_X509_KU_DECIPHER_ONLY              (0x8000)  /* bit 8 */

/* Netscape certificate types
 * (http://www.mozilla.org/projects/security/pki/nss/tech-notes/tn3.html)
 */
#define MBEDTLS_X509_NS_CERT_TYPE_SSL_CLIENT         (0x80)  /* bit 0 */
#define MBEDTLS_X509_NS_CERT_TYPE_SSL_SERVER         (0x40)  /* bit 1 */
#define MBEDTLS_X509_NS_CERT_TYPE_EMAIL              (0x20)  /* bit 2 */
#define MBEDTLS_X509_NS_CERT_TYPE_OBJECT_SIGNING     (0x10)  /* bit 3 */
#define MBEDTLS_X509_NS_CERT_TYPE_RESERVED           (0x08)  /* bit 4 */
#define MBEDTLS_X509_NS_CERT_TYPE_SSL_CA             (0x04)  /* bit 5 */
#define MBEDTLS_X509_NS_CERT_TYPE_EMAIL_CA           (0x02)  /* bit 6 */
#define MBEDTLS_X509_NS_CERT_TYPE_OBJECT_SIGNING_CA  (0x01)  /* bit 7 */

/* X.509 extension types
 *
 * Comments refer to the status for using certificates. Status can be
 * different for writing certificates or reading CRLs or CSRs.
 *
 * Those are defined in oid.h as oid.c needs them in a data structure. Since
 * these were previously defined here, let's have aliases for compatibility.
 */
#define MBEDTLS_X509_EXT_AUTHORITY_KEY_IDENTIFIER MBEDTLS_OID_X509_EXT_AUTHORITY_KEY_IDENTIFIER
#define MBEDTLS_X509_EXT_SUBJECT_KEY_IDENTIFIER   MBEDTLS_OID_X509_EXT_SUBJECT_KEY_IDENTIFIER
#define MBEDTLS_X509_EXT_KEY_USAGE                MBEDTLS_OID_X509_EXT_KEY_USAGE
#define MBEDTLS_X509_EXT_CERTIFICATE_POLICIES     MBEDTLS_OID_X509_EXT_CERTIFICATE_POLICIES
#define MBEDTLS_X509_EXT_POLICY_MAPPINGS          MBEDTLS_OID_X509_EXT_POLICY_MAPPINGS
#define MBEDTLS_X509_EXT_SUBJECT_ALT_NAME         MBEDTLS_OID_X509_EXT_SUBJECT_ALT_NAME         /* Supported (DNS) */
#define MBEDTLS_X509_EXT_ISSUER_ALT_NAME          MBEDTLS_OID_X509_EXT_ISSUER_ALT_NAME
#define MBEDTLS_X509_EXT_SUBJECT_DIRECTORY_ATTRS  MBEDTLS_OID_X509_EXT_SUBJECT_DIRECTORY_ATTRS
#define MBEDTLS_X509_EXT_BASIC_CONSTRAINTS        MBEDTLS_OID_X509_EXT_BASIC_CONSTRAINTS        /* Supported */
#define MBEDTLS_X509_EXT_NAME_CONSTRAINTS         MBEDTLS_OID_X509_EXT_NAME_CONSTRAINTS
#define MBEDTLS_X509_EXT_POLICY_CONSTRAINTS       MBEDTLS_OID_X509_EXT_POLICY_CONSTRAINTS
#define MBEDTLS_X509_EXT_EXTENDED_KEY_USAGE       MBEDTLS_OID_X509_EXT_EXTENDED_KEY_USAGE
#define MBEDTLS_X509_EXT_CRL_DISTRIBUTION_POINTS  MBEDTLS_OID_X509_EXT_CRL_DISTRIBUTION_POINTS
#define MBEDTLS_X509_EXT_INIHIBIT_ANYPOLICY       MBEDTLS_OID_X509_EXT_INIHIBIT_ANYPOLICY
#define MBEDTLS_X509_EXT_FRESHEST_CRL             MBEDTLS_OID_X509_EXT_FRESHEST_CRL
#define MBEDTLS_X509_EXT_NS_CERT_TYPE             MBEDTLS_OID_X509_EXT_NS_CERT_TYPE

/* Storage format identifiers, Recognized formats: PEM and DER */
#define MBEDTLS_X509_FORMAT_DER                 (1)
#define MBEDTLS_X509_FORMAT_PEM                 (2)

#define MBEDTLS_X509_MAX_DN_NAME_SIZE         (256)   /**< Maximum value size of a DN entry */

#define LT_EC_POINT_COMPRESSED2                 (2)
#define LT_EC_POINT_COMPRESSED3                 (3)
#define LT_EC_POINT_UNCOMPRESSED                (4)

LT_EXTERN_C_END
#endif  // ROKU_LT_SOURCE_LT_NET_X509_LTNETX509DEFS_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-May-22   gallienus   created
 */
