#include "apkm.h"
#include <sodium.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <argon2.h>
#include <blake3.h>

// Chiffrement AES-256-GCM avec authentification
int aes256_gcm_encrypt(const unsigned char* plaintext, size_t plaintext_len,
                       const unsigned char* key, const unsigned char* iv,
                       unsigned char* ciphertext, unsigned char* tag) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL);
    EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv);
    
    int len;
    EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len);
    
    EVP_EncryptFinal_ex(ctx, ciphertext + len, &len);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    
    EVP_CIPHER_CTX_free(ctx);
    return 0;
}

// Signature Ed25519 avec sodium
int ed25519_sign(const unsigned char* message, size_t msg_len,
                 const unsigned char* sk, unsigned char* signature) {
    return crypto_sign_detached(signature, NULL, message, msg_len, sk);
}

// Hachage BLAKE3 (ultra-rapide)
void blake3_hash(const void* data, size_t len, unsigned char* hash) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, len);
    blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);
}

// Dérivation de clé Argon2id (résistant aux attaques GPU)
int argon2id_derive_key(const char* password, const unsigned char* salt,
                        unsigned char* key, size_t key_len) {
    return argon2id_hash_raw(2, 1 << 16, 1, password, strlen(password),
                             salt, crypto_pwhash_SALTBYTES, key, key_len);
}

// Chiffrement hybride (courbes elliptiques + AES)
int hybrid_encrypt(const unsigned char* plaintext, size_t plaintext_len,
                   const unsigned char* recipient_pubkey,
                   unsigned char* ciphertext, size_t* ciphertext_len) {
    // Génération d'une paire de clés éphémère
    unsigned char ephemeral_sk[crypto_box_SECRETKEYBYTES];
    unsigned char ephemeral_pk[crypto_box_PUBLICKEYBYTES];
    crypto_box_keypair(ephemeral_pk, ephemeral_sk);
    
    // Calcul du secret partagé
    unsigned char shared_secret[crypto_box_BEFORENMBYTES];
    crypto_box_beforenm(shared_secret, recipient_pubkey, ephemeral_sk);
    
    // Chiffrement avec le secret
    unsigned char nonce[crypto_box_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));
    
    crypto_box_easy_afternm(ciphertext + crypto_box_PUBLICKEYBYTES + crypto_box_NONCEBYTES,
                            plaintext, plaintext_len, nonce, shared_secret);
    
    // Préfixe avec la clé publique éphémère et le nonce
    memcpy(ciphertext, ephemeral_pk, crypto_box_PUBLICKEYBYTES);
    memcpy(ciphertext + crypto_box_PUBLICKEYBYTES, nonce, crypto_box_NONCEBYTES);
    
    *ciphertext_len = crypto_box_PUBLICKEYBYTES + crypto_box_NONCEBYTES +
                      plaintext_len + crypto_box_MACBYTES;
    
    sodium_memzero(shared_secret, sizeof(shared_secret));
    sodium_memzero(ephemeral_sk, sizeof(ephemeral_sk));
    
    return 0;
}
