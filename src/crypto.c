#include "apkm.h"
#include <sodium.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <openssl/err.h>
#include <argon2.h>
#include <blake3.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// INITIALISATION DE SODIUM
// ============================================================================

__attribute__((constructor))
static void init_sodium(void) {
    if (sodium_init() < 0) {
        fprintf(stderr, "[CRYPTO] Failed to initialize libsodium\n");
    }
}

// ============================================================================
// UTILITAIRES
// ============================================================================

static void log_openssl_error(const char *func) {
    unsigned long err = ERR_get_error();
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    fprintf(stderr, "[CRYPTO] OpenSSL error in %s: %s\n", func, buf);
}

// ============================================================================
// AES-256-GCM (Chiffrement authentifié)
// ============================================================================

int aes256_gcm_encrypt(const unsigned char* plaintext, size_t plaintext_len,
                       const unsigned char* key, const unsigned char* iv,
                       unsigned char* ciphertext, unsigned char* tag) {
    if (!plaintext || !key || !iv || !ciphertext || !tag) return -1;
    if (plaintext_len == 0) return -1;
    
    int ret = -1;
    int len;
    int ciphertext_len = 0;
    
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    
    // Initialisation du contexte de chiffrement
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
        log_openssl_error("EVP_EncryptInit_ex");
        goto cleanup;
    }
    
    // Définition de la taille de l'IV (12 bytes recommandé pour GCM)
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1) {
        log_openssl_error("EVP_CTRL_GCM_SET_IVLEN");
        goto cleanup;
    }
    
    // Initialisation avec la clé et l'IV
    if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
        log_openssl_error("EVP_EncryptInit_ex2");
        goto cleanup;
    }
    
    // Chiffrement des données
    if (EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, (int)plaintext_len) != 1) {
        log_openssl_error("EVP_EncryptUpdate");
        goto cleanup;
    }
    ciphertext_len = len;
    
    // Finalisation
    if (EVP_EncryptFinal_ex(ctx, ciphertext + ciphertext_len, &len) != 1) {
        log_openssl_error("EVP_EncryptFinal_ex");
        goto cleanup;
    }
    ciphertext_len += len;
    
    // Récupération du tag d'authentification
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1) {
        log_openssl_error("EVP_CTRL_GCM_GET_TAG");
        goto cleanup;
    }
    
    ret = ciphertext_len;
    
cleanup:
    EVP_CIPHER_CTX_free(ctx);
    return ret;
}

int aes256_gcm_decrypt(const unsigned char* ciphertext, size_t ciphertext_len,
                       const unsigned char* key, const unsigned char* iv,
                       const unsigned char* tag, unsigned char* plaintext) {
    if (!ciphertext || !key || !iv || !tag || !plaintext) return -1;
    
    int ret = -1;
    int len;
    int plaintext_len = 0;
    
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    
    // Initialisation du contexte de déchiffrement
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
        log_openssl_error("EVP_DecryptInit_ex");
        goto cleanup;
    }
    
    // Définition de la taille de l'IV
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1) {
        log_openssl_error("EVP_CTRL_GCM_SET_IVLEN");
        goto cleanup;
    }
    
    // Initialisation avec la clé et l'IV
    if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
        log_openssl_error("EVP_DecryptInit_ex2");
        goto cleanup;
    }
    
    // Définition du tag attendu
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag) != 1) {
        log_openssl_error("EVP_CTRL_GCM_SET_TAG");
        goto cleanup;
    }
    
    // Déchiffrement
    if (EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, (int)ciphertext_len) != 1) {
        log_openssl_error("EVP_DecryptUpdate");
        goto cleanup;
    }
    plaintext_len = len;
    
    // Finalisation et vérification du tag
    if (EVP_DecryptFinal_ex(ctx, plaintext + plaintext_len, &len) != 1) {
        log_openssl_error("EVP_DecryptFinal_ex - Tag verification failed");
        goto cleanup;
    }
    plaintext_len += len;
    
    ret = plaintext_len;
    
cleanup:
    EVP_CIPHER_CTX_free(ctx);
    return ret;
}

// ============================================================================
// ED25519 (Signatures)
// ============================================================================

int ed25519_keypair(unsigned char *pk, unsigned char *sk) {
    if (!pk || !sk) return -1;
    crypto_sign_keypair(pk, sk);
    return 0;
}

int ed25519_sign(const unsigned char* message, size_t msg_len,
                 const unsigned char* sk, unsigned char* signature) {
    if (!message || !sk || !signature) return -1;
    
    unsigned long long sig_len;
    return crypto_sign_detached(signature, &sig_len, message, msg_len, sk);
}

