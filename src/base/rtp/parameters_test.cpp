#include "gtest/gtest.h"
#include "base/rtp/parameters.h" // Class under test
#include "base/webrtc/sdp.h"     // For webrtc::SdpParse (assuming this is the project's SDP parser)
#include "base/json.hpp"         // Assuming nlohmann/json, adjust if different
#include "base/string.h"         // For str::Format, str::Split etc.

// If 远算子.RandomUint32 needs a stub for tests:
// This is a potential stub. Actual implementation depends on how the build system handles test-specific code.
// It's also possible that the actual function is part of a library that can be linked,
// or that its behavior (even if truly random) doesn't affect these specific tests.
/*
namespace 远算子 { // Or the actual namespace
#ifdef FOR_TESTING_STUB_RANDOM // Define this in your test build if this stub is used
  uint32_t RandomUint32(uint32_t min, uint32_t /*max*) {
    // Simple predictable value for testing to ensure ssrc_seed is somewhat consistent
    // if any test were to depend on generated SSRCs, though these tests do not.
    static uint32_t call_count = 0;
    return min + (call_count++ % (10000-1000+1)); // Cycle through a small range
  }
#else
  // If not stubbing, assume the real function is available via linking.
  // If it's in a header, it would be included by parameters.cpp.
  // If it's in a .cpp file compiled separately, it needs to be linked.
  // For now, we proceed as if it's linkable or its randomness doesn't break tests.
#endif
}
*/


// Helper function to convert SDP string to the JSON format Parameters::Parse expects.
// This function simulates obtaining a media section JSON from a full SDP.
json SdpToMediaSectionJson(const std::string& sdp_m_line_and_attributes) {
    // Construct a minimal valid SDP for parsing.
    // webrtc::SdpParse expects a complete SDP.
    std::string full_sdp = "v=0\r\n"
                           "o=- 0 0 IN IP4 127.0.0.1\r\n"
                           "s=-\r\n"
                           "t=0 0\r\n"
                           // Standard required session attributes
                           "a=msid-semantic: WMS\r\n" 
                           + sdp_m_line_and_attributes;
    
    json sdp_json_output; // This will contain the full parsed SDP.
    std::string error_reason;
    // The actual webrtc::SdpParse might return a more complex JSON structure.
    // We are interested in the JSON object for the *first media section*.
    // The `true` for `first_media_section_only` in the original example might be a custom behavior.
    // Standard SDP parsers usually give a list of media sections.
    // Let's assume webrtc::SdpParse populates sdp_json_output and we extract the first media section.

    bool parsed = webrtc::SdpParse(full_sdp, sdp_json_output, error_reason); 
    EXPECT_TRUE(parsed) << "SDP Parsing failed: " << error_reason << "\nSDP provided:\n" << full_sdp;
    
    // Assuming sdp_json_output has a "media" array.
    if (!sdp_json_output.contains("media") || !sdp_json_output["media"].is_array() || sdp_json_output["media"].empty()) {
        EXPECT_TRUE(false) << "Parsed SDP does not contain a 'media' array or it's empty.\nParsed JSON:\n" << sdp_json_output.dump(2);
        return json::object(); // Return empty object on failure to find media section
    }
    // Return the first media section.
    return sdp_json_output["media"][0];
}


class ParametersParseTest : public ::testing::Test {
protected:
    base::rtp::Parameters params;
    base::rtp::Capability cap;
    std::string answer_str; 
    std::map<std::string, std::string> rid_scalability_mode_map_empty;

    static void SetUpTestSuite() {
        // This is called once before all tests in the test suite.
        // Useful for one-time setup like Parameter's static maps.
        base::rtp::Parameters::SetupHeaderExtensionMap();
    }

    void SetUp() override {
      params = base::rtp::Parameters();
      cap = base::rtp::Capability();
      answer_str.clear();
      rid_scalability_mode_map_empty.clear();
    }

    const RTC::RtpEncodingParameters* FindEncodingByRid(const std::string& rid) const {
        for (const auto& enc : params.encodings) {
            if (enc.rid == rid) {
                return &enc;
            }
        }
        return nullptr;
    }
};

class ParametersAnswerTest : public ::testing::Test {
protected:
    base::rtp::Parameters params;

    static void SetUpTestSuite() {
        base::rtp::Parameters::SetupHeaderExtensionMap();
    }

    void SetUp() override {
        params = base::rtp::Parameters();
        params.kind = base::rtp::Parameters::MediaKind::VIDEO; 
        // Default network info to avoid issues if Answer() expects it
        params.network.port = 9; 
    }

