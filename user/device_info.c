// device_info.c
// Copyright 2017 Lukas Friedrichsen
// License: Apache License Version 2.0
//
// 2017-04-27
//
// Description: This class provides meta-data to a node, therewith affixing an unique identity to it. Other members of the
// same network can request this information via UDP.
//
// This class is based on https://github.com/espressif/ESP8266_MESH_DEMO/tree/master/mesh_performance/scenario/devicefind.c

#include "mem.h"
#include "osapi.h"
#include "ets_sys.h"
#include "os_type.h"
#include "espconn.h"
#include "user_interface.h"
#include "device_info.h"
#include "user_config.h"

const static char *request_string = REQUEST_STRING; // Local copy of REQUEST_STRING

static struct espconn *udp_info_socket = NULL;

static char device_info_buffer[64]; // Buffer to store the device info

// Check the content of the received UDP-message and forward the nodes meta-data to the sender in case of a valid request
static void ICACHE_FLASH_ATTR udp_info_recv_cb(void *arg, char *data, unsigned short len) {
  if (!arg || !data || len == 0) {
    os_printf("udp_info_recv_cb: Invalid transfer parameters!\n");
    return;
  }

  // Print the received message
  os_printf("udp_info_recv_cb: %s\n", data);

  // Check, if the message is a valid information-request
  if (len == strlen(request_string) && os_memcmp(data, request_string, len) == 0) {
    uint8_t resp_len = 0, op_mode = 0;
    struct ip_info ipconfig;
    uint8_t mac_addr[6];  // Refrain from using mesh_device_mac_type from mesh_device.h at this point to keep this class seperated from the mesh-application and therewith independent
    remot_info *con_info = NULL;

    // Check for the operation-mode of the device and get the respective IP- and MAC-adress
    op_mode = wifi_get_opmode();
    if (op_mode == SOFTAP_MODE || op_mode == STATION_MODE || op_mode == STATIONAP_MODE) { // Prevent errors resulting from runtime-conditions concerning the WiFi-operation-mode (e.g. if the device is switched into sleep-mode)
      if (op_mode == SOFTAP_MODE) {
        wifi_get_ip_info(SOFTAP_IF, &ipconfig);
        wifi_get_macaddr(SOFTAP_IF, mac_addr);
      }
      else {
        wifi_get_ip_info(STATION_IF, &ipconfig);
        wifi_get_macaddr(STATION_IF, mac_addr);
      }

      // Clear the Buffer
      os_memset(device_info_buffer, 0, sizeof(device_info_buffer));

      // Print the devices meta-data into the buffer and obtain the actual length of the resulting String
      // Structure: PURPOSE,MAC,IP (allows easy CSV-parsing)
      resp_len = os_sprintf(device_info_buffer, "%s," MACSTR "," IPSTR "\n", DEVICE_PURPOSE, MAC2STR(mac_addr), IP2STR(&ipconfig.ip));

      // Get the connection information
      if (espconn_get_connection_info(udp_info_socket, &con_info, 0) == ESPCONN_OK) {
        udp_info_socket->proto.udp->remote_port = con_info->remote_port;
        os_memcpy(udp_info_socket->proto.udp->remote_ip, con_info->remote_ip, 4);

        // Return the devices meta-data to the sender
        if (espconn_sendto(udp_info_socket, device_info_buffer, resp_len) == ESPCONN_OK) {
          os_printf("udp_info_recv_cb: Sent meta-data to " IPSTR ":%d!\n", IP2STR(udp_info_socket->proto.udp->remote_ip), udp_info_socket->proto.udp->remote_port);
        }
        else {
          os_printf("udp_info_recv_cb: Error while sending meta-data!\n");
        }
      }
      else {
        os_printf("udp_info_recv_cb: Failed to retrieve connection info!\n");
      }
    }
    else {
      os_printf("udp_info_recv_cb: Wrong WiFi-operation-mode!\n");
    }
  }
}

// Free all occupied resources
void ICACHE_FLASH_ATTR device_info_disable(void) {
  os_printf("device_info_disable: Disable device_info!\n");

  if (udp_info_socket) {
    os_free(udp_info_socket);
  }
}

// Initialize the UDP-socket and set up its configuration
void ICACHE_FLASH_ATTR device_info_init(void) {
  os_printf("device_info_init: Initialize device_info!\n");

  // Initialize the UDP-socket
  udp_info_socket = (struct espconn *) os_zalloc(sizeof(struct espconn));
  if (!udp_info_socket) {
    os_printf("device_info_init: Failed to initialize the UDP-socket!\n");
    return;
  }

  // Set up UDP-socket-configuration
  udp_info_socket->type = ESPCONN_UDP;
  udp_info_socket->state = ESPCONN_NONE;
  udp_info_socket->proto.udp = (esp_udp *) os_zalloc(sizeof(esp_udp));
  udp_info_socket->proto.udp->local_port = DEVICE_INFO_PORT;

  // Create UDP-socket and register sent-callback
  if (!espconn_create(udp_info_socket)) {
    espconn_regist_recvcb(udp_info_socket, udp_info_recv_cb);
  }
  else {
    os_printf("device_info_init: Error while creating the UDP-socket!\n");
  }
}
