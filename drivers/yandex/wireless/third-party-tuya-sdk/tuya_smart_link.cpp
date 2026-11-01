#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;


#ifdef __ANDROID__

#include <android/log.h>

#endif

#include "tuya_smart_link.h"

static pthread_mutex_t smartLock = PTHREAD_MUTEX_INITIALIZER;


uint8_t crc8_table[256] =
        {
                0x00, 0x5E, 0xBC, 0xE2, 0x61, 0x3F, 0xDD, 0x83, 0xC2, 0x9C, 0x7E, 0x20, 0xA3, 0xFD,
                0x1F, 0x41,
                0x9D, 0xC3, 0x21, 0x7F, 0xFC, 0xA2, 0x40, 0x1E, 0x5F, 0x01, 0xE3, 0xBD, 0x3E, 0x60,
                0x82, 0xDC,
                0x23, 0x7D, 0x9F, 0xC1, 0x42, 0x1C, 0xFE, 0xA0, 0xE1, 0xBF, 0x5D, 0x03, 0x80, 0xDE,
                0x3C, 0x62,
                0xBE, 0xE0, 0x02, 0x5C, 0xDF, 0x81, 0x63, 0x3D, 0x7C, 0x22, 0xC0, 0x9E, 0x1D, 0x43,
                0xA1, 0xFF,
                0x46, 0x18, 0xFA, 0xA4, 0x27, 0x79, 0x9B, 0xC5, 0x84, 0xDA, 0x38, 0x66, 0xE5, 0xBB,
                0x59, 0x07,
                0xDB, 0x85, 0x67, 0x39, 0xBA, 0xE4, 0x06, 0x58, 0x19, 0x47, 0xA5, 0xFB, 0x78, 0x26,
                0xC4, 0x9A,
                0x65, 0x3B, 0xD9, 0x87, 0x04, 0x5A, 0xB8, 0xE6, 0xA7, 0xF9, 0x1B, 0x45, 0xC6, 0x98,
                0x7A, 0x24,
                0xF8, 0xA6, 0x44, 0x1A, 0x99, 0xC7, 0x25, 0x7B, 0x3A, 0x64, 0x86, 0xD8, 0x5B, 0x05,
                0xE7, 0xB9,
                0x8C, 0xD2, 0x30, 0x6E, 0xED, 0xB3, 0x51, 0x0F, 0x4E, 0x10, 0xF2, 0xAC, 0x2F, 0x71,
                0x93, 0xCD,
                0x11, 0x4F, 0xAD, 0xF3, 0x70, 0x2E, 0xCC, 0x92, 0xD3, 0x8D, 0x6F, 0x31, 0xB2, 0xEC,
                0x0E, 0x50,
                0xAF, 0xF1, 0x13, 0x4D, 0xCE, 0x90, 0x72, 0x2C, 0x6D, 0x33, 0xD1, 0x8F, 0x0C, 0x52,
                0xB0, 0xEE,
                0x32, 0x6C, 0x8E, 0xD0, 0x53, 0x0D, 0xEF, 0xB1, 0xF0, 0xAE, 0x4C, 0x12, 0x91, 0xCF,
                0x2D, 0x73,
                0xCA, 0x94, 0x76, 0x28, 0xAB, 0xF5, 0x17, 0x49, 0x08, 0x56, 0xB4, 0xEA, 0x69, 0x37,
                0xD5, 0x8B,
                0x57, 0x09, 0xEB, 0xB5, 0x36, 0x68, 0x8A, 0xD4, 0x95, 0xCB, 0x29, 0x77, 0xF4, 0xAA,
                0x48, 0x16,
                0xE9, 0xB7, 0x55, 0x0B, 0x88, 0xD6, 0x34, 0x6A, 0x2B, 0x75, 0x97, 0xC9, 0x4A, 0x14,
                0xF6, 0xA8,
                0x74, 0x2A, 0xC8, 0x96, 0x15, 0x4B, 0xA9, 0xF7, 0xB6, 0xE8, 0x0A, 0x54, 0xD7, 0x89,
                0x6B, 0x35,
        };

uint8_t crc8(uint8_t *pdata, uint8_t size) {
    uint8_t value = 0;

    while (size) {
        value ^= *pdata++;
        value = crc8_table[value];

        size--;
    }

    return value;
}