    void ExpectSdpContains(const std::string& sdp, const std::string& substring) {
        EXPECT_NE(sdp.find(substring), std::string::npos)
            << "SDP should contain: \"" << substring << "\"\nActual SDP substring:\n"
            << sdp.substr(std::max(0, (int)sdp.find(substring) - 30), substring.length() + 60) 
            << "\nFull SDP:\n" << sdp;
    }

    void ExpectSdpDoesNotContain(const std::string& sdp, const std::string& substring) {
        EXPECT_EQ(sdp.find(substring), std::string::npos)
            << "SDP should NOT contain: \"" << substring << "\"\nActual SDP:\n" << sdp;
    }
};

// --- Parameters::Parse Tests ---

TEST_F(ParametersParseTest, VP9WithScalabilityStructure) {
    std::string sdp_media = "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                            "a=rtcp-mux\r\n"
                            "a=rtpmap:96 VP9/90000\r\n"
                            "a=fmtp:96 profile-id=0; scalability-structure=S2T3\r\n"
                            "a=mid:video\r\n"; // Added a=mid for typical media section
    json media_section = SdpToMediaSectionJson(sdp_media);
    ASSERT_FALSE(media_section.empty()) << "Media section parsing returned empty JSON.";

    // Basic check for payload type in rtp array
    ASSERT_TRUE(media_section.contains("rtp") && media_section["rtp"].is_array() && !media_section["rtp"].empty());
    ASSERT_TRUE(media_section["rtp"][0].contains("payload") && media_section["rtp"][0]["payload"].is_number());
    
    bool success = params.Parse(media_section, cap, answer_str, rid_scalability_mode_map_empty);
    ASSERT_TRUE(success) << "Parse failed: " << answer_str;
    ASSERT_FALSE(params.encodings.empty());
    EXPECT_EQ(params.encodings[0].scalabilityMode, "L2T3");
}

TEST_F(ParametersParseTest, VP9WithFmtpScalabilityMode) {
    std::string sdp_media = "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                            "a=rtcp-mux\r\n"
                            "a=rtpmap:96 VP9/90000\r\n"
                            "a=fmtp:96 profile-id=0; scalabilityMode=L1T2_KEY\r\n"
                            "a=mid:video\r\n";
    json media_section = SdpToMediaSectionJson(sdp_media);
    ASSERT_FALSE(media_section.empty());

    bool success = params.Parse(media_section, cap, answer_str, rid_scalability_mode_map_empty);
    ASSERT_TRUE(success) << "Parse failed: " << answer_str;
    ASSERT_FALSE(params.encodings.empty());
    EXPECT_EQ(params.encodings[0].scalabilityMode, "L1T2_KEY");
}

TEST_F(ParametersParseTest, AV1WithFmtpScalabilityMode) {
    std::string sdp_media = "m=video 9 UDP/TLS/RTP/SAVPF 97\r\n"
                            "a=rtcp-mux\r\n"
                            "a=rtpmap:97 AV1/90000\r\n"
                            "a=fmtp:97 scalabilityMode=L3T1\r\n"
                            "a=mid:video\r\n";
    json media_section = SdpToMediaSectionJson(sdp_media);
    ASSERT_FALSE(media_section.empty());
    
    bool success = params.Parse(media_section, cap, answer_str, rid_scalability_mode_map_empty);
    ASSERT_TRUE(success) << "Parse failed: " << answer_str;
    ASSERT_FALSE(params.encodings.empty());
    EXPECT_EQ(params.encodings[0].scalabilityMode, "L3T1");
}

TEST_F(ParametersParseTest, AV1WithDirectLsTtInFmtp) {
    std::string sdp_media = "m=video 9 UDP/TLS/RTP/SAVPF 97\r\n"
                            "a=rtcp-mux\r\n"
                            "a=rtpmap:97 AV1/90000\r\n"
                            "a=fmtp:97 L1T2;some-other-param=value\r\n"
                            "a=mid:video\r\n";
    json media_section = SdpToMediaSectionJson(sdp_media);
    ASSERT_FALSE(media_section.empty());

    bool success = params.Parse(media_section, cap, answer_str, rid_scalability_mode_map_empty);
    ASSERT_TRUE(success) << "Parse failed: " << answer_str;
    ASSERT_FALSE(params.encodings.empty());
    EXPECT_EQ(params.encodings[0].scalabilityMode, "L1T2");
}

