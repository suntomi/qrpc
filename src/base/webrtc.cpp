// #include "common.hpp"
// #include "DepLibSRTP.hpp"
// #include "DepLibUV.hpp"
// #include "DepLibWebRTC.hpp"
// #include "DepOpenSSL.hpp"
// #include "DepUsrSCTP.hpp"

// namespace base {
// void WebRTCServer::Init() {
// 	try
// 	{
// 		// Initialize static stuff.
// 		DepOpenSSL::ClassInit();
// 		DepLibSRTP::ClassInit();
// 		DepUsrSCTP::ClassInit();
// 		DepLibWebRTC::ClassInit();
// 		Utils::Crypto::ClassInit();
// 		RTC::DtlsTransport::ClassInit();
// 		RTC::SrtpSession::ClassInit();

// #ifdef MS_EXECUTABLE
// 		// Ignore some signals.
// 		IgnoreSignals();
// #endif

// 		// Run the Worker.
// 		Worker worker(channel.get(), payloadChannel.get());

// 		// Free static stuff.
// 		DepLibSRTP::ClassDestroy();
// 		Utils::Crypto::ClassDestroy();
// 		DepLibWebRTC::ClassDestroy();
// 		RTC::DtlsTransport::ClassDestroy();
// 		DepUsrSCTP::ClassDestroy();
// 		DepLibUV::ClassDestroy();

// #ifdef MS_EXECUTABLE
// 		// Wait a bit so pending messages to stdout/Channel arrive to the Node
// 		// process.
// 		uv_sleep(200);
// #endif

// 		return 0;
// 	}
// 	catch (const MediaSoupError& error)
// 	{
// 		MS_ERROR_STD("failure exit: %s", error.what());

// 		// 40 is a custom exit code to notify "unknown error" to the Node library.
// 		return 40;
// 	}
// }  
// } //namespace base