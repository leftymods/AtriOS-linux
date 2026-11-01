#ifndef _LINK_H_
#define _LINK_H_



#ifdef __cplusplus
extern "C" {
#endif

int tuya_smart_link(
		const char *ssid, const char *password, const char *pwd_raw, const char *token, int pkt_intervaltime, int round_intervaltime, int pkt_basecount, int m_round_count, int b_round_count);

void send_status_stop(void);

#ifdef __cplusplus
}
#endif

#endif //_LINK_H_