int crc32(const char *data, int len) {
    int crc = 0xFFFFFFFF;// initial contents of LFBSR
    int poly = 0xEDB88320;// reverse polynomial

    for (int i = 0; i < len; i++) {
        int temp = (crc ^ data[i]) & 0xff;

        for (int k = 0; k < 8; k++) {
            if ((temp & 1) == 1) {
                temp = ((uint32_t) temp >> 1) ^ poly;
            } else {
                temp = ((uint32_t) temp >> 1);
            }
        }
        crc = ((uint32_t) crc >> 8) ^ temp;
    }

    crc = crc ^ 0xffffffff;

    return crc;
}

typedef struct BroadcastLinkInfo {
    uint16_t preamble[4];
    uint16_t *sequence;
    uint16_t sequence_len;
} BroadcastLinkInfo;

BroadcastLinkInfo *broadcast_link_info = NULL;


void broadcast_head_encode() {
    broadcast_link_info->preamble[0] = 1;
    broadcast_link_info->preamble[1] = 3;
    broadcast_link_info->preamble[2] = 6;
    broadcast_link_info->preamble[3] = 10;
}

void broadcast_body_encode(const char *ssid, const char *password, const char *token) {
    uint8_t ssid_len = strlen(ssid);
    uint8_t pwd_len = strlen(password);
    uint8_t token_len = strlen(token);
    uint8_t total_len = ssid_len + pwd_len + token_len + 2;
    uint8_t total_len_crc = crc8(&total_len, 1);

    uint8_t norm_len = total_len + (4 - total_len % 4) % 4;
    uint8_t *seq_data = (uint8_t *) malloc(norm_len * sizeof(uint8_t));
    memset(seq_data, 0, norm_len * sizeof(uint8_t));
    seq_data[0] = pwd_len;
    memcpy(seq_data + 1, password, pwd_len);
    seq_data[pwd_len + 1] = token_len;
    memcpy(seq_data + pwd_len + 2, token, token_len);
    memcpy(seq_data + pwd_len + token_len + 2, ssid, ssid_len);

    uint8_t seq_index = 6 + 16, block_index = 0;
    uint8_t *seq_block = (uint8_t *) malloc(5 * sizeof(uint8_t));
    broadcast_link_info->sequence_len = norm_len / 4 * 6 + 6 + 16;
    broadcast_link_info->sequence = (uint16_t *) malloc(
            broadcast_link_info->sequence_len * sizeof(uint16_t));
    for (int i = 0; i < norm_len; i += 4) {
        seq_block[0] = block_index;
        memcpy(seq_block + 1, seq_data + i, 4 * sizeof(uint8_t));

        broadcast_link_info->sequence[seq_index++] = crc8(seq_block, 5) | 0x80;
        broadcast_link_info->sequence[seq_index++] = block_index | 0x80;
        block_index++;

        for (int k = 0; k < 4; k++) {
            broadcast_link_info->sequence[seq_index++] = seq_block[k + 1] | 0x100;
        }
    }

    //head_code * 4
    for (int j = 0; j < 4; j++) {
        broadcast_link_info->sequence[0 + j * 4] = 1;
        broadcast_link_info->sequence[1 + j * 4] = 3;
        broadcast_link_info->sequence[2 + j * 4] = 6;
        broadcast_link_info->sequence[3 + j * 4] = 10;
    }

    // magic_code
    uint8_t crc_low = total_len_crc & 0x0F;
    uint8_t crc_high = (total_len_crc & 0xF0) >> 4;
    broadcast_link_info->sequence[0 + 16] = 0x10 | (total_len >> 4 & 0xF);
    broadcast_link_info->sequence[1 + 16] = 0x20 | (total_len & 0xF);
    broadcast_link_info->sequence[2 + 16] = 0x30 | (crc_high & 0xF);
    broadcast_link_info->sequence[3 + 16] = 0x40 | (crc_low & 0xF);

    uint8_t data_crc = crc8(seq_data, total_len);
    uint8_t data_crc_low = data_crc & 0x0F;
    uint8_t data_crc_high = (data_crc & 0xF0) >> 4;
    broadcast_link_info->sequence[4 + 16] = 0x50 | (data_crc_high & 0xF);
    broadcast_link_info->sequence[5 + 16] = 0x60 | (data_crc_low & 0xF);

    free(seq_data);
    seq_data = NULL;
    free(seq_block);
    seq_block = NULL;
}

