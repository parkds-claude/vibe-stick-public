#include "ble_hid.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include "debug_trace.h"
#include "logic/clock.h"
#include "logic/link_tracker.h"

namespace {

// 표준 키보드 보고 형식(보고 ID 1): [수정키, 예약, 키1..키6]
// 이 형식을 바꾸면 컴퓨터가 옛 형식을 기억하고 있어, 컴퓨터에서 기기를 지우고 다시 짝을 맺어야 한다.
// 그래서 키 코드 범위는 처음부터 넉넉히(0~231) 잡아 둔다.
const uint8_t KB_REPORT_ID  = 1;  // 아래 REPORT_MAP 의 Report ID 와 같아야 한다
const size_t  KB_REPORT_LEN = 8;  // [수정키, 예약, 키1..키6]

const uint8_t REPORT_MAP[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, KB_REPORT_ID,  //   Report ID
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224)
    0x29, 0xE7,        //   Usage Maximum (231)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute) — 수정키
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant) — 예약
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x05,        //   Usage Maximum (5)
    0x91, 0x02,        //   Output (Data, Variable, Absolute) — LED
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Constant) — LED 채움
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xE7, 0x00,  //   Logical Maximum (231) — F13~F24(초점 키 후보)까지 쓸 수 있게
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xE7, 0x00,  //   Usage Maximum (231)
    0x81, 0x00,        //   Input (Data, Array) — 키 6개
    0xC0,              // End Collection
};

// 암호화가 끝난 뒤 호스트가 HID 를 쓸 준비를 마칠 때까지 두는 여유.
// 이 전에 보낸 첫 키가 사라지는 문제가 여러 BLE 키보드 구현에서 보고됐다.
const uint32_t SETTLE_MS = 300;
// 연결은 됐는데 이 시간 안에 암호화가 끝나지 않으면 끊는다.
// (기기 쪽 결합만 지워진 경우 등 — 그대로 두면 "연결 중"에 영원히 머문다.)
const uint32_t CONNECT_TIMEOUT_MS = 30000;
const int      SEND_ATTEMPTS      = 3;     // 전송 실패 시 다시 시도하는 횟수
const uint32_t SEND_RETRY_MS      = 2;     // 그 사이 간격
const uint32_t DISCONNECT_WAIT_MS = 1000;  // 짝 초기화 때 연결이 끊기기를 기다리는 최대 시간

NimBLEServer*         server = nullptr;
NimBLEHIDDevice*      hidDev = nullptr;
NimBLECharacteristic* input  = nullptr;

// 연결 상태 판정. 콜백(NimBLE 호스트 태스크)이 사건을 넣고 loop 태스크가 상태를 읽는다.
LinkTracker tracker(SETTLE_MS, CONNECT_TIMEOUT_MS);

// loop 태스크에서만 쓰는 값
bool releasePending = false;

class Callbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    DBG_LOG("[ble] connect handle=%u bonded=%d enc=%d t=%lu\n", info.getConnHandle(),
            info.isBonded(), info.isEncrypted(), (unsigned long)millis());
    // 다시 연결될 때는 암호화가 이 콜백보다 먼저 끝나 있다(실측) → 연결의 실제 상태를 넘긴다.
    tracker.onConnected(millis(), info.isEncrypted() && info.isBonded());
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    DBG_LOG("[ble] disconnect reason=0x%x t=%lu\n", reason, (unsigned long)millis());
    tracker.onDisconnected();
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    DBG_LOG("[ble] auth enc=%d bonded=%d bonds=%d t=%lu\n", info.isEncrypted(), info.isBonded(),
            NimBLEDevice::getNumBonds(), (unsigned long)millis());
    // 암호화뿐 아니라 결합(키 저장)까지 된 상대만 받는다.
    if (!info.isEncrypted() || !info.isBonded()) {
      server->disconnect(info);
      return;
    }
    // 한 번에 한 컴퓨터만: 이미 결합이 있는데 다른 상대가 새로 결합했다면
    // 새 결합을 지우고 끊는다. 새 컴퓨터로 바꾸려면 forgetPeers() 를 먼저 한다.
    if (NimBLEDevice::getNumBonds() > 1) {
      NimBLEDevice::deleteBond(info.getIdAddress());
      server->disconnect(info);
      return;
    }
    tracker.onSecured(millis());
    DBG_LOG("[ble] auth done\n");
  }
};

}  // namespace

namespace blehid {

void begin(const char* deviceName) {
  NimBLEDevice::init(deviceName);
  // D11: 결합 O, 숫자 확인 X(Just Works), 보안 연결 O
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  // 암호 키와 신원 키를 주고받는다(맥 재연결에 필요). 라이브러리 기본값과 같지만
  // 기본값이 바뀐 이력이 있어 명시한다.
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new Callbacks());
  server->advertiseOnDisconnect(true);