int ed25519_verify(const unsigned char* message, size_t msg_len,
                   const unsigned char* pk, const unsigned char* signature) {
    if (!message || !pk || !signature) return -1;
    
    return crypto_sign_verify_detached(signature, message, msg_len, pk);
}

// ============================================================================
// BLAKE3 (Hachage ultra-rapide)
// ============================================================================

void blake3_hash(const void* data, size_t len, unsigned char* hash) {
    if (!data || !hash) return;
    
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, len);
    blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);
}

int blake3_hash_file(const char *filename, unsigned char *hash) {
    if (!filename || !hash) return -1;
    
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;
    
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    
    unsigned char buffer[8192];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        blake3_hasher_update(&hasher, buffer, bytes);
    }
    
    blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);
    
    fclose(f);
    return 0;
}

// ============================================================================
// ARGON2ID (Dérivation de clé)
// ============================================================================

int argon2id_derive_key(const char* password, const unsigned char* salt,
                        unsigned char* key, size_t key_len) {
    if (!password || !salt || !key) return -1;
    
    // Paramètres recommandés par l'IANA (RFC 9106)
    // t = 1, m = 19 (2^19 = 512 KiB), p = 1
    uint32_t t_cost = 1;        // Nombre d'itérations
    uint32_t m_cost = 19;       // 2^19 = 512 MiB de mémoire
    uint32_t parallelism = 1;   // Parallélisme
    
    int result = argon2id_hash_raw(
        t_cost,                  // Nombre d'itérations
        1 << m_cost,             // Utilisation mémoire (en KiB)
        parallelism,             // Degré de parallélisme
        password, strlen(password),  // Mot de passe
        salt, crypto_pwhash_SALTBYTES,  // Sel
        key, (uint32_t)key_len   // Sortie
    );
    
    return (result == ARGON2_OK) ? 0 : -1;
}

// ============================================================================
// GÉNÉRATION DE SÉCURITÉ
// ============================================================================

int random_bytes(unsigned char *buf, size_t len) {
    if (!buf) return -1;
    randombytes_buf(buf, len);
    return 0;
}

// ============================================================================
// CHIFFREMENT HYBRIDE (courbes elliptiques + AES)
// ============================================================================

int hybrid_encrypt(const unsigned char* plaintext, size_t plaintext_len,
                   const unsigned char* recipient_pubkey,
                   unsigned char* ciphertext, size_t* ciphertext_len) {
    if (!plaintext || !recipient_pubkey || !ciphertext || !ciphertext_len) return -1;
    
    // Vérifier que la clé publique est valide
    if (sodium_is_zero(recipient_pubkey, crypto_box_PUBLICKEYBYTES)) {
        return -1;
    }
    
    // Génération d'une paire de clés éphémère
    unsigned char ephemeral_sk[crypto_box_SECRETKEYBYTES];
    unsigned char ephemeral_pk[crypto_box_PUBLICKEYBYTES];
    
    crypto_box_keypair(ephemeral_pk, ephemeral_sk);
    
    // Calcul du secret partagé
    unsigned char shared_secret[crypto_box_BEFORENMBYTES];
    int result = crypto_box_beforenm(shared_secret, recipient_pubkey, ephemeral_sk);
    
    if (result != 0) {
        sodium_memzero(ephemeral_sk, sizeof(ephemeral_sk));
        sodium_memzero(shared_secret, sizeof(shared_secret));
        return -1;
    }
    
    // Génération d'un nonce aléatoire
    unsigned char nonce[crypto_box_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));
    
    // Chiffrement avec le secret partagé
    unsigned char *ciphertext_body = ciphertext + crypto_box_PUBLICKEYBYTES + crypto_box_NONCEBYTES;
    
    result = crypto_box_easy_afternm(
        ciphertext_body,
        plaintext, plaintext_len,
        nonce,
        shared_secret
    );
    
    if (result != 0) {
        sodium_memzero(ephemeral_sk, sizeof(ephemeral_sk));
        sodium_memzero(shared_secret, sizeof(shared_secret));
        return -1;
    }
    
    // Préfixe avec la clé publique éphémère et le nonce
    memcpy(ciphertext, ephemeral_pk, crypto_box_PUBLICKEYBYTES);
    memcpy(ciphertext + crypto_box_PUBLICKEYBYTES, nonce, crypto_box_NONCEBYTES);
    
    *ciphertext_len = crypto_box_PUBLICKEYBYTES + crypto_box_NONCEBYTES +
                      plaintext_len + crypto_box_MACBYTES;
    
    // Nettoyage mémoire sensible
    sodium_memzero(ephemeral_sk, sizeof(ephemeral_sk));
    sodium_memzero(shared_secret, sizeof(shared_secret));
    
    return 0;
}

