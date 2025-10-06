#include "aes_demo.h"
#include <stdio.h>
#include <string.h>

// ================= S-BOX =================
static const uint8_t sbox[256] = {
  // 16x16 S-Box table
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

// Rcon
static const uint8_t Rcon[11] = {
  0x8d,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

// ================= AES CORE FUNCTIONS =================
static void AddRoundKey(uint8_t *state, const uint8_t *roundKey) {
    for(int i=0;i<16;i++) state[i] ^= roundKey[i];
}

static void SubBytes(uint8_t *state) {
    for(int i=0;i<16;i++) state[i] = sbox[state[i]];
}

static void ShiftRows(uint8_t *state) {
    uint8_t tmp[16];
    tmp[0]=state[0]; tmp[1]=state[5]; tmp[2]=state[10]; tmp[3]=state[15];
    tmp[4]=state[4]; tmp[5]=state[9]; tmp[6]=state[14]; tmp[7]=state[3];
    tmp[8]=state[8]; tmp[9]=state[13]; tmp[10]=state[2]; tmp[11]=state[7];
    tmp[12]=state[12]; tmp[13]=state[1]; tmp[14]=state[6]; tmp[15]=state[11];
    memcpy(state,tmp,16);
}

static uint8_t xtime(uint8_t x){ return (x<<1) ^ (((x>>7)&1)*0x1b); }

static void MixColumns(uint8_t *state) {
    uint8_t tmp[16];
    for(int i=0;i<4;i++){
        int j=i*4;
        tmp[j]= (uint8_t)(xtime(state[j]) ^ xtime(state[j+1]) ^ state[j+1] ^ state[j+2] ^ state[j+3]);
        tmp[j+1]= (uint8_t)(state[j] ^ xtime(state[j+1]) ^ xtime(state[j+2]) ^ state[j+2] ^ state[j+3]);
        tmp[j+2]= (uint8_t)(state[j] ^ state[j+1] ^ xtime(state[j+2]) ^ xtime(state[j+3]) ^ state[j+3]);
        tmp[j+3]= (uint8_t)(xtime(state[j]) ^ state[j] ^ state[j+1] ^ state[j+2] ^ xtime(state[j+3]));
    }
    memcpy(state,tmp,16);
}

static void KeyExpansion(uint8_t *roundKey, const uint8_t *key) {
    int i=0;
    memcpy(roundKey,key,16);
    i=16;
    int rconIter=1;
    uint8_t temp[4];

    while(i<176){
        for(int j=0;j<4;j++) temp[j]=roundKey[i-4+j];
        if(i%16==0){
            // rotate
            uint8_t t=temp[0];
            temp[0]=temp[1]; temp[1]=temp[2]; temp[2]=temp[3]; temp[3]=t;
            // subbytes
            for(int j=0;j<4;j++) temp[j]=sbox[temp[j]];
            // rcon
            temp[0]^=Rcon[rconIter++];
        }
        for(int j=0;j<4;j++){
            roundKey[i]=roundKey[i-16]^temp[j];
            i++;
        }
    }
}

void AES_init_ctx(AES_ctx *ctx, const uint8_t *key){
    KeyExpansion(ctx->round_key,key);
}

void AES_encrypt(AES_ctx *ctx, uint8_t *buf){
    uint8_t state[16];
    memcpy(state,buf,16);

    AddRoundKey(state,ctx->round_key);

    for(int round=1;round<10;round++){
        SubBytes(state);
        ShiftRows(state);
        MixColumns(state);
        AddRoundKey(state,ctx->round_key+round*16);
    }

    SubBytes(state);
    ShiftRows(state);
    AddRoundKey(state,ctx->round_key+160);

    memcpy(buf,state,16);
}

// ================= SECURE ZERO =================
static void secure_zero(void *v, size_t n) {
    volatile uint8_t *p = (volatile uint8_t*)v;
    while (n--) *p++ = 0;
}

// ================= TOKEN FUNCTIONS =================
void token_init(TokenContext *token,const char *username,const uint8_t *key){
    strncpy(token->user.username,username,MAX_USERNAME_LEN);
    token->user.authenticated=1; // demo: coi như login thành công

    // copy key và expand
    memcpy(token->aes_key, key, AES_KEY_SIZE);
    AES_init_ctx(&token->aes, key);
}

void token_encrypt_and_log(TokenContext *token,const uint8_t *input){
    memcpy(token->plaintext, input, AES_BLOCK_SIZE);

    // copy plaintext sang buffer làm việc
    memcpy(token->ciphertext, token->plaintext, AES_BLOCK_SIZE);
    AES_encrypt(&token->aes, token->ciphertext);

    printf("User: %s | Ciphertext: ", token->user.username);
    for(int i=0;i<AES_BLOCK_SIZE;i++) printf("%02x ", token->ciphertext[i]);
    printf("\n");
}

void token_wipe(TokenContext *token) {
    secure_zero(token->aes_key, AES_KEY_SIZE);
    secure_zero(token->aes.round_key, sizeof(token->aes.round_key));
    secure_zero(token->plaintext, AES_BLOCK_SIZE);
    // ciphertext có thể giữ lại để truyền đi
}
/* ========================= [ADDED] Helper functions ========================= */

#define CMD_HEADER_SIZE 3
#define CRC_SIZE 2

/* CRC-16-CCITT (0x1021) - basic implementation */
uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

/* PKCS#7 padding - pad 'buf' in-place, buffer must have room for pad up to block_size.
   Returns new length after padding. */
size_t pkcs7_pad(uint8_t *buf, size_t len, size_t block_size)
{
    size_t pad = block_size - (len % block_size);
    if (pad == 0) pad = block_size;
    for (size_t i = 0; i < pad; ++i) buf[len + i] = (uint8_t)pad;
    return len + pad;
}

/* AES-CBC encrypt N bytes (length must be multiple of 16). Uses AES_encrypt() core for each block.
   - ctx: pointer to AES_ctx already initialised
   - input: plaintext (in)
   - output: ciphertext (out)
   - length: bytes (multiple of 16)
   - iv: 16-byte IV (will not be modified)
*/
void AES_encrypt_CBC(AES_ctx *ctx,
                            const uint8_t *input, uint8_t *output,
                            size_t length, const uint8_t iv[AES_BLOCK_SIZE])
{
    uint8_t chain[AES_BLOCK_SIZE];
    uint8_t block[AES_BLOCK_SIZE];

    memcpy(chain, iv, AES_BLOCK_SIZE);

    for (size_t offset = 0; offset < length; offset += AES_BLOCK_SIZE) {
        /* XOR plaintext block with chain (IV or previous ciphertext) */
        for (int i = 0; i < AES_BLOCK_SIZE; ++i)
            block[i] = input[offset + i] ^ chain[i];

        /* encrypt block in-place (AES_encrypt expects a modifiable buffer) */
        AES_encrypt(ctx, block);

        /* write ciphertext */
        memcpy(output + offset, block, AES_BLOCK_SIZE);

        /* update chain = ciphertext */
        memcpy(chain, block, AES_BLOCK_SIZE);
    }
}

/* Update token key (overwrite token->aes_key and re-init AES context) */
void token_set_key(TokenContext *t, const uint8_t *newkey, size_t keylen)
{
    /* keylen should be AES_KEY_SIZE */
    memcpy(t->aes_key, newkey, AES_KEY_SIZE);            /* [ADDED] store new key */
    AES_init_ctx(&t->aes, t->aes_key);                  /* [ADDED] reexpand round keys */
}

/* Wipe token secure data */
void token_reset(TokenContext *t)
{
    token_wipe(t); /* existing function wipes key schedule and plaintext */
    /* additionally clear username/auth */
    memset(t->user.username, 0, MAX_USERNAME_LEN);
    t->user.authenticated = 0;
}

