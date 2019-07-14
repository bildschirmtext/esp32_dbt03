#include "applications.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"


int app_btx(btx_iofunc_t in, btx_iofunc_t out, btx_iofunc_t status)
{
	char addr_str[128];
	int addr_family;
	int ip_protocol;
	struct sockaddr_in dest_addr;
	dest_addr.sin_addr.s_addr = inet_addr("195.201.94.166");
	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons(20000);
	addr_family = AF_INET;
	ip_protocol = IPPROTO_IP;
	inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

	int sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
	if (sock < 0) {
		return -1;
	}

	int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
	if (err != 0) {
		return -2;
	}
	/* set recv timeout (100 ms) */
	int opt = 100;
	lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &opt, sizeof(int));

	while (status(-1)==0) {
		char tx_buffer[8]; //Terminal=>IP
		int tx_len = in(0);
		if (tx_len>=0) {
			tx_buffer[0]=tx_len;
			int err = send(sock, tx_buffer, 1, 0);
			if (err < 0) {
				break;
			}
		}

		char rx_buffer[8]; //IP=>Terminal
		int rx_len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT);
		// Error occurred during receiving
		if (rx_len < 0) {
			if (errno!=11) {
				break;
			}
		}
		// Data received
		else {
			int n;
			for (n=0; n<rx_len; n++) out(rx_buffer[n]);
		}

		vTaskDelay(20 / portTICK_PERIOD_MS);
	}
	shutdown(sock, 0);
	close(sock);
	return 0;
}




int application(btx_iofunc_t in, btx_iofunc_t out, btx_iofunc_t status)
{
	return app_btx(in, out, status);
}