void broadcast_encode(const char *ssid, const char *password, const char *token) {
    broadcast_head_encode();
    broadcast_body_encode(ssid, password, token);
}

void broadcast_encode_release() {
    if (broadcast_link_info != NULL) {
        if (broadcast_link_info->sequence != NULL) {
            free(broadcast_link_info->sequence);
            broadcast_link_info->sequence = NULL;
            broadcast_link_info->sequence_len = 0;
        }
        free(broadcast_link_info);
        broadcast_link_info = NULL;
    }
}

typedef struct Packet {
    int i;
    int j;
    int k;
} Packet;

typedef struct MulticastLinkInfo {
    Packet preamble[3];
    std::vector<Packet> vec_body;
} MulticastLinkInfo;

MulticastLinkInfo *multicast_link_info = NULL;

void multicast_head_encode() {
    uint16_t preamble[6] = {0x54, 0x59, 0x53, 0x54, 0x30, 0x31};
    for (int substate = 0; substate < 3; substate++) {
        multicast_link_info->preamble[substate].k = preamble[2 * substate];
        multicast_link_info->preamble[substate].j = preamble[2 * substate + 1];
        multicast_link_info->preamble[substate].i = substate | 0x78;
    }
}

void xmitState(uint8_t *data, int len, int crc, int mask, int substate, int flag) {
    if (substate == 0) {
        int u = mask;
        Packet pkt;
        pkt.i = u;
        pkt.j = len;
        pkt.k = len;
        multicast_link_info->vec_body.push_back(pkt);
    } else if (substate == 1 || substate == 2) {
        Packet pkt;
        pkt.k = (int) (crc >> ((2 * (substate - 1) + 0) * 8)) & 0xff;
        pkt.j = (int) (crc >> ((2 * (substate - 1) + 1) * 8)) & 0xff;
        pkt.i = substate | mask;
        multicast_link_info->vec_body.push_back(pkt);
    } else {
        Packet pkt;
        pkt.i = substate | mask;
        pkt.k = (0xff & data[(2 * (substate - 3))]);
        if (flag == 2) {
            pkt.j = (0xff & data[(2 * (substate - 3)) + 1]);
        } else {
            pkt.j = 0;
        }
        multicast_link_info->vec_body.push_back(pkt);
    }
}

void xmitState(uint8_t *data, int len, int crc, int mask) {
    int substate = 0;
    while (true) {
        xmitState(data, len, crc, mask, substate++, 2);
        if (len & 1) {
            if (substate * 2 == len + 5) {
                xmitState(data, len, crc, mask, substate, 1);
                break;
            }
        } else {
            if ((substate - 1) * 2 == (len + 4)) {
                break;
            }
        }
    }
}

void multicast_body_encode(const char *ssid, const char *password, const char *pwd_raw,
                           const char *token) {
    int ssid_len = (int) strlen(ssid);
    int pwd_len = (int) strlen(pwd_raw);
    int token_len = (int) strlen(token);

    int ssid_crc = crc32(ssid, ssid_len);
    int token_crc = crc32(token, token_len);
    int pwd_crc = crc32(pwd_raw, pwd_len);

    uint8_t norm_len = (pwd_len > 0) ? pwd_len + (16 - pwd_len % 16) % 16 : 16;


    uint16_t preamble[6] = {0x54, 0x59, 0x53, 0x54, 0x30, 0x31};
    multicast_link_info->vec_body.clear();
    for (int substate = 0; substate < 3; substate++) {
        Packet pkt;
        pkt.k = preamble[2 * substate];
        pkt.j = preamble[2 * substate + 1];
        pkt.i = substate | 0x78;
        multicast_link_info->vec_body.push_back(pkt);
    }

    xmitState((uint8_t *) ssid, ssid_len, ssid_crc, 0x40);
    xmitState((uint8_t *) password, (pwd_len != 0) ? norm_len : 0, pwd_crc, 0x00);
    xmitState((uint8_t *) token, token_len, token_crc, 0x20);

}

void
multicast_encode(const char *ssid, const char *password, const char *pwd_raw, const char *token) {
    multicast_head_encode();
    multicast_body_encode(ssid, password, pwd_raw, token);
}