TEST_F(ParametersParseTest, NoSvcInformation) {
    std::string sdp_media = "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                            "a=rtcp-mux\r\n"
                            "a=rtpmap:96 VP9/90000\r\n"
                            "a=fmtp:96 profile-id=0\r\n"
                            "a=mid:video\r\n";
    json media_section = SdpToMediaSectionJson(sdp_media);
    ASSERT_FALSE(media_section.empty());

    bool success = params.Parse(media_section, cap, answer_str, rid_scalability_mode_map_empty);
    ASSERT_TRUE(success) << "Parse failed: " << answer_str;
    ASSERT_FALSE(params.encodings.empty()); // Should still have a default encoding
    EXPECT_TRUE(params.encodings[0].scalabilityMode.empty());
}

TEST_F(ParametersParseTest, VP9WithRidsAndRidScalabilityModeMap) {
    std::string sdp_media = "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                            "a=rtcp-mux\r\n"
                            "a=rtpmap:96 VP9/90000\r\n"
                            "a=fmtp:96 profile-id=0\r\n" 
                            "a=simulcast:send 0;1\r\n"
                            "a=rid:0 send\r\n" // rid_scalability_mode_map should take precedence
                            "a=rid:1 send\r\n"
                            "a=mid:video\r\n";
    json media_section = SdpToMediaSectionJson(sdp_media);
    ASSERT_FALSE(media_section.empty());

    std::map<std::string, std::string> rid_map = {{"0", "L1T1"}, {"1", "L1T2"}};
    
    bool success = params.Parse(media_section, cap, answer_str, rid_map);
    ASSERT_TRUE(success) << "Parse failed: " << answer_str;
    // The Parse logic creates encodings based on simulcast RIDs if present.
    ASSERT_EQ(params.encodings.size(), 2); 

    const RTC::RtpEncodingParameters* enc0 = FindEncodingByRid("0");
    ASSERT_NE(enc0, nullptr);
    EXPECT_EQ(enc0->scalabilityMode, "L1T1");

    const RTC::RtpEncodingParameters* enc1 = FindEncodingByRid("1");
    ASSERT_NE(enc1, nullptr);
    EXPECT_EQ(enc1->scalabilityMode, "L1T2");
}


// --- Parameters::Answer Tests ---

TEST_F(ParametersAnswerTest, VP9OfferWithSvc) {
    params.codecs.emplace_back();
    auto& vp9_codec = params.codecs.back();
    vp9_codec.mimeType.SetMimeType("video/VP9");
    vp9_codec.payloadType = 96;
    vp9_codec.clockRate = 90000;

    params.encodings.emplace_back();
    auto& encoding = params.encodings.back();
    encoding.codecPayloadType = 96;
    encoding.hasCodecPayloadType = true;
    encoding.scalabilityMode = "L2T3";

    std::string sdp = params.Answer("");
    ExpectSdpContains(sdp, "a=rtpmap:96 VP9/90000\r\n");
    ExpectSdpContains(sdp, "a=fmtp:96 scalabilityMode=L2T3;x-google-start-bitrate=1000\r\n");
}

TEST_F(ParametersAnswerTest, AV1OfferWithSvc) {
    params.codecs.emplace_back();
    auto& av1_codec = params.codecs.back();
    av1_codec.mimeType.SetMimeType("video/AV1");
    av1_codec.payloadType = 97;
    av1_codec.clockRate = 90000;

    params.encodings.emplace_back();
    auto& encoding = params.encodings.back();
    encoding.codecPayloadType = 97;
    encoding.hasCodecPayloadType = true;
    encoding.scalabilityMode = "L1T2_KEY";

    std::string sdp = params.Answer("");
    ExpectSdpContains(sdp, "a=rtpmap:97 AV1/90000\r\n");
    ExpectSdpContains(sdp, "a=fmtp:97 scalabilityMode=L1T2_KEY;x-google-start-bitrate=1000\r\n");
}

TEST_F(ParametersAnswerTest, VP9OfferWithSimulcastAndPerRidSvc) {
    params.codecs.emplace_back();
    auto& vp9_codec = params.codecs.back();
    vp9_codec.mimeType.SetMimeType("video/VP9");
    vp9_codec.payloadType = 96;
    vp9_codec.clockRate = 90000;
    vp9_codec.parameters.Add("profile-id", 0);


    params.simulcast.send_rids = "foo;bar"; 

    RTC::RtpEncodingParameters enc_foo;
    enc_foo.rid = "foo";
    enc_foo.codecPayloadType = 96; // Matches vp9_codec PT
    enc_foo.hasCodecPayloadType = true;
    enc_foo.scalabilityMode = "L1T1";
    params.encodings.push_back(enc_foo);

    RTC::RtpEncodingParameters enc_bar;
    enc_bar.rid = "bar";
    enc_bar.codecPayloadType = 96; // Matches vp9_codec PT
    enc_bar.hasCodecPayloadType = true;
    enc_bar.scalabilityMode = "L1T2";
    params.encodings.push_back(enc_bar);
    
    std::string sdp = params.Answer("");

    ExpectSdpContains(sdp, "a=rtpmap:96 VP9/90000\r\n");
    // The first encoding (enc_foo) matches the payload type, so its SM should be in fmtp.
    ExpectSdpContains(sdp, "a=fmtp:96 profile-id=0;scalabilityMode=L1T1;x-google-start-bitrate=1000\r\n");
    ExpectSdpContains(sdp, "a=simulcast:send foo;bar\r\n"); // Ensure newline after simulcast line
    ExpectSdpContains(sdp, "a=rid:foo send scalabilityMode=L1T1\r\n");
    ExpectSdpContains(sdp, "a=rid:bar send scalabilityMode=L1T2\r\n");
}


