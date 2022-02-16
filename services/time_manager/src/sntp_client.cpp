/*
 * Copyright (C) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstdio>
#include <unistd.h>
#include <sstream>
#include <ctime>
#include <sys/time.h>
#include <string>
#include <securec.h>
#include <iomanip>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <chrono>
#include "time_common.h"
#include "sntp_client.h"

namespace OHOS {
namespace MiscServices {
namespace {
    constexpr auto SECONDS_SINCE_FIRST_EPOCH = (2208988800UL); // Seconds from 1/1/1900 00.00 to 1/1/1970 00.00;
    constexpr uint64_t MICRO_TO_MILESECOND = 1000;
    constexpr uint64_t MILLISECOND_TO_SECOND = 1000;
    constexpr uint64_t FRACTION_TO_SECOND = 0x100000000;
    constexpr uint64_t UINT32_MASK = 0xFFFFFFFF;
    const int VERSION_MASK = 0x38;
    const int MODE_MASK = 0x7;
    constexpr int INVALID_RETURN = -1;
    constexpr int INDEX_ZERO = 0;
    constexpr int INDEX_ONE = 1;
    constexpr int INDEX_TWO = 2;
    constexpr int INDEX_THREE = 3;
    constexpr int INDEX_FOUR = 4;
    constexpr int INDEX_SIX = 6;
    constexpr unsigned char MODE_THREE = 3;
    constexpr unsigned char VERSION_THREE = 3;
    constexpr double TEN_TO_MINUS_SIX_POWER = 1.0e-6;
    constexpr double TEN_TO_SIX_POWER = 1.0e6;
    constexpr int NTP_PORT = 123;
    constexpr int NTP_MSG_OFFSET_ROOT_DELAY = 4;
    constexpr int NTP_MSG_OFFSET_ROOT_DISPERSION = 8;
    constexpr int NTP_MSG_OFFSET_REFERENCE_IDENTIFIER = 12;
    constexpr int REFERENCE_TIMESTAMP_OFFSET = 16;
    constexpr int ORIGINATE_TIMESTAMP_OFFSET = 24;
    constexpr int RECEIVE_TIMESTAMP_OFFSET = 32;
    constexpr int TRANSMIT_TIMESTAMP_OFFSET = 40;
    constexpr int NTP_PACKAGE_SIZE = 48;
    constexpr int SNTP_MSG_OFFSET_SIX = 6;
    constexpr int SNTP_MSG_OFFSET_THREE = 3;
    constexpr int SECOND_OF_DAY = 86400;
    constexpr int SECOND_OF_HOUR = 3600;
    constexpr int SECOND_OF_MINUTE = 60;
}
SNTPClient::SNTPClient() {}
SNTPClient::~SNTPClient() {}

bool SNTPClient::RequestTime(std::string host)
{
    int iResult;
    struct sockaddr_in RecvAddr;
    unsigned short Port = NTP_PORT;
    int BufLen = NTP_PACKAGE_SIZE;

    // Create a socket for sending data
    int SendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (SendSocket == 0) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "socket failed with error:  %{public}d", 0);
        return false;
    }

    struct hostent* hostV;
    if ((hostV = gethostbyname(host.c_str())) == nullptr) {
        // More descriptive error message
        TIME_HILOGE(TIME_MODULE_SERVICE, "Get host by name %{public}s but get nullptr.", host.c_str());
        return false;
    }

    errno_t ret = memset_s((char*)& RecvAddr, sizeof(RecvAddr), 0, sizeof(RecvAddr));
    if (ret != EOK) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "memcpy_s failed, err = %d\n", ret);
        return false;
    }
    RecvAddr.sin_family = AF_INET;
    errno_t ret1 = memcpy_s((char*)& RecvAddr.sin_addr.s_addr, hostV->h_length, (char*)hostV->h_addr, hostV->h_length);
    if (ret1 != EOK) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "memcpy_s failed, err = %d\n", ret1);
        return false;
    }
    RecvAddr.sin_port = htons(Port);
    if (connect(SendSocket, (struct sockaddr*) & RecvAddr, sizeof(RecvAddr)) < 0) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "Connect socket failed with host: %{public}s", host.c_str());
        return false;
    }

    // Create the NTP tx timestamp and fill the fields in the msg to be tx
    char SendBuf[NTP_PACKAGE_SIZE] = {0};
    CreateMessage(SendBuf);
    iResult = send(SendSocket, SendBuf, BufLen, 0);
    if (iResult == INVALID_RETURN) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "Send socket message failed. Host: %{public}s", host.c_str());
        close(SendSocket);
        return false;
    }

    char bufferRx[NTP_PACKAGE_SIZE] = { 0 };
    // Receive until the peer closes the connection
    iResult = recv(SendSocket, bufferRx, NTP_PACKAGE_SIZE, 0);
    if (iResult == INVALID_RETURN) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "Recieve socket message failed. Host: %{public}s", host.c_str());
        close(SendSocket);
        return false;
    }

    ReceivedMessage(bufferRx);
    return true;
}


void SNTPClient::SetClockOffset(int clockOffset)
{
    m_clockOffset = clockOffset;
}

int SNTPClient::GetClockOffset(void)
{
    return m_clockOffset;
}

uint64_t SNTPClient::GetNtpTimestamp64(int offset, char* buffer)
{
    const int _len = sizeof(uint64_t);
    char valueRx[_len];
    errno_t ret = memset_s(valueRx, sizeof(uint64_t), 0, sizeof(uint64_t));
    if (ret != EOK) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "memcpy_s failed, err = %d\n", ret);
        return false;
    }
    int numOfBit = sizeof(uint64_t) - 1;
    for (int loop = offset; loop < offset + _len; loop++) {
        valueRx[numOfBit] = buffer[loop];
        numOfBit--;
    }

    uint64_t milliseconds;
    errno_t ret1 = memcpy_s(&milliseconds, sizeof(uint64_t), valueRx, sizeof(uint64_t));
    if (ret1 != EOK) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "memcpy_s failed, err = %d\n", ret1);
        return false;
    }

    return milliseconds;
}

void SNTPClient::ConvertUnixToNtp(struct ntp_timestamp *ntpTs, struct timeval *unixTs)
{
    // 0x83AA7E80; the seconds from Jan 1, 1900 to Jan 1, 1970
    ntpTs->second = unixTs->tv_sec + SECONDS_SINCE_FIRST_EPOCH; // 0x83AA7E80;
    ntpTs->fraction =
        (uint32_t)((double)(unixTs->tv_usec + 1) * (double)(1LL << RECEIVE_TIMESTAMP_OFFSET) * TEN_TO_MINUS_SIX_POWER);
}

void SNTPClient::ConvertNtpToUnix(struct ntp_timestamp *ntpTs, struct timeval *unixTs)
{
    // 0x83AA7E80; the seconds from Jan 1, 1900 to Jan 1, 1970
    unixTs->tv_sec = ntpTs->second - SECONDS_SINCE_FIRST_EPOCH;
    unixTs->tv_usec =
        (uint32_t)((double)ntpTs->fraction * TEN_TO_SIX_POWER / (double)(1LL << RECEIVE_TIMESTAMP_OFFSET));
}

uint64_t SNTPClient::ConvertNtpToDate(uint64_t _ntpTs, struct date_structure *_outDataTs)
{
    uint32_t second = (uint32_t)((_ntpTs >> RECEIVE_TIMESTAMP_OFFSET) & UINT32_MASK);
    uint32_t fraction = (uint32_t)(_ntpTs & UINT32_MASK);

    struct timeval unix;
    struct ntp_timestamp ntpTs;
    ntpTs.second = second;
    ntpTs.fraction = fraction;

    ConvertNtpToUnix(&ntpTs, &unix);
    _outDataTs->hour = (unix.tv_sec % SECOND_OF_DAY) / SECOND_OF_HOUR;
    _outDataTs->minute = (unix.tv_sec % SECOND_OF_HOUR) / SECOND_OF_MINUTE;
    _outDataTs->second = (unix.tv_sec % SECOND_OF_MINUTE);
    _outDataTs->millisecond = unix.tv_usec;

    std::ostringstream _ss;
    _ss << std::internal
        << std::setfill('0')
        << std::setw(INDEX_TWO)
        << _outDataTs->hour
        << std::internal
        << std::setfill('0')
        << std::setw(INDEX_TWO)
        << _outDataTs->minute
        << std::internal
        << std::setfill('0')
        << std::setw(INDEX_TWO) << _outDataTs->second
        << std::internal
        << std::setfill('0')
        << std::setw(INDEX_SIX)
        << _outDataTs->millisecond;

    std::string _s = _ss.str();
    return (stoull(_s));
}

 /*
  * /// SNTP Timestamp Format (as described in RFC 2030)
  *                         1                   2                   3
  *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  *   |                           Seconds                             |
  *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  *   |                  Seconds Fraction (0-padded)                  |
  *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  */
