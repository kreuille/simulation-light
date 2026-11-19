/*
  msla_detect — MSLA detection library, portable and without dynamic memory
  =========================================================================
  Detection logic taken from the simulator (print/simulateur/index.html) and the
  firmware (print/firmware/banc_msla/banc_msla.ino), put together in a pure C++17
  library, usable both:
    - by the ESP32 firmware (no Arduino dependency inside the library);
    - by the parity tests on a PC (same verdicts as the simulator).

  Rules:
    - no dynamic memory: all buffers come from the caller, or have a fixed size
      (MSLA_MAX_COUCHES, BUF of the vat control);
    - double precision, like the JavaScript Number, for identical verdicts;
    - comments in English; a Chinese summary is in each header.

  Modules:
    msla_filtres   — median, med3, moving average
    msla_couche    — per-layer detection (features, learning, verdict)
    msla_stabilite — resin settle
    msla_bac       — vat control before a print, planarity, weigh, leak

  Include this header to get everything.

  中文：MSLA 检测库，可移植、无动态内存。检测逻辑取自模拟器与固件，合并为纯 C++17 库，
  既供 ESP32 固件使用（库内不依赖 Arduino），也供 PC 上的一致性测试使用（判定与模拟器相同）。
  规则：无动态内存（缓冲区由调用方提供或固定大小）；double 精度（与 JS Number 一致）；
  注释为英文，各头文件含中文摘要。模块：msla_filtres（滤波）、msla_couche（逐层检测）、
  msla_stabilite（树脂稳定）、msla_bac（料槽检查/平面度/称重/漏液）。包含本头文件即可获得全部。
*/
#ifndef MSLA_DETECT_H
#define MSLA_DETECT_H

#include "msla_filtres.h"
#include "msla_couche.h"
#include "msla_stabilite.h"
#include "msla_bac.h"

#endif  // MSLA_DETECT_H
