#pragma once
// ============================================================================
//  EVDUart.h - link to the EnkelvoudSource board (BT / AUX / USB selection)
// ============================================================================
//
//  The source board is a third ESP32-S3 that owns Bluetooth, the AUX input and
//  the USB host. It has its own UART grammar (see EnkelvoudSource
//  EVDBTUart.h), unchanged here:
//
//    S3 -> source : GET_BT_DEVICES | KICK_BT | REMOVE_BT <mac>
//                   CMD:next | CMD:prev | SRC:BT | SRC:AUX | SRC:USB
//    source -> S3 : BT_JSON|[...] | LOG|<line> | OK|... | ERR|...
//
//  This file only carries that grammar; it makes no policy decisions. The
//  dashboard's /api/source and /api/cmd handlers write into it from core 0.
// ============================================================================
#include <HardwareSerial.h>

inline void setupReceiverLink() {
  if (!receiverUart) {
    receiverUart = new HardwareSerial(1);
    receiverUart->begin(115200, SERIAL_8N1, pinSrcUartRx, pinSrcUartTx);
    LOGI("source UART: rx %d tx %d @115200", pinSrcUartRx, pinSrcUartTx);
  }
}

// One command out. Uppercase on the wire: the source parser is
// case-insensitive but every log line it prints is uppercase, and matching
// that keeps a captured trace readable.
inline bool bridgeSendReceiverCmd(const String &cmd, String &respBody,
                                  uint16_t &httpCode) {
  if (!receiverUart) {
    httpCode = 503;
    respBody = "{\"ok\":false,\"error\":\"UART unavailable\"}";
    return false;
  }
  if (!lockFor(uartMutex, 50)) {
    httpCode = 503;
    respBody = "{\"ok\":false,\"error\":\"UART busy\"}";
    return false;
  }
  receiverUart->printf("CMD:%s\n", cmd.c_str());
  unlockFor(uartMutex);
  httpCode = 200;
  respBody = String("{\"ok\":true,\"cmd\":\"") + cmd + "\",\"transport\":\"uart\"}";
  return true;
}

// Read whatever the source board has said since the last call. Bounded per
// call so a chatty source board cannot monopolise loop().
inline void serviceReceiverLink() {
  if (!receiverUart) return;
  static char   line[1200];
  static size_t lineLen = 0;

  int budget = 512;
  while (receiverUart->available() && budget-- > 0) {
    char c = (char)receiverUart->read();
    if (c == '\n' || c == '\r') {
      if (lineLen == 0) continue;
      line[lineLen] = '\0';
      if (strncmp(line, "BT_JSON|", 8) == 0) {
        if (lockFor(uartMutex, 20)) {
          receiverBtJson = String(line + 8);
          unlockFor(uartMutex);
        }
      } else if (strncmp(line, "LOG|", 4) == 0) {
        LOGI("src: %s", line + 4);
      } else if (strncmp(line, "ERR|", 4) == 0) {
        LOGW("src: %s", line + 4);
      }
      lineLen = 0;
      continue;
    }
    if (lineLen < sizeof(line) - 1) line[lineLen++] = c;
    else lineLen = 0;   // overflow: drop the line rather than truncate it
  }
}