uint64_t SNTPClient::ConvertNtpToStamp(uint64_t _ntpTs)
{
    uint32_t second = (uint32_t)((_ntpTs >> RECEIVE_TIMESTAMP_OFFSET) & UINT32_MASK);
    uint32_t fraction = (uint32_t)(_ntpTs & UINT32_MASK);
    if (second == 0 && fraction == 0) {
        return 0;
    }
    return ((second - SECONDS_SINCE_FIRST_EPOCH) * MILLISECOND_TO_SECOND) +
        ((fraction * MILLISECOND_TO_SECOND) / FRACTION_TO_SECOND);
}

void SNTPClient::CreateMessage(char* buffer)
{
    struct ntp_timestamp ntp;
    struct timeval unix;

    gettimeofday(&unix, NULL);
    // convert unix time to ntp time
    ConvertUnixToNtp(&ntp, &unix);
    ConvertNtpToUnix(&ntp, &unix);
    uint64_t _ntpTs = ntp.second;
    _ntpTs = (_ntpTs << RECEIVE_TIMESTAMP_OFFSET) | ntp.fraction;
    m_originateTimestamp = _ntpTs;

    SNTPMessage _sntpMsg;
    // Important, if you don't set the version/mode, the server will ignore you.
    _sntpMsg.clear();
    _sntpMsg._leapIndicator = 0;
    _sntpMsg._versionNumber = VERSION_THREE;
    _sntpMsg._mode = MODE_THREE;
    // optional (?)
    _sntpMsg._originateTimestamp = _ntpTs;
    char value[sizeof(uint64_t)];
    errno_t ret = memcpy_s(value, sizeof(uint64_t), &_sntpMsg._originateTimestamp, sizeof(uint64_t));
    if (ret != EOK) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "memcpy_s failed, err = %d\n", ret);
        return;
    }
    int numOfBit = sizeof(uint64_t) - 1;
    int ofssetEnd = ORIGINATE_TIMESTAMP_OFFSET + sizeof(uint64_t);
    for (int loop = ORIGINATE_TIMESTAMP_OFFSET; loop < ofssetEnd; loop++) {
        buffer[loop] = value[numOfBit];
        numOfBit--;
    }
    // create the 1-byte info in one go... the result should be 27 :)
    buffer[INDEX_ZERO] = (_sntpMsg._leapIndicator << SNTP_MSG_OFFSET_SIX) |
        (_sntpMsg._versionNumber << SNTP_MSG_OFFSET_THREE) | _sntpMsg._mode;
}

