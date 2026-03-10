#include "bool.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

// Signature SELP : [2.0]: SELP bool (c) 2026 003x2022 223222x22
static const uint8_t SELP_SIGNATURE[32] = {
    0x00, 0x03, 0x78, 0x32, 0x30, 0x32, 0x32, 0x20,
    0x32, 0x32, 0x33, 0x32, 0x32, 0x32, 0x78, 0x32,
    0x32, 0x00, 0x53, 0x45, 0x4C, 0x50, 0x20, 0x62,
    0x6F, 0x6F, 0x6C, 0x20, 0x28, 0x63, 0x29, 0x20
};

// Chiffrement léger (XOR avec pattern)
static void xor_encrypt(uint8_t *data, size_t len, const uint8_t *key, size_t key_len) {
    for (size_t i = 0; i < len; i++) {
        data[i] ^= key[i % key_len];
    }
}

// Chiffrement AES
static int aes_encrypt(const uint8_t *input, size_t in_len,
                       uint8_t *output, size_t *out_len,
                       const uint8_t *key, int bits) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    const EVP_CIPHER *cipher = (bits == 256) ? 
        EVP_aes_256_cbc() : EVP_aes_128_cbc();
    
    uint8_t iv[16];
    RAND_bytes(iv, sizeof(iv));
    
    // Copier l'IV au début
    memcpy(output, iv, sizeof(iv));
    *out_len = sizeof(iv);
    
    EVP_EncryptInit_ex(ctx, cipher, NULL, key, iv);
    
    int len;
    EVP_EncryptUpdate(ctx, output + *out_len, &len, input, in_len);
    *out_len += len;
    
    EVP_EncryptFinal_ex(ctx, output + *out_len, &len);
    *out_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    return SELP_OK;
}

// Générer la signature SELP
void selp_generate_signature(uint32_t signature[8], const uint8_t *data, size_t len) {
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, len);
    SHA256_Update(&sha256, SELP_SIGNATURE, sizeof(SELP_SIGNATURE));
    
    uint8_t hash[32];
    SHA256_Final(hash, &sha256);
    
    // Convertir en 8 uint32_t
    for (int i = 0; i < 8; i++) {
        signature[i] = (hash[i*4] << 24) | (hash[i*4+1] << 16) |
                       (hash[i*4+2] << 8) | hash[i*4+3];
    }
}

// Vérifier la signature
int selp_verify_signature(const uint32_t signature[8], const uint8_t *data, size_t len) {
    uint32_t expected[8];
    selp_generate_signature(expected, data, len);
    
    for (int i = 0; i < 8; i++) {
        if (signature[i] != expected[i]) {
            return SELP_ERR_SIGNATURE;
        }
    }
    return SELP_OK;
}
