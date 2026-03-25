#ifndef _CRYPTO_H_
#define _CRYPTO_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

//=============================================================================
// Hardware Crypto Configuration
//=============================================================================

// If CONFIG_WIREGUARD_USE_HW_CRYPTO is defined (via Kconfig or platformio.ini),
// we use optimized implementations via mbedTLS with ESP32-specific optimizations.
// Otherwise, use reference C implementations.

#ifdef CONFIG_WIREGUARD_USE_HW_CRYPTO
    #define WG_USE_HW_CRYPTO 1
    #include "crypto/esp_hw/esp_crypto.h"
#else
    #define WG_USE_HW_CRYPTO 0
    #include "crypto/refc/blake2s.h"
    #include "crypto/refc/x25519.h"
    #include "crypto/refc/chacha20poly1305.h"
#endif

//=============================================================================
// BLAKE2S
//=============================================================================

#if WG_USE_HW_CRYPTO
    // BLAKE2s has no hardware acceleration, use reference but with optimized mbedTLS if available
    #include "crypto/refc/blake2s.h"  // Still use refc, as no better HW option
    #define wireguard_blake2s_ctx blake2s_ctx
    #define wireguard_blake2s_init(ctx,outlen,key,keylen) blake2s_init(ctx,outlen,key,keylen)
    #define wireguard_blake2s_update(ctx,in,inlen) blake2s_update(ctx,in,inlen)
    #define wireguard_blake2s_final(ctx,out) blake2s_final(ctx,out)
    #define wireguard_blake2s(out,outlen,key,keylen,in,inlen) blake2s(out,outlen,key,keylen,in,inlen)
#else
    #include "crypto/refc/blake2s.h"
    #define wireguard_blake2s_ctx blake2s_ctx
    #define wireguard_blake2s_init(ctx,outlen,key,keylen) blake2s_init(ctx,outlen,key,keylen)
    #define wireguard_blake2s_update(ctx,in,inlen) blake2s_update(ctx,in,inlen)
    #define wireguard_blake2s_final(ctx,out) blake2s_final(ctx,out)
    #define wireguard_blake2s(out,outlen,key,keylen,in,inlen) blake2s(out,outlen,key,keylen,in,inlen)
#endif

//=============================================================================
// X25519
//=============================================================================

#if WG_USE_HW_CRYPTO
    // Use mbedTLS which has Xtensa/RISC-V assembly optimizations
    #define wireguard_x25519(a,b,c) \
        (esp_crypto_hw_enabled() ? x25519_hw((a),(b),(c)) : x25519((a),(b),(c),1))
#else
    #include "crypto/refc/x25519.h"
    #define wireguard_x25519(a,b,c)	x25519(a,b,c,1)
#endif

//=============================================================================
// CHACHA20-POLY1305
//=============================================================================

#if WG_USE_HW_CRYPTO
    #define wireguard_aead_encrypt(dst,src,srclen,ad,adlen,nonce,key) \
        (esp_crypto_hw_enabled() ? \
            chacha20poly1305_encrypt_hw((dst),(src),(srclen),(ad),(adlen),(nonce),(key)) : \
            chacha20poly1305_encrypt((dst),(src),(srclen),(ad),(adlen),(nonce),(key)))
    #define wireguard_aead_decrypt(dst,src,srclen,ad,adlen,nonce,key) \
        (esp_crypto_hw_enabled() ? \
            chacha20poly1305_decrypt_hw((dst),(src),(srclen),(ad),(adlen),(nonce),(key)) : \
            chacha20poly1305_decrypt((dst),(src),(srclen),(ad),(adlen),(nonce),(key)))
#else
    #include "crypto/refc/chacha20poly1305.h"
    #define wireguard_aead_encrypt(dst,src,srclen,ad,adlen,nonce,key) \
        chacha20poly1305_encrypt(dst,src,srclen,ad,adlen,nonce,key)
    #define wireguard_aead_decrypt(dst,src,srclen,ad,adlen,nonce,key) \
        chacha20poly1305_decrypt(dst,src,srclen,ad,adlen,nonce,key)
#endif