void multicast_encode_release() {
    if (multicast_link_info != NULL) {
        std::vector<Packet>().swap(multicast_link_info->vec_body);
        free(multicast_link_info);
        multicast_link_info = NULL;
    }
}


void encode_data(const char *ssid, const char *password, const char *pwd_raw, const char *token) {
    broadcast_link_info = (BroadcastLinkInfo *) malloc(sizeof(BroadcastLinkInfo));
    multicast_link_info = (MulticastLinkInfo *) malloc(sizeof(MulticastLinkInfo));
    memset(broadcast_link_info, 0, sizeof(BroadcastLinkInfo));
    memset(multicast_link_info, 0, sizeof(MulticastLinkInfo));

    broadcast_encode(ssid, pwd_raw, token);
    multicast_encode(ssid, password, pwd_raw, token);
}

volatile int ty_quit_flag = 1;
volatile bool ty_release_flag = true;

void send_status_stop() {
    pthread_mutex_lock(&smartLock);
    ty_quit_flag = 1;
    pthread_mutex_unlock(&smartLock);
}

void pkt_delay(uint sec, uint msec) {
    timeval delay;
    delay.tv_sec = sec;// 秒
    delay.tv_usec = msec * 1000;// 毫秒
    select(0, NULL, NULL, NULL, &delay);
}

typedef struct SocketInfo {
    int sock_fd;
    int broadcast_port;
    int multicast_port;
    char broadcast_addr[20];
    char multicast_addr[20];

    int pkt_intervaltime;
    int round_intervaltime;
    int pkt_basecount;
    int m_round_count;
    int b_round_count;
} SocketInfo;