void SNTPClient::WriteTimeStamp(char* buffer, ntp_timestamp *ntp)
{
    uint64_t _ntpTs = ntp->second;
    _ntpTs = (_ntpTs << RECEIVE_TIMESTAMP_OFFSET) | ntp->fraction;
    m_originateTimestamp = _ntpTs;

    SNTPMessage _sntpMsg;
    // Important, if you don't set the version/mode, the server will ignore you.
    _sntpMsg.clear();
    _sntpMsg._leapIndicator = 0;
    _sntpMsg._versionNumber = VERSION_THREE;
    _sntpMsg._mode = MODE_THREE;
    _sntpMsg._originateTimestamp = _ntpTs;
    char value[sizeof(uint64_t)];
    errno_t ret = memcpy_s(value, sizeof(uint64_t), &_sntpMsg._originateTimestamp, sizeof(uint64_t));
    if (ret != EOK) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "memcpy_s failed, err = %d\n", ret);
        return;
    }
    int numOfBit = sizeof(uint64_t) - 1;
    int ofssetEnd = ORIGINATE_TIMESTAMP_OFFSET + sizeof(uint64_t);
    for (int loop = ORIGINATE_TIMESTAMP_OFFSET; loop < ofssetEnd; loop++) {
        buffer[loop] = value[numOfBit];
        numOfBit--;
    }
    // create the 1-byte info in one go... the result should be 27 :)
    buffer[INDEX_ZERO] = (_sntpMsg._leapIndicator << SNTP_MSG_OFFSET_SIX) |
        (_sntpMsg._versionNumber << SNTP_MSG_OFFSET_THREE) | _sntpMsg._mode;
}

