//*******************************************************
// Copyright (c) MLRS project
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
// OlliW @ www.olliw.eu
//*******************************************************
// CRSF Interface Header
//********************************************************
#ifndef CRSF_INTERFACE_H
#define CRSF_INTERFACE_H
#pragma once

#if (!defined DEVICE_HAS_MBRIDGE) && (SETUP_TX_CHANNELS_SOURCE == 3)
  #undef SETUP_TX_CHANNELS_SOURCE
  #define SETUP_TX_CHANNELS_SOURCE  0
  #warning Device does not support mBridge, so CRSF has been disabled !
#endif

#if (SETUP_TX_CHANNELS_SOURCE == 3) && (defined DEVICE_HAS_MBRIDGE)

#include "..\Common\thirdparty.h"
#include "crsf_protocol.h"


//-------------------------------------------------------
// Interface Implementation

uint16_t micros(void);

void uart_rx_callback(uint8_t c);
void uart_tc_callback(void);

#define UART_RX_CALLBACK_FULL(c)    uart_rx_callback(c)
#define UART_TC_CALLBACK()          uart_tc_callback()

#include "..\modules\stm32ll-lib\src\stdstm32-uart.h"


void uart_putc_tobuf(char c)
{
  uint16_t next = (uart_txwritepos + 1) & UART_TXBUFSIZEMASK;
  if (uart_txreadpos != next) { // fifo not full //this is isr safe, works also if readpos has changed in the meanwhile
    uart_txbuf[next] = c;
    uart_txwritepos = next;
  }
}


void uart_tx_start(void)
{
  LL_USART_EnableIT_TXE(UART_UARTx); // initiates transmitting
}


class tTxCrsfBase : public tSerialBase
{
  public:

    void Init(void);
    bool IsEmpty(void);

    // interface to the timer and uart hardware peripheral used for the bridge
    uint16_t tim_us(void) { return micros(); }
    void transmit_enable(bool flag) { uart_rx_enableisr((flag) ? DISABLE : ENABLE); }
    bool mb_rx_available(void) { return uart_rx_available(); }
    char mb_getc(void) { return uart_getc(); }
    void mb_putc(char c) { uart_putc_tobuf(c); }

    // for in-isr processing
    //void parse_nextchar(uint8_t c);
    bool transmit_start(void); // returns true if transmission should be started

    typedef enum {
      STATE_IDLE = 0,

      STATE_RECEIVE_CRSF_LEN,
      STATE_RECEIVE_CRSF_PAYLOAD,
      STATE_RECEIVE_CRSF_CRC,

      STATE_TRANSMIT_START,
      STATE_TRANSMITING,
    } STATE_ENUM;

    uint8_t state;
    uint8_t len;
    uint8_t cnt;
    uint16_t tlast_us;

    uint8_t frame[128];

    volatile bool frame_received;

    volatile uint8_t tx_available; // this signals if something needs to be send to radio
    uint8_t tx_frame[128];

    // for CRSF
    virtual void crsf_parse_nextchar(uint8_t c) {};
};


class tTxCrsf : public tTxCrsfBase
{
  public:
    uint8_t crc8(const uint8_t* buf);

    bool IsChannelData(void);
    void SendLinkStatistics(tCrsfLinkStatistics* payload); // in OpenTx this triggers telemetryStreaming
    void SendLinkStatisticsTx(tCrsfLinkStatisticsTx* payload);
    void SendLinkStatisticsRx(tCrsfLinkStatisticsRx* payload);

    void crsf_parse_nextchar(uint8_t c) override;
};

tTxCrsf crsf;


// we do not add a delay here before we transmit
// the logic analyzer shows this gives a 30-35 us gap nevertheless, which is perfect

void uart_rx_callback(uint8_t c)
{
  LED_RIGHT_GREEN_ON;

  if (crsf.state >= tTxCrsf::STATE_TRANSMIT_START) { // recover in case something went wrong
      crsf.state = tTxCrsf::STATE_IDLE;
  }

  crsf.crsf_parse_nextchar(c);

  if (crsf.transmit_start()) { // check if a transmission waits, put it into buf and return true to start
      uart_tx_start();
  }

  LED_RIGHT_GREEN_OFF;
}


void uart_tc_callback(void)
{
  crsf.transmit_enable(false); // switches on rx
  crsf.state = tTxCrsf::STATE_IDLE;
}


bool tTxCrsfBase::transmit_start(void)
{
  if (state < STATE_TRANSMIT_START) return false; // we are in receiving

  if (!tx_available || (state != STATE_TRANSMIT_START)) {
    state = STATE_IDLE;
    return false;
  }

  transmit_enable(true); // switches of rx

  for (uint8_t i = 0; i < tx_available; i++) {
      uint8_t c = tx_frame[i];
      mb_putc(c);
  }

  tx_available = 0;
  state = STATE_TRANSMITING;
  return true;
}


