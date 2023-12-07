#define MS_CLASS "DepUsrSCTP"
// #define MS_LOG_DEV_LEVEL 3

#include "base/stream.h"
#include "base/alarm.h"
#include "base/webrtc/sctp.h"
#include "base/logger.h"
#include <usrsctp.h>
#include <cstdio> // std::vsnprintf()
#include <mutex>

namespace base {
  /* Static. */

  // static constexpr size_t CheckerInterval{ 10u }; // In ms.
  static std::mutex GlobalSyncMutex;
  static size_t GlobalInstances{ 0u };

  /* Static methods for usrsctp global callbacks. */

  inline static int onSendSctpData(void* addr, void* data, size_t len, uint8_t /*tos*/, uint8_t /*setDf*/)
  {
    auto* sctpAssociation = DepUsrSCTP::RetrieveSctpAssociation(reinterpret_cast<uintptr_t>(addr));
    if (!sctpAssociation) {
      logger::fatal({{"proto","sctp"},{"ev","no SctpAssociation found"}});
      return -1;
    }
    sctpAssociation->OnUsrSctpSendSctpData(data, len);
    // NOTE: Must not free data, usrsctp lib does it.
    return 0;
  }

  // Static method for printing usrsctp debug.
  inline static void sctpDebug(const char* format, ...)
  {
    char buffer[10000];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buffer, sizeof(buffer), format, ap);
    // Remove the artificial carriage return set by usrsctp.
    buffer[std::strlen(buffer) - 1] = '\0';
    logger::debug({{"proto","sctp"},{"buffer",str::HexDump(
      reinterpret_cast<const uint8_t *>(buffer), std::strlen(buffer)
    )}});
    va_end(ap);
  }

  /* Static variables. */

  uint64_t DepUsrSCTP::numSctpAssociations{ 0u };
  uintptr_t DepUsrSCTP::nextSctpAssociationId{ 0u };
  std::unordered_map<uintptr_t, SctpAssociation*> DepUsrSCTP::mapIdSctpAssociation;
  DepUsrSCTP::Timer DepUsrSCTP::timer = DepUsrSCTP::Timer();

  /* Static methods. */

  void DepUsrSCTP::ClassInit(AlarmProcessor &a)
  {
    TRACK();

    std::lock_guard<std::mutex> lock(GlobalSyncMutex);

    if (GlobalInstances == 0)
    {
      usrsctp_init_nothreads(0, onSendSctpData, sctpDebug);

      // Disable explicit congestion notifications (ecn).
      usrsctp_sysctl_set_sctp_ecn_enable(0);

  #ifdef SCTP_DEBUG
      usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
  #endif

      // start timer
      if (a.Set(timer, qrpc_time_now()) < 0) {
        logger::die({{"proto","sctp"},{"ev","fail to start sctp timer"}});
      }
    }

    ++GlobalInstances;
  }

  void DepUsrSCTP::ClassDestroy()
  {
    TRACK();

    std::lock_guard<std::mutex> lock(GlobalSyncMutex);
    --GlobalInstances;

    if (GlobalInstances == 0)
    {
      usrsctp_finish();

      numSctpAssociations   = 0u;
      nextSctpAssociationId = 0u;

      DepUsrSCTP::mapIdSctpAssociation.clear();
    }
  }

  // these indirect reference of SctpAssociation* instead of direct pointer into usrsctp stack, is
  // introduced to avoid the following vulnerability issue:
  // https://github.com/versatica/mediasoup/commit/e5722654f002ca623e28b376d25a590d5fa19824
  // https://github.com/versatica/mediasoup/pull/439  
  // > usrsctp is actually putting the address of that pointer in SCTP messages (as part of the cookie)
  uintptr_t DepUsrSCTP::GetNextSctpAssociationId()
  {
    TRACK();

    std::lock_guard<std::mutex> lock(GlobalSyncMutex);

    // NOTE: usrsctp_connect() fails with a value of 0.
    if (DepUsrSCTP::nextSctpAssociationId == 0u)
      ++DepUsrSCTP::nextSctpAssociationId;

    // In case we've wrapped around and need to find an empty spot from a removed
    // SctpAssociation. Assumes we'll never be full.
    while (DepUsrSCTP::mapIdSctpAssociation.find(DepUsrSCTP::nextSctpAssociationId) !=
          DepUsrSCTP::mapIdSctpAssociation.end())
    {
      ++DepUsrSCTP::nextSctpAssociationId;

      if (DepUsrSCTP::nextSctpAssociationId == 0u)
        ++DepUsrSCTP::nextSctpAssociationId;
    }

    return DepUsrSCTP::nextSctpAssociationId++;
  }

  void DepUsrSCTP::RegisterSctpAssociation(SctpAssociation* sctpAssociation)
  {
    TRACK();

    std::lock_guard<std::mutex> lock(GlobalSyncMutex);

    auto it = DepUsrSCTP::mapIdSctpAssociation.find(sctpAssociation->id);

    MASSERT(
      it == DepUsrSCTP::mapIdSctpAssociation.end(),
      "the id of the SctpAssociation is already in the map");

    DepUsrSCTP::mapIdSctpAssociation[sctpAssociation->id] = sctpAssociation;
  }

  void DepUsrSCTP::DeregisterSctpAssociation(SctpAssociation* sctpAssociation)
  {
    TRACK();

    std::lock_guard<std::mutex> lock(GlobalSyncMutex);

    auto found = DepUsrSCTP::mapIdSctpAssociation.erase(sctpAssociation->id);

    MASSERT(found > 0, "SctpAssociation not found");
  }

  SctpAssociation* DepUsrSCTP::RetrieveSctpAssociation(uintptr_t id)
  {
    TRACK();

    std::lock_guard<std::mutex> lock(GlobalSyncMutex);

    auto it = DepUsrSCTP::mapIdSctpAssociation.find(id);

    if (it == DepUsrSCTP::mapIdSctpAssociation.end())
      return nullptr;

    return it->second;
  }

  qrpc_time_t DepUsrSCTP::Timer::operator()()
  {
    static qrpc_time_t interval = qrpc_time_msec(10);
    // TRACK();
    auto now            = qrpc_time_now();
    auto nowMs          = qrpc_time_to_msec(now);
    const int elapsedMs = lastCalledAtMs != 0 ? static_cast<int>(nowMs - lastCalledAtMs) : 0;
    usrsctp_handle_timers(elapsedMs);
    lastCalledAtMs = nowMs;
    return now + interval;
  }
} // namespace base