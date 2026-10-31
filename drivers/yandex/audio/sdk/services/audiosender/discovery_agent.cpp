#include "discovery_agent.h"

#include "common.h"

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/system/byteorder.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define SSDP_PORT 1900
#define SSDP_MULTICAST_ADDRESS "239.255.255.250"
#define BUF_SIZE 1024

YIO_DEFINE_LOG_MODULE("audio_sender");

using namespace AudioSender;

DiscoveryAgent::DiscoveryAgent(std::shared_ptr<YandexIO::IDevice> device)
    : device_(std::move(device))
    , softwareVersion_(device_->softwareVersion())
{
}

void DiscoveryAgent::start() {
    isRunning_ = true;
    discoveryThread_ = std::thread(&DiscoveryAgent::discovery, this);
    discoveryThread_.detach();
}

void DiscoveryAgent::stop() {
    isRunning_ = false;
    if (discoveryThread_.joinable()) {
        discoveryThread_.join();
    }
}

void DiscoveryAgent::discovery() {
    struct sockaddr_in addr;
    struct sockaddr_in back_addr;
    int rcv_socket;
    int send_socket;
    socklen_t addrlen = sizeof(addr);
    struct ip_mreq mreq;
    char msg_buf[BUF_SIZE];
    size_t nbytes;
    int yes = 1;

    // Create rcv_socket
    if ((rcv_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        YIO_LOG_ERROR_EVENT("DiscoveryAgent.CreateSocketFailed", "errno: " << errno);
        return;
    }

    // Set reuse option
    if (setsockopt(rcv_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        YIO_LOG_ERROR_EVENT("DiscoveryAgent.SetReuseAddressFailed", "errno: " << errno);
        return;
    }

    // Bind
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = HostToInet<uint32_t>(INADDR_ANY);
    addr.sin_port = HostToInet<uint16_t>(SSDP_PORT);
    if (bind(rcv_socket, (struct sockaddr*)&addr, addrlen) < 0) {
        YIO_LOG_ERROR_EVENT("DiscoveryAgent.BindToInaddrAnyFailed", "errno: " << errno);
        return;
    }

    while (isRunning_) {
        // Join to multicast group
        mreq.imr_multiaddr.s_addr = inet_addr(SSDP_MULTICAST_ADDRESS);
        mreq.imr_interface.s_addr = HostToInet<uint32_t>(INADDR_ANY);
        if (setsockopt(rcv_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            YIO_LOG_ERROR_EVENT("DiscoveryAgent.CannotJoinMulticastGroup", "errno: " << errno);
            for (size_t i = 0; i < 5 && isRunning_; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } else {
            break;
        }
    }
    if (!isRunning_) {
        YIO_LOG_ERROR_EVENT("DiscoveryAgent.StoppedDuringInit", "");
        return;
    }

    if ((send_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        YIO_LOG_ERROR_EVENT("DiscoveryAgent.CannotCreateSendSocket", "errno: " << errno);
        return;
    }

    while (isRunning_) {
        YIO_LOG_TRACE("Receiving data (recvfrom)");
        if ((nbytes = recvfrom(rcv_socket,
                               msg_buf,
                               BUF_SIZE,
                               0,
                               (struct sockaddr*)&addr,
                               &addrlen)) == static_cast<size_t>(-1))
        {
            YIO_LOG_ERROR_EVENT("DiscoveryAgent.SocketReceiveError", "errno: " << errno);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        YIO_LOG_TRACE(msg_buf);

        std::string sender_addr(inet_ntoa(addr.sin_addr));
        YIO_LOG_TRACE("From: " << sender_addr);

        std::string msg(msg_buf);
        if (msg.find("ST: quasar:audiosender") != std::string::npos) {
            size_t uuid_index = msg.find("S: uuid:");
            std::string uuid = msg.substr(uuid_index + 9, uuid_index + 43);
            YIO_LOG_TRACE(uuid);

            back_addr.sin_family = AF_INET;
            back_addr.sin_port = HostToInet<uint16_t>(SSDP_PORT);
            back_addr.sin_addr = addr.sin_addr;

            YIO_LOG_TRACE("\nSending answer");

            const std::string deviceType = device_->configuration()->getDeviceType();
            const std::string deviceId = device_->deviceId();

            std::string answer = std::string("HTTP/1.1 200 OK\n\n") +
                                 "S: uuid:" + uuid + "\nExt:\n" +
                                 "Cache-Control: no-cache=\"Ext\", max-age = 5000\n" +
                                 "ST: quasar:audiosender:" + softwareVersion_ + "\n" +
                                 "AL: <quasar:audiosender><http://" + deviceId + "." + deviceType + "/>";

            if (sendto(send_socket,
                       answer.c_str(),
                       answer.size(),
                       0,
                       (const sockaddr*)&back_addr,
                       sizeof(back_addr)) < 0)
            {
                YIO_LOG_ERROR_EVENT("DiscoveryAgent.SendingAnswerFailed", "errno: " << errno);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        memset(&msg_buf, 0, BUF_SIZE);
    }
    YIO_LOG_INFO("DiscoveryAgent was stopeed");
}