  hidDev = new NimBLEHIDDevice(server);
  input = hidDev->getInputReport(KB_REPORT_ID);
  static const uint8_t NO_KEYS[KB_REPORT_LEN] = {0};
  input->setValue(NO_KEYS, sizeof(NO_KEYS));  // 호스트가 읽으면 "모두 뗌"이 나오게
  hidDev->getOutputReport(KB_REPORT_ID);  // 보고 형식에 LED 출력이 선언돼 있어 특성을 만들어 둔다(값은 쓰지 않음)
  hidDev->setManufacturer("vibe-stick");
  // D14: 시험대용 식별값. 출처 0x01(Bluetooth SIG) + 0x02E5(Espressif) + 임의 제품 번호.
  // 짝을 맺은 뒤에 바꾸면 컴퓨터에서 기기를 지우고 다시 맺어야 한다.
  hidDev->setPnp(0x01, 0x02E5, 0x5653, 0x0100);
  hidDev->setHidInfo(0x00, 0x01);
  hidDev->setReportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  hidDev->setBatteryLevel(100);
  server->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);  // 0x03C1 (NimBLEHIDDevice.h)
  adv->addServiceUUID(hidDev->getHidService()->getUUID());
  // 이름은 광고 본문에 넣는다(15자 이하여야 애플 기기 목록에 보인다).
  // 결합된 상대만 받는 광고(지향성·화이트리스트)는 애플 기기와 맞지 않아 쓰지 않는다.
  adv->setName(deviceName);
  adv->start();
}

Link link() {
  // 결합 수는 끊겨 있을 때만 쓰인다("찾는 중"과 "짝 맺기 대기"를 가른다).
  // 연결돼 있는 동안에는 저장소를 들여다보지 않는다(매 틱 불리는 함수라서).
  const bool hasBond = tracker.connected() || NimBLEDevice::getNumBonds() > 0;
  return tracker.state(millis(), hasBond);
}

bool send(uint8_t mods, uint8_t key) {
  if (link() != Link::Ready) return false;
  const uint8_t report[KB_REPORT_LEN] = {mods, 0, key, 0, 0, 0, 0, 0};
  // 값을 직접 실어 보내는 경로: 호출마다 바로 전송돼 누름과 뗌이 합쳐지지 않는다.
  // 전송 버퍼가 잠깐 모자랄 수 있어 몇 번 다시 시도한다.
  for (int attempt = 0; attempt < SEND_ATTEMPTS; attempt++) {
    DBG_STAGE(100 + attempt);  // 100~102: 전송(notify) 안
    const bool sent = input->notify(report, sizeof(report));
    DBG_STAGE(110);
    if (sent) {
      // 보고는 "지금 눌린 키 전체"다. 전송에 성공했으면 그 전에 못 보낸 "뗌"은 이 보고가 대신한다.
      // (대기 표시를 남겨 두면 다음 틱에 poll() 이 방금 누른 키를 떼어 버린다.)
      releasePending = false;
      return true;
    }
    delay(SEND_RETRY_MS);
  }
  return false;
}

bool releaseAll() {
  const bool ok = send(0, 0);
  // 연결은 살아 있는데 "뗌"을 못 보냈다면 컴퓨터에서 키가 눌린 채 남는다 → poll() 이 다시 보낸다.
  releasePending = !ok && link() == Link::Ready;
  return ok;
}

void poll(uint32_t nowMs) {
  // 못 보낸 "뗌"을 다시 보낸다. 그 사이 끊겼다면 releaseAll() 이 대기 표시를 지운다
  // (끊기면 컴퓨터가 뗀 것으로 처리한다).
  if (releasePending) releaseAll();
  if (tracker.connectTimedOut(nowMs)) {
    DBG_LOG("[ble] connect timeout -> disconnect\n");
    if (server->getConnectedCount() > 0) server->disconnect(server->getPeerInfo(0));
  }
}

void setBattery(uint8_t pct) {
  // 암호화 전에 알리면 저장된 구독 정보가 망가져 재연결 뒤 알림이 안 나가는 문제가 알려져 있다.
  if (hidDev) hidDev->setBatteryLevel(pct, tracker.secured());
}

bool forgetPeers() {
  if (!server) return false;
  // 지우기 전에 연결을 끊고 광고를 멈춘다. 광고 중에는 신원 키(IRK)를 준 상대(맥)의
  // 결합이 지워지지 않는다(ble_gap_unpair 가 BUSY 로 돌아온다).
  server->advertiseOnDisconnect(false);
  if (server->getConnectedCount() > 0) {
    server->disconnect(server->getPeerInfo(0));
    const uint32_t t0 = millis();
    while (server->getConnectedCount() > 0 && !elapsedMs(millis(), t0, DISCONNECT_WAIT_MS)) delay(10);
  }
  NimBLEDevice::stopAdvertising();
  NimBLEDevice::deleteAllBonds();
  tracker.dropSecurity();
  releasePending = false;

  const bool ok = (NimBLEDevice::getNumBonds() == 0);
  if (!ok) {
    // 실패: 원래대로 되살린다.
    server->advertiseOnDisconnect(true);
    if (server->getConnectedCount() == 0) NimBLEDevice::startAdvertising();
  }
  return ok;
}

}  // namespace blehid
