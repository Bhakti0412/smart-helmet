// ble_link.h - BLE GATT server (ESP32 built-in BLE stack).
// Characteristics:  EVENT (notify) | STATUS (notify/read) | COMMAND (write: "CANCEL")
#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "config.h"

class BleLink : public BLEServerCallbacks, public BLECharacteristicCallbacks {
 public:
  void begin() {
    BLEDevice::init(BLE_DEVICE_NAME);
    BLEDevice::setMTU(BLE_MTU);
    server_ = BLEDevice::createServer();
    server_->setCallbacks(this);

    BLEService* svc = server_->createService(SERVICE_UUID);

    eventChar_ = svc->createCharacteristic(CHAR_EVENT_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    eventChar_->addDescriptor(new BLE2902());

    statusChar_ = svc->createCharacteristic(
        CHAR_STATUS_UUID, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
    statusChar_->addDescriptor(new BLE2902());

    cmdChar_ = svc->createCharacteristic(
        CHAR_COMMAND_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    cmdChar_->setCallbacks(this);

    svc->start();
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
  }

  bool connected() const { return connected_; }

  // Returns true once if the phone asked to cancel the crash countdown.
  bool consumeCancel() {
    if (!cancelRequested_) return false;
    cancelRequested_ = false;
    return true;
  }

  // {"e":"HARD_BRAKE","v":0.52,"t":123456}
  void sendEvent(const char* name, float value, uint32_t uptimeMs) {
    char buf[96];
    int n = snprintf(buf, sizeof(buf), "{\"e\":\"%s\",\"v\":%.2f,\"t\":%lu}", name, value,
                     (unsigned long)uptimeMs);
    if (n <= 0 || !connected_) return;
    eventChar_->setValue((uint8_t*)buf, (size_t)n);
    eventChar_->notify();
  }

  // {"night":1,"ldr":850,"st":"IDLE"}   (+ ,"lat":..,"lon":.. when the optional GPS has a fix)
  void sendStatus(bool night, int ldr, const char* crashState, bool hasFix, double lat, double lon) {
    char buf[128];
    int n;
    if (hasFix) {
      n = snprintf(buf, sizeof(buf), "{\"night\":%d,\"ldr\":%d,\"st\":\"%s\",\"lat\":%.6f,\"lon\":%.6f}",
                   night ? 1 : 0, ldr, crashState, lat, lon);
    } else {
      n = snprintf(buf, sizeof(buf), "{\"night\":%d,\"ldr\":%d,\"st\":\"%s\"}", night ? 1 : 0, ldr,
                   crashState);
    }
    if (n <= 0) return;
    statusChar_->setValue((uint8_t*)buf, (size_t)n);
    if (connected_) statusChar_->notify();
  }

  // --- callbacks (run on the BLE task) ---
  void onConnect(BLEServer*) override { connected_ = true; }
  void onDisconnect(BLEServer*) override {
    connected_ = false;
    BLEDevice::startAdvertising();       // allow the phone to reconnect
  }
  void onWrite(BLECharacteristic* c) override {
    auto v = c->getValue();              // std::string (core 2.x) or String (core 3.x)
    if (v.length() >= 6 && strncmp(v.c_str(), "CANCEL", 6) == 0) cancelRequested_ = true;
  }

 private:
  BLEServer* server_ = nullptr;
  BLECharacteristic *eventChar_ = nullptr, *statusChar_ = nullptr, *cmdChar_ = nullptr;
  volatile bool connected_ = false;
  volatile bool cancelRequested_ = false;
};