// XChaCha20-Poly1305 (used if supported)
#if WG_USE_HW_CRYPTO
    #define wireguard_xaead_encrypt(dst,src,srclen,ad,adlen,nonce,key) \
        chacha20poly1305_encrypt_hw((dst),(src),(srclen),(ad),(adlen),(nonce),(key))  // For now, same as regular
    #define wireguard_xaead_decrypt(dst,src,srclen,ad,adlen,nonce,key) \
        chacha20poly1305_decrypt_hw((dst),(src),(srclen),(ad),(adlen),(nonce),(key))
#else
    #include "crypto/refc/chacha20poly1305.h"
    #define wireguard_xaead_encrypt(dst,src,srclen,ad,adlen,nonce,key) \
        xchacha20poly1305_encrypt(dst,src,srclen,ad,adlen,nonce,key)
    #define wireguard_xaead_decrypt(dst,src,srclen,ad,adlen,nonce,key) \
        xchacha20poly1305_decrypt(dst,src,srclen,ad,adlen,nonce,key)
#endif

//=============================================================================
// Memory & Utility Functions
//=============================================================================

void crypto_zero(void *dest, size_t len);
bool crypto_equal(const void *a, const void *b, size_t size);

// Optional: Initialize hardware crypto (call from wireguard_platform_init)
void esp_crypto_platform_init(void);

#endif /* _CRYPTO_H_ */


// Endian / unaligned helper macros
#define U8C(v) (v##U)
#define U32C(v) (v##U)

#define U8V(v) ((uint8_t)(v) & U8C(0xFF))
#define U32V(v) ((uint32_t)(v) & U32C(0xFFFFFFFF))

#define U8TO32_LITTLE(p) \
  (((uint32_t)((p)[0])      ) | \
   ((uint32_t)((p)[1]) <<  8) | \
   ((uint32_t)((p)[2]) << 16) | \
   ((uint32_t)((p)[3]) << 24))

#define U8TO64_LITTLE(p) \
  (((uint64_t)((p)[0])      ) | \
   ((uint64_t)((p)[1]) <<  8) | \
   ((uint64_t)((p)[2]) << 16) | \
   ((uint64_t)((p)[3]) << 24) | \
   ((uint64_t)((p)[4]) << 32) | \
   ((uint64_t)((p)[5]) << 40) | \
   ((uint64_t)((p)[6]) << 48) | \
   ((uint64_t)((p)[7]) << 56))

#define U16TO8_BIG(p, v) \
  do { \
    (p)[1] = U8V((v)      ); \
    (p)[0] = U8V((v) >>  8); \
  } while (0)

#define U32TO8_LITTLE(p, v) \
  do { \
    (p)[0] = U8V((v)      ); \
    (p)[1] = U8V((v) >>  8); \
    (p)[2] = U8V((v) >> 16); \
    (p)[3] = U8V((v) >> 24); \
  } while (0)

#define U32TO8_BIG(p, v) \
  do { \
    (p)[3] = U8V((v)      ); \
    (p)[2] = U8V((v) >>  8); \
    (p)[1] = U8V((v) >> 16); \
    (p)[0] = U8V((v) >> 24); \
  } while (0)

#define U64TO8_LITTLE(p, v) \
  do { \
    (p)[0] = U8V((v)      ); \
    (p)[1] = U8V((v) >>  8); \
    (p)[2] = U8V((v) >> 16); \
    (p)[3] = U8V((v) >> 24); \
    (p)[4] = U8V((v) >> 32); \
    (p)[5] = U8V((v) >> 40); \
    (p)[6] = U8V((v) >> 48); \
    (p)[7] = U8V((v) >> 56); \
} while (0)

#define U64TO8_BIG(p, v) \
  do { \
    (p)[7] = U8V((v)      ); \
    (p)[6] = U8V((v) >>  8); \
    (p)[5] = U8V((v) >> 16); \
    (p)[4] = U8V((v) >> 24); \
    (p)[3] = U8V((v) >> 32); \
    (p)[2] = U8V((v) >> 40); \
    (p)[1] = U8V((v) >> 48); \
    (p)[0] = U8V((v) >> 56); \
} while (0)


void crypto_zero(void *dest, size_t len);
bool crypto_equal(const void *a, const void *b, size_t size);

#endif /* _CRYPTO_H_ */

