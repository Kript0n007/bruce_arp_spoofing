#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <TFT_eSPI.h>
#include "update.h"

extern TFT_eSPI tft;  // Usa a definição externa do objeto tft

const char* versionUrl = "https://kript0n007.github.io/bruce_arp_spoofing/version.txt";
const char* firmwareUrl = "https://kript0n007.github.io/bruce_arp_spoofing/firmware.bin";
const char* currentVersion = "1.5"; 

void performOTA();

void drawLoadingAnimation() {
    static int pos = 0;
    tft.setCursor(10, 40);
    tft.fillRect(10, 40, 100, 10, TFT_BLACK);
    for (int i = 0; i < pos; i++) {
        tft.print(".");
    }
    pos = (pos + 1) % 4;
}

void showStatusMessage(const char* message) {
    tft.println(message);
    Serial.println(message);
}

void waitForButtonPress() {
    showStatusMessage("Press the button to exit...");
    while (true) {
        if (digitalRead(37) == LOW) {  // SEL_BTN
            break;
        }
        drawLoadingAnimation();
        delay(500);
    }
}

int versionToInt(String version) {
    version.replace(".", "");
    return version.toInt();
}

void checkForUpdate() {
    WiFiClientSecure client;  // Use WiFiClientSecure para HTTPS
    HTTPClient http;

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Checking for updates...");
    tft.setTextSize(1);

    showStatusMessage("Checking for firmware versiooooooooooooon...");
    client.setInsecure();  // Desabilitar verificação de certificado para simplicidade
    http.begin(client, versionUrl);
    int httpCode = http.GET();

    while (http.connected() && (httpCode < 0)) {
        drawLoadingAnimation();
        delay(500);
    }

    if (httpCode == HTTP_CODE_OK) {
        String newVersion = http.getString();
        newVersion.trim(); // Remove whitespace

        int newVersionInt = versionToInt(newVersion);
        int currentVersionInt = versionToInt(currentVersion);

        Serial.print("Current version: ");
        Serial.println(currentVersion);
        Serial.print("Current version int: ");
        Serial.println(currentVersionInt);
        Serial.print("New version: ");
        Serial.println(newVersion);
        Serial.print("New version int: ");
        Serial.println(newVersionInt);

        if (newVersionInt > currentVersionInt) {
            char buffer[50];
            sprintf(buffer, "New version available: %s", newVersion.c_str());
            showStatusMessage(buffer);
            performOTA();
        } else {
            showStatusMessage("Firmware is up to date.");
            waitForButtonPress();
        }
    } else {
        char buffer[50];
        sprintf(buffer, "HTTP error code: %d", httpCode);
        showStatusMessage("HTTP request failed.");
        showStatusMessage(http.errorToString(httpCode).c_str());
        showStatusMessage(buffer);
        waitForButtonPress();
    }
    http.end();
}

void performOTA() {
    WiFiClientSecure client;  // Use WiFiClientSecure para HTTPS
    HTTPClient http;

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Performing OTA...");
    tft.setTextSize(1);

    showStatusMessage("Checking for firmware update...");
    client.setInsecure();  // Desabilitar verificação de certificado para simplicidade
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Configura para seguir redirecionamentos
    http.begin(client, firmwareUrl);
    int httpCode = http.GET();

    while (http.connected() && (httpCode < 0)) {
        drawLoadingAnimation();
        delay(500);
    }

    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        bool canBegin = Update.begin(contentLength);

        Serial.print("Content length: ");
        Serial.println(contentLength);
        Serial.print("Can begin update: ");
        Serial.println(canBegin ? "Yes" : "No");

        if (canBegin) {
            showStatusMessage("Begin OTA update...");
            WiFiClient * stream = http.getStreamPtr();
            size_t written = Update.writeStream(*stream);

            Serial.print("Written bytes: ");
            Serial.println(written);

            if (written == contentLength) {
                showStatusMessage("OTA update completed!");
                if (Update.end()) {
                    showStatusMessage("Update successfully applied, restarting...");
                    delay(2000);  // Adiciona um atraso para garantir que a mensagem seja exibida
                    ESP.restart();
                } else {
                    showStatusMessage("Update failed.");
                    showStatusMessage(Update.errorString());
                    Serial.print("Update failed: ");
                    Serial.println(Update.errorString());
                    waitForButtonPress();
                }
            } else {
                char buffer[50];
                sprintf(buffer, "Written only: %d/%d. Retry?", written, contentLength);
                showStatusMessage(buffer);
                waitForButtonPress();
            }
        } else {
            showStatusMessage("Not enough space to begin OTA update");
            waitForButtonPress();
        }
    } else {
        char buffer[50];
        sprintf(buffer, "HTTP error code: %d", httpCode);
        showStatusMessage("HTTP request failed.");
        showStatusMessage(http.errorToString(httpCode).c_str());
        showStatusMessage(buffer);
        waitForButtonPress();
    }
    http.end();
}