void *send_data_thread(void *arg) {
    SocketInfo *socket_info = (SocketInfo *) arg;

    sockaddr_in multicast_addr;
    memset(&multicast_addr, 0, sizeof(sockaddr_in));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_port = htons(socket_info->multicast_port);
    multicast_addr.sin_addr.s_addr = inet_addr(socket_info->multicast_addr);

    int broadcast_enable = 1;
    
#ifdef __APPLE__
    if (setsockopt(socket_info->sock_fd, SOL_SOCKET, SO_NOSIGPIPE, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        return NULL;
    }
#endif

    if (setsockopt(socket_info->sock_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        return NULL;
    }

    sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(sockaddr_in));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(socket_info->broadcast_port);
    broadcast_addr.sin_addr.s_addr = inet_addr(socket_info->broadcast_addr);

    int send_count = 0, denom = 1;
    char buffer[512 * 2] = {0};
    int addr_len = sizeof(sockaddr_in);
    for (int d = 0; d < 2; d++) {
        denom = 3 * socket_info->m_round_count + 4 * socket_info->b_round_count;
        denom = (denom != 0) ? denom : 7;
        send_count = socket_info->pkt_basecount / denom + 1;// 发满1000包
        for (int n = 0; n < send_count; n++) {
            for (int round = 0; round < socket_info->m_round_count; round++) {
                for (int m = 0; m < 3; m++) {
                    sprintf(socket_info->multicast_addr, "226.%d.%d.%d",
                            multicast_link_info->preamble[m].i, multicast_link_info->preamble[m].j,
                            multicast_link_info->preamble[m].k);
                    multicast_addr.sin_addr.s_addr = inet_addr(socket_info->multicast_addr);
                    sendto(socket_info->sock_fd, buffer, 1, 0, (sockaddr *) &multicast_addr,
                           addr_len);
                    pkt_delay(0, socket_info->pkt_intervaltime);
                }
            }
            for (int round = 0; round < socket_info->b_round_count; round++) {
                for (int m = 0; m < 4; m++) {
                    sendto(socket_info->sock_fd, buffer, broadcast_link_info->preamble[m], 0,
                           (sockaddr *) &broadcast_addr, addr_len);
                    pkt_delay(0, socket_info->pkt_intervaltime);
                }
            }
            {
                pthread_mutex_lock(&smartLock);
                if (ty_quit_flag == 1) {
                    pthread_mutex_unlock(&smartLock);
                    return NULL;
                }
                pthread_mutex_unlock(&smartLock);
            }


        }

        denom = (int) multicast_link_info->vec_body.size() * socket_info->m_round_count +
                broadcast_link_info->sequence_len * socket_info->b_round_count;
        denom = (denom != 0) ? denom : ((int) multicast_link_info->vec_body.size() +
                                        broadcast_link_info->sequence_len);
        send_count = socket_info->pkt_basecount / denom + 1;
        for (int r = 0; r < 5; r++) {
            for (int n = 0; n < send_count; n++) {
                for (int round = 0; round < socket_info->m_round_count; round++) {
                    for (int m = 0; m < multicast_link_info->vec_body.size(); m++) {
                        sprintf(socket_info->multicast_addr, "226.%d.%d.%d",
                                multicast_link_info->vec_body[m].i,
                                multicast_link_info->vec_body[m].j,
                                multicast_link_info->vec_body[m].k);
                        multicast_addr.sin_addr.s_addr = inet_addr(socket_info->multicast_addr);
                        sendto(socket_info->sock_fd, buffer, 1, 0, (sockaddr *) &multicast_addr,
                               addr_len);
                        pkt_delay(0, socket_info->pkt_intervaltime);

                        {
                            pthread_mutex_lock(&smartLock);
                            if (ty_quit_flag == 1) {
                                pthread_mutex_unlock(&smartLock);
                                return NULL;
                            }
                            pthread_mutex_unlock(&smartLock);
                        }
                    }
                }

                for (int round = 0; round < socket_info->b_round_count; round++) {
                    for (int m = 0; m < broadcast_link_info->sequence_len; m++) {
                        sendto(socket_info->sock_fd, buffer, broadcast_link_info->sequence[m], 0,
                               (sockaddr *) &broadcast_addr, addr_len);
                        pkt_delay(0, socket_info->pkt_intervaltime);

                        {
                            pthread_mutex_lock(&smartLock);
                            if (ty_quit_flag == 1) {
                                pthread_mutex_unlock(&smartLock);
                                return NULL;
                            }
                            pthread_mutex_unlock(&smartLock);
                        }
                    }
                }

                {
                    pthread_mutex_lock(&smartLock);
                    if (ty_quit_flag == 1) {
                        pthread_mutex_unlock(&smartLock);
                        return NULL;
                    }
                    pthread_mutex_unlock(&smartLock);
                }
            }// for

            pkt_delay(socket_info->round_intervaltime, 0);

            {
                pthread_mutex_lock(&smartLock);
                if (ty_quit_flag == 1) {
                    pthread_mutex_unlock(&smartLock);
                    return NULL;
                }
                pthread_mutex_unlock(&smartLock);
            }
        }
    }

    return NULL;
}

int send_data(int pkt_intervaltime, int round_intervaltime, int pkt_basecount, int m_round_count,
              int b_round_count) {
    SocketInfo socket_info;
    socket_info.broadcast_port = 30011;
    socket_info.multicast_port = 30012;
    sprintf(socket_info.broadcast_addr, "%s", "255.255.255.255");
    sprintf(socket_info.multicast_addr, "%s", "255.255.255.255");
    socket_info.pkt_intervaltime = pkt_intervaltime;
    socket_info.round_intervaltime = round_intervaltime;
    socket_info.pkt_basecount = pkt_basecount;
    socket_info.m_round_count = m_round_count;
    socket_info.b_round_count = b_round_count;

    if ((socket_info.sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        return 0;
    } else {
        send_data_thread(&socket_info);
        close(socket_info.sock_fd);
        return 1;
    }
}
//*-------------------------------------------------------------------------------------------------------/

void release() {
    broadcast_encode_release();
    multicast_encode_release();

    pthread_mutex_lock(&smartLock);
    ty_quit_flag = 1;
    ty_release_flag = true;
    pthread_mutex_unlock(&smartLock);
}
//*-------------------------------------------------------------------------------------------------------/


int tuya_smart_link(
        const char *ssid, const char *password, const char *pwd_raw, const char *token,
        int pkt_intervaltime, int round_intervaltime, int pkt_basecount, int m_round_count,
        int b_round_count) {
    {
        pthread_mutex_lock(&smartLock);
        if (ty_quit_flag == 0 || !ty_release_flag) {
            pthread_mutex_unlock(&smartLock);
            return -1;
        }
        ty_quit_flag = 0;
        ty_release_flag = false;
        pthread_mutex_unlock(&smartLock);
    }
    encode_data(ssid, password, pwd_raw, token);
    int ret = send_data(pkt_intervaltime, round_intervaltime, pkt_basecount, m_round_count,
                        b_round_count);
    release();

    return ret;
}