void SNTPClient::ReceivedMessage(char* buffer)
{
    struct ntp_timestamp ntp;
    struct timeval unix;

    gettimeofday(&unix, NULL);
    ConvertUnixToNtp(&ntp, &unix);
    uint64_t _ntpTs = ntp.second;
    _ntpTs = (_ntpTs << RECEIVE_TIMESTAMP_OFFSET) | ntp.fraction;

    SNTPMessage _sntpMsg;
    _sntpMsg.clear();
    _sntpMsg._leapIndicator = buffer[INDEX_ZERO] >> SNTP_MSG_OFFSET_SIX;
    _sntpMsg._versionNumber = (buffer[INDEX_ZERO] & VERSION_MASK) >> SNTP_MSG_OFFSET_THREE;
    _sntpMsg._mode = (buffer[INDEX_ZERO] & MODE_MASK);
    _sntpMsg._stratum = buffer[INDEX_ONE];
    _sntpMsg._pollInterval = buffer[INDEX_TWO];
    _sntpMsg._precision = buffer[INDEX_THREE];
    _sntpMsg._rootDelay = GetNtpField32(NTP_MSG_OFFSET_ROOT_DELAY, buffer);
    _sntpMsg._rootDispersion = GetNtpField32(NTP_MSG_OFFSET_ROOT_DISPERSION, buffer);
    int _refId[INDEX_FOUR];
    GetReferenceId(NTP_MSG_OFFSET_REFERENCE_IDENTIFIER, buffer, _refId);
    _sntpMsg._referenceIdentifier[INDEX_ZERO] = _refId[INDEX_ZERO];
    _sntpMsg._referenceIdentifier[INDEX_ONE] = _refId[INDEX_ONE];
    _sntpMsg._referenceIdentifier[INDEX_TWO] = _refId[INDEX_TWO];
    _sntpMsg._referenceIdentifier[INDEX_THREE] = _refId[INDEX_THREE];
    _sntpMsg._referenceTimestamp = GetNtpTimestamp64(REFERENCE_TIMESTAMP_OFFSET, buffer);
    _sntpMsg._originateTimestamp = GetNtpTimestamp64(ORIGINATE_TIMESTAMP_OFFSET, buffer);
    _sntpMsg._receiveTimestamp = GetNtpTimestamp64(RECEIVE_TIMESTAMP_OFFSET, buffer);
    _sntpMsg._transmitTimestamp = GetNtpTimestamp64(TRANSMIT_TIMESTAMP_OFFSET, buffer);

    uint64_t _tempOriginate = m_originateTimestamp;
    if (_sntpMsg._originateTimestamp > 0) {
        _tempOriginate = _sntpMsg._originateTimestamp;
    }

    struct date_structure dataTs;
    uint64_t _originClient = ConvertNtpToDate(_tempOriginate, &dataTs);
    uint64_t _receiveServer = ConvertNtpToDate(_sntpMsg._receiveTimestamp, &dataTs);
    uint64_t _transmitServer = ConvertNtpToDate(_sntpMsg._transmitTimestamp, &dataTs);
    uint64_t _receiveClient = ConvertNtpToDate(_ntpTs, &dataTs);

    int _clockOffset = (((_receiveServer - _originClient) + (_transmitServer - _receiveClient)) / INDEX_TWO);
    _clockOffset = _clockOffset / MICRO_TO_MILESECOND;
    int _roundTripDelay = (_receiveClient - _originClient) - (_receiveServer - _transmitServer);
    _roundTripDelay = _roundTripDelay / MICRO_TO_MILESECOND;
    mRoundTripTime = _roundTripDelay;
    mNtpTime = ConvertNtpToStamp(_ntpTs) + _clockOffset;
    mNtpTimeReference = std::chrono::duration_cast<std::chrono::milliseconds>
        (std::chrono::steady_clock::now().time_since_epoch()).count();
    SetClockOffset(_clockOffset);
}

unsigned int SNTPClient::GetNtpField32(int offset, char* buffer)
{
    const int _len = sizeof(int);
    char valueRx[_len];
    errno_t ret = memset_s(valueRx, _len, 0, _len);
    if (ret != EOK) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "memcpy_s failed, err = %d\n", ret);
        return false;
    }
    int numOfBit = sizeof(int) - 1;
    for (int loop = offset; loop < offset + _len; loop++) {
        valueRx[numOfBit] = buffer[loop];
        numOfBit--;
    }

    unsigned int milliseconds;
    errno_t retValue = memcpy_s(&milliseconds, sizeof(int), valueRx, sizeof(int));
    if (retValue != EOK) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "memcpy_s failed, err = %d\n", retValue);
        milliseconds = 0;
        return milliseconds;
    }
    return milliseconds;
}

void SNTPClient::GetReferenceId(int offset, char* buffer, int* _outArray)
{
    const int _len = sizeof(int);
    int num = 0;
    for (int loop = offset; loop < offset + _len; loop++) {
        _outArray[num] = buffer[loop];
        num++;
    }
}

void SNTPClient::SNTPMessage::clear()
{
    errno_t ret = memset_s(this, sizeof(*this), 0, sizeof(*this));
    if (ret != EOK) {
        TIME_HILOGE(TIME_MODULE_SERVICE, "memcpy_s failed.");
    }
}

int64_t SNTPClient::getNtpTIme()
{
    return mNtpTime;
}

int64_t SNTPClient::getNtpTimeReference()
{
    return mNtpTimeReference;
}

int64_t SNTPClient::getRoundTripTime()
{
    return mRoundTripTime;
}
} // MiscServices
} // OHOS