void tTxCrsfBase::Init(void)
{
  tSerialBase::Init();

  transmit_enable(false);

#if defined MBRIDGE_TX_XOR || defined MBRIDGE_RX_XOR
  gpio_init(MBRIDGE_TX_XOR, IO_MODE_OUTPUT_PP_HIGH, IO_SPEED_VERYFAST);
  gpio_init(MBRIDGE_RX_XOR, IO_MODE_OUTPUT_PP_HIGH, IO_SPEED_VERYFAST);
  MBRIDGE_TX_SET_INVERTED;
  MBRIDGE_RX_SET_INVERTED;
#endif

  uart_init_isroff();

#if defined MBRIDGE_RX_TX_INVERT_INTERNAL
  LL_USART_Disable(UART_UARTx);
  LL_USART_SetTXPinLevel(MBRIDGE_UARTx, LL_USART_TXPIN_LEVEL_INVERTED);
  LL_USART_SetRXPinLevel(MBRIDGE_UARTx, LL_USART_RXPIN_LEVEL_INVERTED);
  LL_USART_Enable(UART_UARTx);
#endif

  transmit_enable(false);

  frame_received = false;
  state = STATE_IDLE;
  tlast_us = 0;
  tx_available = 0;
}


bool tTxCrsfBase::IsEmpty(void)
{
    return (tx_available == 0);
}


//-------------------------------------------------------
// CRSF Bridge

// a frame is sent every 4 ms, frame length is max 64 bytes
// a byte is 25 us
// gaps between frames are 1 ms or so
#define CRSF_TMO_US        500


// CRSF frame format:
// adress len type payload crc
// len is the length including type, payload, crc

void tTxCrsf::crsf_parse_nextchar(uint8_t c)
{
  uint16_t tnow_us = tim_us();

  if (state != STATE_IDLE) {
      uint16_t dt = tnow_us - tlast_us;
      if (dt > CRSF_TMO_US) state = STATE_IDLE;
  }

  tlast_us = tnow_us;

  switch (state) {
  case STATE_IDLE:
      if (c == CRSF_ADDRESS_MODULE) {
        cnt = 0;
        frame[cnt++] = c;
        state = STATE_RECEIVE_CRSF_LEN;
      }
      break;

  case STATE_RECEIVE_CRSF_LEN:
      frame[cnt++] = c;
      len = c;
      state = STATE_RECEIVE_CRSF_PAYLOAD;
      break;
  case STATE_RECEIVE_CRSF_PAYLOAD:
      frame[cnt++] = c;
      if (cnt >= len + 1) {
        state = STATE_RECEIVE_CRSF_CRC;
      }
    break;
  case STATE_RECEIVE_CRSF_CRC:
      frame[cnt++] = c;
      // let's just ignore it
      frame_received = true;
      state = STATE_TRANSMIT_START;
      break;
  }
}


uint8_t tTxCrsf::crc8(const uint8_t* buf)
{
  return crc8_update(0, &(buf[2]), buf[1] - 1, 0xD5);
}


bool tTxCrsf::IsChannelData(void)
{
  return (frame[2] == CRSF_FRAME_ID_CHANNELS);
}


void tTxCrsf::SendLinkStatistics(tCrsfLinkStatistics* payload)
{
  constexpr uint8_t len = CRSF_LINK_STATISTICS_LEN;
  tx_frame[0] = CRSF_ADDRESS_RADIO;
  tx_frame[1] = (4-2) + len;
  tx_frame[2] = CRSF_FRAME_ID_LINK_STATISTICS;
  memcpy(&(tx_frame[3]), payload, len);
  tx_frame[3 + len] = crc8(tx_frame);

  tx_available = 4 + len;
}


void tTxCrsf::SendLinkStatisticsTx(tCrsfLinkStatisticsTx* payload)
{
  constexpr uint8_t len = CRSF_LINK_STATISTICS_TX_LEN;
  tx_frame[0] = CRSF_ADDRESS_RADIO;
  tx_frame[1] = (4-2) + len;
  tx_frame[2] = CRSF_FRAME_ID_LINK_STATISTICS_TX;
  memcpy(&(tx_frame[3]), payload, len);
  tx_frame[3 + len] = crc8(tx_frame);

  tx_available = 4 + len;
}


void tTxCrsf::SendLinkStatisticsRx(tCrsfLinkStatisticsRx* payload)
{
  constexpr uint8_t len = CRSF_LINK_STATISTICS_RX_LEN;
  tx_frame[0] = CRSF_ADDRESS_RADIO;
  tx_frame[1] = (4-2) + len;
  tx_frame[2] = CRSF_FRAME_ID_LINK_STATISTICS_RX;
  memcpy(&(tx_frame[3]), payload, len);
  tx_frame[3 + len] = crc8(tx_frame);

  tx_available = 4 + len;
}


//-------------------------------------------------------
// convenience helper

void crsf_send_LinkStatistics(void)
{
tCrsfLinkStatistics lstats;

  lstats.uplink_rssi1 = txstats.GetRssi();
  lstats.uplink_rssi2 = -128;
  lstats.uplink_LQ = txstats.GetLQ();
  lstats.uplink_snr = stats.last_rx_snr;
  lstats.active_antenna = 0;
  lstats.mode = 1; // 50 Hz
  lstats.uplink_transmit_power = CRSF_POWER_0_mW;
  lstats.downlink_rssi = stats.received_rssi;
  lstats.downlink_LQ = stats.received_LQ;
  lstats.downlink_snr = 0;
  crsf.SendLinkStatistics(&lstats);
}


