#include "buddy_lan_pair.h"
#include "buddy_lan.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "buddy_lan_crypto.h"
#include "mbedtls/platform_util.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static buddy_pair_gate_t s_gate;
static uint32_t s_next_id;
static uint64_t s_next_pair_ms;
static char s_identity[13];

static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000); }
static void hex(const uint8_t *bytes, size_t count, char *out)
{
    static const char alphabet[] = "0123456789abcdef";
    for (size_t i = 0; i < count; ++i) {
        out[2*i] = alphabet[bytes[i] >> 4];
        out[2*i+1] = alphabet[bytes[i] & 15];
    }
    out[2*count] = 0;
}
static bool unhex(const char *text, size_t count, uint8_t *out)
{
    for (size_t i = 0; i < count * 2; ++i) {
        unsigned value;
        char c = text[i];
        if (c >= '0' && c <= '9') value = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') value = (unsigned)(c - 'a' + 10);
        else return false;
        if ((i & 1) == 0) out[i/2] = (uint8_t)(value << 4);
        else out[i/2] |= (uint8_t)value;
    }
    return true;
}
static bool equal(const uint8_t *a, const uint8_t *b, size_t count)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < count; ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}
static bool hash(const uint8_t *data, size_t size, uint8_t out[32])
{
    size_t length;
    return psa_hash_compute(PSA_ALG_SHA_256, data, size, out, 32, &length) == PSA_SUCCESS
           && length == 32;
}
static bool mac(const uint8_t key[32], const uint8_t *data, size_t size, uint8_t out[32])
{
    return buddy_hmac(key, data, size, out);
}
void buddy_lan_pair_snapshot(buddy_pair_gate_t *snapshot)
{
    taskENTER_CRITICAL(&s_lock);
    *snapshot = s_gate;
    taskEXIT_CRITICAL(&s_lock);
    if (!buddy_pair_gate_live(snapshot, now_ms())) memset(snapshot, 0, sizeof(*snapshot));
}
void buddy_lan_pair_decide(uint32_t observed_id, bool approve)
{
    uint64_t now = now_ms();
    taskENTER_CRITICAL(&s_lock);
    (void)buddy_pair_gate_decide(&s_gate, observed_id, approve, now);
    taskEXIT_CRITICAL(&s_lock);
}
static bool alive(uint32_t id)
{
    char ip[16];
    buddy_lan_ip(ip);
    if (!ip[0]) return false;
    if (!id) return true;
    buddy_pair_gate_t gate;
    buddy_lan_pair_snapshot(&gate);
    return gate.id == id;
}
static bool write_all(int fd, const char *line)
{
    size_t remaining = strlen(line);
    while (remaining) {
        int size = send(fd, line, remaining, 0);
        if (size <= 0) return false;
        line += size;
        remaining -= (size_t)size;
    }
    return true;
}
static int read_pair_line(int fd, char line[256], uint64_t deadline, uint32_t id)
{
    size_t size = 0;
    while (now_ms() < deadline && alive(id) && size < 255) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        struct timeval timeout = {.tv_usec = 100000};
        int ready = select(fd+1, &set, NULL, NULL, &timeout);
        if (ready < 0) return -1;
        if (!ready) continue;
        char value;
        if (recv(fd, &value, 1, 0) != 1 || value == 0) return -1;
        if (value == '\n') { line[size] = 0; return (int)size; }
        line[size++] = value;
    }
    return -1;
}

typedef struct {
    uint8_t client_pub[65], server_pub[65], client_nonce[16], server_nonce[16];
    uint8_t transcript[185], commitment[32], digest[32], temporary[32], key[32];
    uint8_t cipher[48], nonce[12];
    char line[256];
} pairing_crypto_t;