TEST_F(ParametersAnswerTest, OfferWithNoSvc) {
    params.codecs.emplace_back();
    auto& vp9_codec = params.codecs.back();
    vp9_codec.mimeType.SetMimeType("video/VP9");
    vp9_codec.payloadType = 96;
    vp9_codec.clockRate = 90000;
    vp9_codec.parameters.Add("profile-id", 0);

    params.encodings.emplace_back();
    auto& encoding = params.encodings.back();
    encoding.codecPayloadType = 96; // Matches vp9_codec PT
    encoding.hasCodecPayloadType = true;
    // encoding.scalabilityMode remains empty

    std::string sdp = params.Answer("");
    ExpectSdpContains(sdp, "a=rtpmap:96 VP9/90000\r\n");
    ExpectSdpContains(sdp, "a=fmtp:96 profile-id=0;x-google-start-bitrate=1000\r\n");
    ExpectSdpDoesNotContain(sdp, "scalabilityMode=");
    ExpectSdpDoesNotContain(sdp, "scalability-structure=");
}

TEST_F(ParametersAnswerTest, PreservesXGoogleStartBitrateWithSvc) {
    params.codecs.emplace_back();
    auto& vp9_codec = params.codecs.back();
    vp9_codec.mimeType.SetMimeType("video/VP9");
    vp9_codec.payloadType = 96;
    vp9_codec.clockRate = 90000;
    vp9_codec.parameters.Add("some-other-param", "value"); 

    params.encodings.emplace_back();
    auto& encoding = params.encodings.back();
    encoding.codecPayloadType = 96; // Matches vp9_codec PT
    encoding.hasCodecPayloadType = true;
    encoding.scalabilityMode = "L2T3";

    std::string sdp = params.Answer("");
    ExpectSdpContains(sdp, "a=rtpmap:96 VP9/90000\r\n");
    ExpectSdpContains(sdp, "a=fmtp:96 some-other-param=value;scalabilityMode=L2T3;x-google-start-bitrate=1000\r\n");
}

// Example of testing recv direction for simulcast RIDs
TEST_F(ParametersAnswerTest, VP9OfferWithSimulcastRecvAndPerRidSvc) {
    params.codecs.emplace_back();
    auto& vp9_codec = params.codecs.back();
    vp9_codec.mimeType.SetMimeType("video/VP9");
    vp9_codec.payloadType = 96;
    vp9_codec.clockRate = 90000;

    params.simulcast.recv_rids = "alpha;beta"; 

    RTC::RtpEncodingParameters enc_alpha;
    enc_alpha.rid = "alpha";
    enc_alpha.codecPayloadType = 96;
    enc_alpha.hasCodecPayloadType = true;
    enc_alpha.scalabilityMode = "L2T1";
    params.encodings.push_back(enc_alpha);

    RTC::RtpEncodingParameters enc_beta;
    enc_beta.rid = "beta";
    enc_beta.codecPayloadType = 96;
    enc_beta.hasCodecPayloadType = true;
    enc_beta.scalabilityMode = "L2T2";
    params.encodings.push_back(enc_beta);
    
    std::string sdp = params.Answer("");

    ExpectSdpContains(sdp, "a=rtpmap:96 VP9/90000\r\n");
    ExpectSdpContains(sdp, "a=fmtp:96 scalabilityMode=L2T1;x-google-start-bitrate=1000\r\n"); // enc_alpha's SM
    ExpectSdpContains(sdp, "a=simulcast:send alpha;beta\r\n"); // recv_rids in params become send rids in offer
    ExpectSdpContains(sdp, "a=rid:alpha send scalabilityMode=L2T1\r\n");
    ExpectSdpContains(sdp, "a=rid:beta send scalabilityMode=L2T2\r\n");
}

```
