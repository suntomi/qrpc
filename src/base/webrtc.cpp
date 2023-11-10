#include "base/defs.h"
#include "base/webrtc.h"

#include "common.hpp"
#include "MediaSoupErrors.hpp"
#include "DepLibSRTP.hpp"
#include "DepLibUV.hpp"
#include "DepLibWebRTC.hpp"
#include "DepOpenSSL.hpp"
#include "DepUsrSCTP.hpp"
#include "RTC/DtlsTransport.hpp"
#include "RTC/SrtpSession.hpp"

#include <mutex>

namespace base {
std::once_flag once_init, once_fin;

int WebRTCServer::Init() {
  std::call_once(once_init, GlobalInit);
  return QRPC_OK;
}

int WebRTCServer::Fin() {
  std::call_once(once_fin, GlobalFin);
  return QRPC_OK;
}

int WebRTCServer::GlobalInit() {
	try
	{
		// Initialize static stuff.
		DepOpenSSL::ClassInit();
		DepLibSRTP::ClassInit();
		DepUsrSCTP::ClassInit();
		DepLibWebRTC::ClassInit();
		Utils::Crypto::ClassInit();
		RTC::DtlsTransport::ClassInit();
		RTC::SrtpSession::ClassInit();

		return QRPC_OK;
	} catch (const MediaSoupError& error) {
		logger::die({{"ev","mediasoup setup failure"},{"reason",error.what()}});
    // no return
		return QRPC_EDEPS;
	}
}
void WebRTCServer::GlobalFin() {
	try
	{
    // Free static stuff.
		DepLibSRTP::ClassDestroy();
		Utils::Crypto::ClassDestroy();
		DepLibWebRTC::ClassDestroy();
		RTC::DtlsTransport::ClassDestroy();
		DepUsrSCTP::ClassDestroy();
		DepLibUV::ClassDestroy();
  }
  catch (const MediaSoupError& error) {
    logger::error({{"ev","mediasoup cleanup failure"},{"reason",error.what()}});
	}
}
} //namespace base