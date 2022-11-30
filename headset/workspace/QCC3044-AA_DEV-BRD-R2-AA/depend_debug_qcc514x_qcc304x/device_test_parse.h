/*
    Warning - this file was autogenerated by genparse
    DO NOT EDIT - any changes will be lost
*/

#ifndef DEVICE_TEST_PARSE_H
#define DEVICE_TEST_PARSE_H

#include <message_.h>

#if (defined __XAP__ || defined __KALIMBA__)
#include <source.h>
#endif

const uint8 *DeviceTestServiceParser_parseData(const uint8 *s, const uint8 *e, Task task);
void DeviceTestServiceParser_handleUnrecognised(const uint8 *data, uint16 length, Task task);

#if (defined __XAP__ || defined __KALIMBA__)
uint16 DeviceTestServiceParser_parseSource(Source rfcDataIncoming, Task task);
#endif

struct sequence
{
  const uint8 *data;
  uint16 length;
};

struct DeviceTestServiceCommand_HandleLed
{
  uint16 ledValue;
};
struct DeviceTestServiceCommand_HandleAuthResp
{
  struct sequence response;
};
struct DeviceTestServiceCommand_HandlePskeyGet
{
  uint16 pskey;
};
struct DeviceTestServiceCommand_HandlePskeySet
{
  uint16 pskey;
  struct sequence value;
};
struct DeviceTestServiceCommand_HandleAncEnable
{
  uint16 mode;
};
struct DeviceTestServiceCommand_HandleTouchSensor
{
  uint16 durationS;
  uint16 stopAfter;
};
struct DeviceTestServiceCommand_HandlePskeyClear
{
  uint16 pskey;
};
struct DeviceTestServiceCommand_HandleMibPskeyGet
{
  uint16 pskey;
};
struct DeviceTestServiceCommand_HandleAncGetPsKey
{
  uint16 instance;
  struct sequence pskey;
};
struct DeviceTestServiceCommand_HandleAncSetPsKey
{
  uint16 instance;
  struct sequence pskey;
  struct sequence value;
};
struct DeviceTestServiceCommand_HandleAudioPlayTone
{
  uint16 durationMs;
  uint16 speakerSelection;
  uint16 tone;
};
struct DeviceTestServiceCommand_HandleDtsEndTesting
{
  uint16 reboot;
};
struct DeviceTestServiceCommand_HandleAudioLoopbackStart
{
  uint16 micSelection;
  uint16 sampleRate;
  uint16 speakerSelection;
};
struct DeviceTestServiceCommand_HandleRfTestCarrier
{
  uint16 channel;
};
struct DeviceTestServiceCommand_HandlePioOutput
{
  uint16 onOff;
  uint16 pio;
};
struct DeviceTestServiceCommand_HandleProximitySensor
{
  uint16 durationS;
  uint16 stopAfter;
};
struct DeviceTestServiceCommand_HandleAncSetFineGain
{
  uint16 gainpath;
  uint16 gainvalue;
  uint16 mode;
};
struct DeviceTestServiceCommand_HandleDtsSetTestMode
{
  uint16 testmode;
};
struct DeviceTestServiceCommand_HandleRfTestCfgPower
{
  uint16 powerSetting;
};
struct DeviceTestServiceCommand_HandleHallEffectSensor
{
  uint16 durationS;
  uint16 stopAfter;
};
struct DeviceTestServiceCommand_HandleAncReadFineGain
{
  uint16 gainpath;
  uint16 mode;
};
struct DeviceTestServiceCommand_HandleRfTestLeRxStart
{
  uint16 lechannel;
};
struct DeviceTestServiceCommand_HandleRfTestLeTxStart
{
  uint16 lechannel;
  uint16 lelength;
  uint16 pattern;
};
struct DeviceTestServiceCommand_HandlePioInput
{
  uint16 pio;
};
struct DeviceTestServiceCommand_HandleAncWriteFineGain
{
  uint16 gainpath;
  uint16 gainvalue;
  uint16 mode;
};
struct DeviceTestServiceCommand_HandleRfTestCfgAddress
{
  struct sequence bdaddr;
  uint16 logicalAddr;
};
struct DeviceTestServiceCommand_HandleRfTestCfgChannel
{
  uint16 channel;
};
struct DeviceTestServiceCommand_HandleRfTestCfgStopPio
{
  uint16 pio;
};
struct DeviceTestServiceCommand_HandleRfTestCfgPacket
{
  uint16 length;
  struct sequence packetType;
  struct sequence payload;
};
struct DeviceTestServiceCommand_HandleRfTestCfgStopTime
{
  uint16 reboot;
  uint16 timeMs;
};
struct DeviceTestServiceCommand_HandleAccelerometer
{
  uint16 durationS;
  uint16 stopAfter;
};
struct DeviceTestServiceCommand_HandleAncSetFineGainDual
{
  uint16 gainpath;
  uint16 instance0gain;
  uint16 instance1gain;
  uint16 mode;
};
struct DeviceTestServiceCommand_HandleAncReadFineGainDual
{
  uint16 gainpath;
  uint16 mode;
};
struct DeviceTestServiceCommand_HandleAncWriteFineGainDual
{
  uint16 gainpath;
  uint16 instance0gain;
  uint16 instance1gain;
  uint16 mode;
};
void DeviceTestServiceCommand_HandleLed(Task , const struct DeviceTestServiceCommand_HandleLed *);
void DeviceTestServiceCommand_HandleAuthResp(Task , const struct DeviceTestServiceCommand_HandleAuthResp *);
void DeviceTestServiceCommand_HandlePskeyGet(Task , const struct DeviceTestServiceCommand_HandlePskeyGet *);
void DeviceTestServiceCommand_HandlePskeySet(Task , const struct DeviceTestServiceCommand_HandlePskeySet *);
void DeviceTestServiceCommand_HandleRssiRead(Task );
void DeviceTestServiceCommand_HandleAncEnable(Task , const struct DeviceTestServiceCommand_HandleAncEnable *);
void DeviceTestServiceCommand_HandleAuthStart(Task );
void DeviceTestServiceCommand_HandleTouchSensor(Task , const struct DeviceTestServiceCommand_HandleTouchSensor *);
void DeviceTestServiceCommand_HandleAncDisable(Task );
void DeviceTestServiceCommand_HandlePskeyClear(Task , const struct DeviceTestServiceCommand_HandlePskeyClear *);
void DeviceTestServiceCommand_HandleRfTestStop(Task );
void DeviceTestServiceCommand_HandleAuthDisable(Task );
void DeviceTestServiceCommand_HandleDtsTestModeQuery(Task );
void DeviceTestServiceCommand_HandleLocalBdaddr(Task );
void DeviceTestServiceCommand_HandleMibPskeyGet(Task , const struct DeviceTestServiceCommand_HandleMibPskeyGet *);
void DeviceTestServiceCommand_HandleTemperature(Task );
void DeviceTestServiceCommand_HandleBatteryLevel(Task );
void DeviceTestServiceCommand_HandleAudioLoopbackStop(Task );
void DeviceTestServiceCommand_HandleAncGetPsKey(Task , const struct DeviceTestServiceCommand_HandleAncGetPsKey *);
void DeviceTestServiceCommand_HandleAncSetPsKey(Task , const struct DeviceTestServiceCommand_HandleAncSetPsKey *);
void DeviceTestServiceCommand_HandleAudioPlayTone(Task , const struct DeviceTestServiceCommand_HandleAudioPlayTone *);
void DeviceTestServiceCommand_HandleDtsEndTesting(Task , const struct DeviceTestServiceCommand_HandleDtsEndTesting *);
void DeviceTestServiceCommand_HandleAudioLoopbackStart(Task , const struct DeviceTestServiceCommand_HandleAudioLoopbackStart *);
void DeviceTestServiceCommand_HandleRfTestCarrier(Task , const struct DeviceTestServiceCommand_HandleRfTestCarrier *);
void DeviceTestServiceCommand_HandleDeviceUnderTestMode(Task );
void DeviceTestServiceCommand_HandleRfTestTxStart(Task );
void DeviceTestServiceCommand_HandlePioAllInputs(Task );
void DeviceTestServiceCommand_HandlePioOutput(Task , const struct DeviceTestServiceCommand_HandlePioOutput *);
void DeviceTestServiceCommand_HandleProximitySensor(Task , const struct DeviceTestServiceCommand_HandleProximitySensor *);
void DeviceTestServiceCommand_HandleAncSetFineGain(Task , const struct DeviceTestServiceCommand_HandleAncSetFineGain *);
void DeviceTestServiceCommand_HandleDtsSetTestMode(Task , const struct DeviceTestServiceCommand_HandleDtsSetTestMode *);
void DeviceTestServiceCommand_HandleRfTestCfgPower(Task , const struct DeviceTestServiceCommand_HandleRfTestCfgPower *);
void DeviceTestServiceCommand_HandleHallEffectSensor(Task , const struct DeviceTestServiceCommand_HandleHallEffectSensor *);
void DeviceTestServiceCommand_HandleAncReadFineGain(Task , const struct DeviceTestServiceCommand_HandleAncReadFineGain *);
void DeviceTestServiceCommand_HandleRfTestLeRxStart(Task , const struct DeviceTestServiceCommand_HandleRfTestLeRxStart *);
void DeviceTestServiceCommand_HandleRfTestLeTxStart(Task , const struct DeviceTestServiceCommand_HandleRfTestLeTxStart *);
void DeviceTestServiceCommand_HandlePioInput(Task , const struct DeviceTestServiceCommand_HandlePioInput *);
void DeviceTestServiceCommand_HandleAncWriteFineGain(Task , const struct DeviceTestServiceCommand_HandleAncWriteFineGain *);
void DeviceTestServiceCommand_HandleRfTestCfgAddress(Task , const struct DeviceTestServiceCommand_HandleRfTestCfgAddress *);
void DeviceTestServiceCommand_HandleRfTestCfgChannel(Task , const struct DeviceTestServiceCommand_HandleRfTestCfgChannel *);
void DeviceTestServiceCommand_HandleRfTestCfgStopPio(Task , const struct DeviceTestServiceCommand_HandleRfTestCfgStopPio *);
void DeviceTestServiceCommand_HandleAudioPlayTestTone(Task );
void DeviceTestServiceCommand_HandleRfTestCfgPacket(Task , const struct DeviceTestServiceCommand_HandleRfTestCfgPacket *);
void DeviceTestServiceCommand_HandleRfTestCfgStopTime(Task , const struct DeviceTestServiceCommand_HandleRfTestCfgStopTime *);
void DeviceTestServiceCommand_HandleAccelerometer(Task , const struct DeviceTestServiceCommand_HandleAccelerometer *);
void DeviceTestServiceCommand_HandleAncSetFineGainDual(Task , const struct DeviceTestServiceCommand_HandleAncSetFineGainDual *);
void DeviceTestServiceCommand_HandleAncReadFineGainDual(Task , const struct DeviceTestServiceCommand_HandleAncReadFineGainDual *);
void DeviceTestServiceCommand_HandleAncWriteFineGainDual(Task , const struct DeviceTestServiceCommand_HandleAncWriteFineGainDual *);

#endif