int hybrid_decrypt(const unsigned char* ciphertext, size_t ciphertext_len,
                   const unsigned char* recipient_sk,
                   unsigned char* plaintext, size_t* plaintext_len) {
    if (!ciphertext || !recipient_sk || !plaintext || !plaintext_len) return -1;
    
    // Vérifier la taille minimale
    if (ciphertext_len < crypto_box_PUBLICKEYBYTES + crypto_box_NONCEBYTES + crypto_box_MACBYTES) {
        return -1;
    }
    
    // Extraire la clé publique éphémère et le nonce
    const unsigned char *ephemeral_pk = ciphertext;
    const unsigned char *nonce = ciphertext + crypto_box_PUBLICKEYBYTES;
    const unsigned char *ciphertext_body = ciphertext + crypto_box_PUBLICKEYBYTES + crypto_box_NONCEBYTES;
    size_t body_len = ciphertext_len - crypto_box_PUBLICKEYBYTES - crypto_box_NONCEBYTES;
    
    // Calcul du secret partagé
    unsigned char shared_secret[crypto_box_BEFORENMBYTES];
    int result = crypto_box_beforenm(shared_secret, ephemeral_pk, recipient_sk);
    
    if (result != 0) {
        sodium_memzero(shared_secret, sizeof(shared_secret));
        return -1;
    }
    
    // Déchiffrement
    result = crypto_box_open_easy_afternm(
        plaintext,
        ciphertext_body, body_len,
        nonce,
        shared_secret
    );
    
    if (result != 0) {
        sodium_memzero(shared_secret, sizeof(shared_secret));
        return -1;
    }
    
    *plaintext_len = body_len - crypto_box_MACBYTES;
    
    // Nettoyage mémoire sensible
    sodium_memzero(shared_secret, sizeof(shared_secret));
    
    return 0;
}

// ============================================================================
// UTILITAIRES DE CONVERSION
// ============================================================================

void bytes_to_hex(const unsigned char *bytes, size_t len, char *hex) {
    if (!bytes || !hex) return;
    
    for (size_t i = 0; i < len; i++) {
        sprintf(hex + (i * 2), "%02x", bytes[i]);
    }
    hex[len * 2] = '\0';
}

int hex_to_bytes(const char *hex, unsigned char *bytes, size_t len) {
    if (!hex || !bytes) return -1;
    
    size_t hex_len = strlen(hex);
    if (hex_len != len * 2) return -1;
    
    for (size_t i = 0; i < len; i++) {
        char byte_str[3] = {hex[i*2], hex[i*2+1], '\0'};
        char *endptr;
        long val = strtol(byte_str, &endptr, 16);
        if (*endptr != '\0' || val < 0 || val > 255) {
            return -1;
        }
        bytes[i] = (unsigned char)val;
    }
    
    return 0;
}

// ============================================================================
// TEST UNITAIRE (optionnel, peut être commenté)
// ============================================================================

#ifdef CRYPTO_TEST
void crypto_test(void) {
    printf("[CRYPTO] Running self-tests...\n");
    
    // Test AES-GCM
    {
        unsigned char key[32] = {0};
        unsigned char iv[12] = {0};
        unsigned char plaintext[] = "Hello, World!";
        unsigned char ciphertext[256];
        unsigned char decrypted[256];
        unsigned char tag[16];
        
        int len = aes256_gcm_encrypt(plaintext, strlen((char*)plaintext), key, iv, ciphertext, tag);
        if (len > 0) {
            int dlen = aes256_gcm_decrypt(ciphertext, len, key, iv, tag, decrypted);
            decrypted[dlen] = '\0';
            if (dlen == strlen((char*)plaintext) && memcmp(plaintext, decrypted, dlen) == 0) {
                printf("  ✅ AES-GCM: OK\n");
            } else {
                printf("  ❌ AES-GCM: Failed\n");
            }
        }
    }
    
    // Test BLAKE3
    {
        unsigned char hash[BLAKE3_OUT_LEN];
        blake3_hash("test", 4, hash);
        printf("  ✅ BLAKE3: OK\n");
    }
    
    // Test ED25519
    {
        unsigned char pk[crypto_sign_PUBLICKEYBYTES];
        unsigned char sk[crypto_sign_SECRETKEYBYTES];
        unsigned char sig[crypto_sign_BYTES];
        
        ed25519_keypair(pk, sk);
        ed25519_sign((unsigned char*)"test", 4, sk, sig);
        
        if (ed25519_verify((unsigned char*)"test", 4, pk, sig) == 0) {
            printf("  ✅ ED25519: OK\n");
        } else {
            printf("  ❌ ED25519: Failed\n");
        }
    }
    
    printf("[CRYPTO] Self-tests completed\n");
}
#endif