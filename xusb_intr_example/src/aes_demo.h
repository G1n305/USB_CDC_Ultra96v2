#ifndef AES_DEMO_H
#define AES_DEMO_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>   /* [ADDED] cho size_t, giả sử cần */

#define AES_BLOCK_SIZE 16
#define AES_KEY_SIZE   16
#define AES_ROUNDS     10
#define MAX_USERNAME_LEN 32

// ================= USER AUTH STRUCT =================
typedef struct {
    char username[MAX_USERNAME_LEN];
    uint8_t public_key[32];     // Placeholder cho Ed25519
    uint8_t signature[64];      // Placeholder cho chữ ký số
    uint8_t authenticated;      // 0 = no, 1 = yes
} UserAuthContext;

// ================= AES CONTEXT =================
typedef struct {
    uint8_t round_key[176];     // expanded key schedule
} AES_ctx;

// ================= TOKEN CONTEXT =================
typedef struct {
    UserAuthContext user;
    AES_ctx aes;

    // thêm buffer để lưu dữ liệu nhạy cảm
    uint8_t aes_key[AES_KEY_SIZE];
    uint8_t plaintext[AES_BLOCK_SIZE];
    uint8_t ciphertext[AES_BLOCK_SIZE];
} TokenContext;

// ================= API =================
void AES_init_ctx(AES_ctx *ctx, const uint8_t *key);
void AES_encrypt(AES_ctx *ctx, uint8_t *buf);

void token_init(TokenContext *token, const char *username, const uint8_t *key);
void token_encrypt_and_log(TokenContext *token, const uint8_t *input);
void token_wipe(TokenContext *token);

uint16_t crc16_ccitt(const uint8_t *data, size_t len);
size_t pkcs7_pad(uint8_t *buf, size_t len, size_t block_size);
void AES_encrypt_CBC(AES_ctx *ctx,
                     const uint8_t *input, uint8_t *output,
                     size_t length, const uint8_t iv[AES_BLOCK_SIZE]);
void token_set_key(TokenContext *t, const uint8_t *newkey, size_t keylen);
void token_reset(TokenContext *t);

#endif