void crsf_send_LinkStatisticsTx(void)
{
tCrsfLinkStatisticsTx lstats;

  lstats.uplink_rssi = txstats.GetRssi(); // ignored by OpenTx
  lstats.uplink_rssi_percent = 12;
  lstats.uplink_LQ = txstats.GetLQ(); // ignored by OpenTx
  lstats.uplink_snr = stats.last_rx_snr; // ignored by OpenTx
  lstats.downlink_transmit_power = CRSF_POWER_0_mW;
  lstats.uplink_fps = 5;
  crsf.SendLinkStatisticsTx(&lstats);
}


void crsf_send_LinkStatisticsRx(void)
{
tCrsfLinkStatisticsRx lstats;

  lstats.downlink_rssi = stats.received_rssi; // ignored by OpenTx
  lstats.downlink_rssi_percent = 13;
  lstats.downlink_LQ = stats.received_LQ; // ignored by OpenTx
  lstats.downlink_snr = 0; // ignored by OpenTx
  lstats.uplink_transmit_power = CRSF_POWER_0_mW;
  crsf.SendLinkStatisticsRx(&lstats);
}


// CRSF:    172 .. 992 .. 1811, 11 bits
// rcData:  0 .. 1024 .. 2047, 11 bits
void fill_rcdata_from_crsf(tRcData* rc, uint8_t* frame)
{
tCrsfChannelBuffer buf;

  memcpy(buf.c, &(frame[3]), CRSF_CHANNELPACKET_SIZE);

  rc->ch[0] = (((int32_t)(buf.ch0) - 992) * 2047) / 1638 + 1024;
  rc->ch[1] = (((int32_t)(buf.ch1) - 992) * 2047) / 1638 + 1024;
  rc->ch[2] = (((int32_t)(buf.ch2) - 992) * 2047) / 1638 + 1024;
  rc->ch[3] = (((int32_t)(buf.ch3) - 992) * 2047) / 1638 + 1024;
  rc->ch[4] = (((int32_t)(buf.ch4) - 992) * 2047) / 1638 + 1024;
  rc->ch[5] = (((int32_t)(buf.ch5) - 992) * 2047) / 1638 + 1024;
  rc->ch[6] = (((int32_t)(buf.ch6) - 992) * 2047) / 1638 + 1024;
  rc->ch[7] = (((int32_t)(buf.ch7) - 992) * 2047) / 1638 + 1024;
  rc->ch[8] = (((int32_t)(buf.ch8) - 992) * 2047) / 1638 + 1024;
  rc->ch[9] = (((int32_t)(buf.ch9) - 992) * 2047) / 1638 + 1024;

  rc->ch[10] = (((int32_t)(buf.ch10) - 992) * 2047) / 1638 + 1024;
  rc->ch[11] = (((int32_t)(buf.ch11) - 992) * 2047) / 1638 + 1024;
  rc->ch[12] = (((int32_t)(buf.ch12) - 992) * 2047) / 1638 + 1024;
  rc->ch[13] = (((int32_t)(buf.ch13) - 992) * 2047) / 1638 + 1024;
  rc->ch[14] = (((int32_t)(buf.ch14) - 992) * 2047) / 1638 + 1024;
  rc->ch[15] = (((int32_t)(buf.ch15) - 992) * 2047) / 1638 + 1024;
}



#endif // if (SETUP_TX_CHANNELS_SOURCE == 3)

#endif // CRSF_INTERFACE_H








/*

void tTxCrsf::SpinOnce(void)
{
uint8_t c;

  if ((state != STATE_IDLE)) {
    uint16_t dt = tim_us() - tlast_us;
    if (dt > CRSF_TMO_US) state = STATE_IDLE;
  }

  switch (state) {
  case STATE_IDLE:
    if (!mb_rx_available()) break;
    tlast_us = tim_us();
    c = mb_getc();
    if (c == CRSF_ADDRESS_MODULE) {
      cnt = 0;
      frame[cnt++] = c;
      state = STATE_RECEIVE_CRSF_LEN;
    }
    break;
  case STATE_RECEIVE_CRSF_LEN:
    if (!mb_rx_available()) break;
    tlast_us = tim_us();
    c = mb_getc();
    frame[cnt++] = c;
    len = c;
    state = STATE_RECEIVE_CRSF_PAYLOAD;
    break;
  case STATE_RECEIVE_CRSF_PAYLOAD:
    if (!mb_rx_available()) break;
    tlast_us = tim_us();
    c = mb_getc();
    frame[cnt++] = c;
    if (cnt >= len + 1) {
      state = STATE_RECEIVE_CRSF_CRC;
    }
    break;
  case STATE_RECEIVE_CRSF_CRC:
    if (!mb_rx_available()) break;
    tlast_us = tim_us();
    c = mb_getc();
    frame[cnt++] = c;
    updated = true;
    state = STATE_IDLE;
    break;
  }
}

 */