void buddy_lan_pair_serve(int client, const char *commit, const uint8_t key[32])
{
    uint64_t now = now_ms();
    if (now < s_next_pair_ms) return;
    s_next_pair_ms = now + 60000; /* At most one SAS attempt/minute, including failures. */
    pairing_crypto_t *p = calloc(1, sizeof(*p));
    if (!p) return;
    uint32_t id = 0;
    size_t size = 0;
    mbedtls_svc_key_id_t private_key = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t gcm = MBEDTLS_SVC_KEY_ID_INIT;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    if (strlen(commit) != 74 || strncmp(commit, "FAP_PAIR1 ", 10) ||
        !unhex(commit+10, 32, p->commitment)) goto done;
    if (psa_generate_key(&attributes, &private_key) != PSA_SUCCESS ||
        psa_export_public_key(private_key, p->server_pub, sizeof(p->server_pub), &size) != PSA_SUCCESS
        || size != sizeof(p->server_pub)) goto done;
    esp_fill_random(p->server_nonce, 16);
    memcpy(p->line, "FAP_PAIR1 ", 10);
    hex(p->server_pub, 65, p->line+10);
    p->line[140] = ' ';
    hex(p->server_nonce, 16, p->line+141);
    snprintf(p->line+173, sizeof(p->line)-173, " %s\n", s_identity);
    if (!write_all(client, p->line)) goto done;
    if (read_pair_line(client, p->line, now_ms()+5000, 0) != 175 ||
        strncmp(p->line, "FAP_REVEAL1 ", 12) || p->line[142] != ' ' ||
        !unhex(p->line+12, 65, p->client_pub) || !unhex(p->line+143, 16, p->client_nonce)) goto done;
    memcpy(p->transcript, "FAP-PAIR-1:commit", 17);
    memcpy(p->transcript+17, p->client_pub, 65);
    memcpy(p->transcript+82, p->client_nonce, 16);
    if (!hash(p->transcript, 98, p->temporary) || !equal(p->temporary, p->commitment, 32) ||
        psa_raw_key_agreement(PSA_ALG_ECDH, private_key, p->client_pub, sizeof(p->client_pub),
                              p->temporary, sizeof(p->temporary), &size) != PSA_SUCCESS
        || size != sizeof(p->temporary)) goto done;
    memcpy(p->transcript, "FAP-PAIR-1:", 11);
    memcpy(p->transcript+11, p->client_pub, 65);
    memcpy(p->transcript+76, p->server_pub, 65);
    memcpy(p->transcript+141, p->client_nonce, 16);
    memcpy(p->transcript+157, p->server_nonce, 16);
    memcpy(p->transcript+173, s_identity, 12);
    if (!hash(p->transcript, 185, p->digest) || !mac(p->digest, p->temporary, 32, p->commitment) ||
        !mac(p->commitment, (const uint8_t *)"FAP-PAIR-1:key\x01", 15, p->key) ||
        !mac(p->key, (const uint8_t *)"FAP-PAIR-1:sas", 14, p->temporary)) goto done;
    uint32_t code = ((uint32_t)p->temporary[0]<<24 | (uint32_t)p->temporary[1]<<16 |
                     (uint32_t)p->temporary[2]<<8 | p->temporary[3]) % 1000000;
    if (++s_next_id == 0) ++s_next_id;
    id = s_next_id;
    uint64_t deadline = now_ms() + 30000;
    taskENTER_CRITICAL(&s_lock);
    s_gate = (buddy_pair_gate_t){.id=id, .code=code, .deadline_ms=deadline};
    taskEXIT_CRITICAL(&s_lock);
    if (!write_all(client, "FAP_READY1\n")) goto done;
    if (read_pair_line(client, p->line, deadline, id) != 77 ||
        strncmp(p->line, "FAP_CONFIRM1 ", 13) || !unhex(p->line+13, 32, p->temporary) ||
        !mac(p->key, (const uint8_t *)"FAP-PAIR-1:confirm", 18, p->commitment) ||
        !equal(p->temporary, p->commitment, 32)) goto done;
    while (alive(id)) {
        buddy_pair_gate_t gate;
        buddy_lan_pair_snapshot(&gate);
        if (gate.approved) {
            esp_fill_random(p->nonce, 12);
            if (buddy_gcm_key(p->key, &gcm) != PSA_SUCCESS ||
                psa_aead_encrypt(gcm, PSA_ALG_GCM, p->nonce, sizeof(p->nonce), p->digest,
                    sizeof(p->digest), key, 32, p->cipher, sizeof(p->cipher), &size) != PSA_SUCCESS
                || size != sizeof(p->cipher)) goto done;
            memcpy(p->line, "FAP_KEY1 ", 9);
            hex(p->nonce, 12, p->line+9);
            p->line[33] = ' ';
            hex(p->cipher, 48, p->line+34);
            p->line[130] = '\n'; p->line[131] = 0;
            if (alive(id)) (void)write_all(client, p->line);
            break;
        }
        char value;
        if (recv(client, &value, 1, MSG_PEEK | MSG_DONTWAIT) == 0) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
done:
    taskENTER_CRITICAL(&s_lock);
    if (s_gate.id == id) memset(&s_gate, 0, sizeof(s_gate));
    taskEXIT_CRITICAL(&s_lock);
    psa_destroy_key(gcm);
    psa_destroy_key(private_key);
    mbedtls_platform_zeroize(p, sizeof(*p));
    free(p);
}

static void discovery_task(void *context)
{
    (void)context;
    for (;;) {
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        struct sockaddr_in address = {.sin_family=AF_INET, .sin_port=htons(BUDDY_DISCOVERY_PORT),
                                      .sin_addr.s_addr=htonl(INADDR_ANY)};
        struct timeval timeout = {.tv_sec=2};
        if (fd >= 0) setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (fd >= 0 && bind(fd, (struct sockaddr *)&address, sizeof(address)) == 0) {
            uint64_t last_reply = 0;
            for (;;) {
                char packet[64], response[224], ip[16];
                struct sockaddr_in peer;
                socklen_t length = sizeof(peer);
                int size = recvfrom(fd, packet, sizeof(packet), 0, (struct sockaddr *)&peer, &length);
                buddy_lan_ip(ip);
                if (size != 30 || !ip[0] || strncmp(packet, "FAP_DISCOVER1 ", 14) || now_ms()-last_reply < 250) continue;
                uint8_t nonce[8];
                if (!unhex(packet+14, 8, nonce)) continue;
                packet[30] = 0;
                last_reply = now_ms();
                int count = snprintf(response, sizeof(response),
                    "{\"app\":\"passport\",\"v\":1,\"nonce\":\"%s\",\"id\":\"%s\",\"port\":8765,\"pair\":%s}",
                    packet+14, s_identity, buddy_lan_connected() ? "false" : "true");
                if (count > 0 && count < (int)sizeof(response))
                    sendto(fd, response, count, 0, (struct sockaddr *)&peer, length);
            }
        }
        if (fd >= 0) close(fd);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
esp_err_t buddy_lan_pair_start(void)
{
    uint8_t mac_address[6];
    esp_err_t error = esp_read_mac(mac_address, ESP_MAC_WIFI_STA);
    if (error != ESP_OK) return error;
    hex(mac_address, 6, s_identity);
    return xTaskCreate(discovery_task, "lan_discovery", 3072, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
