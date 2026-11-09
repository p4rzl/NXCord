#include "voice_connection.h"
#ifdef SLEEPY_VOICE_ENABLED
#include <chrono>
#include <sodium.h>
#include <thread>
#include "client.h"

namespace SleepyDiscord {
	VoiceConnection::VoiceConnection(BaseDiscordClient* client, VoiceContext& _context) :
		origin(client), context(_context), sSRC(0), port(0), nextTime(0),
#if !defined(NONEXISTENT_OPUS)
		encoder(nullptr),
#endif
		secretKey()
	{
		dave = NXCord::DaveSession::create(context.channelID.number(), origin->getID().string(),
			[this](bool binary, const std::string& message) {
				if (binary) origin->sendBinary(message, connection);
				else origin->send(message, connection);
			});
	}

	void VoiceConnection::disconnect() {
		videoEnabled = false;
		stopSpeaking();
		{
			std::lock_guard<std::mutex> lock(rtpSendLock);
			state = static_cast<State>(state & ~State::AUDIO_ENABLED);
		}
		UDP.unsetReceiveHandler();
		// Join the UDP worker before destroying its crypto/decoder state.
		UDP.disconnect();
		std::string update;
		/*The number 120 comes from the number of letters in this string + 1:
		{"op":4,"d":{"guild_id":"18446744073709551615","channel_id":null,"self_mute":false,"self_deaf":false,"self_video":false}}
		*/
		update.reserve(124);
		update +=
			"{"
				"\"op\":4,"
				"\"d\":{"
					"\"guild_id\":\""; update += context.serverID; update += "\","
					"\"channel_id\":null,"
					"\"self_mute\":false,"
					"\"self_deaf\":false,"
					"\"self_video\":false"
				"}"
			"}";
		origin->send(update, origin->connection);

		if (state & State::CONNECTED)
			origin->disconnect(1000, "", connection);
		if (heart.isValid())
			heart.stop(); //Kill
		speechTimer.stop();
		listenTimer.stop();
		//deal with raw pointers
		//Sorry about this c code, we are dealing with c libraries
		if (encoder != nullptr) {
			opus_encoder_destroy(encoder);
			encoder = nullptr;
		}
		{
			std::lock_guard<std::mutex> lock(audioOutputLock);
			audioOutput.reset();
		}
		audioSource.reset();
		{
			std::lock_guard<std::mutex> lock(decodersLock);
			decoders.clear();
		}
		dave.reset();
		state = static_cast<State>(state & ~State::CONNECTED);
	}

	void VoiceConnection::initialize() {
		if (state == NOT_CONNECTED)
			return;

		std::string resume;
		/*The number 96 comes from the number of letters in this string + 1:
		{"op":7,"d":{"server_id":"18446744073709551615","session_id":"","token":"","seq_ack":65535}}
		*/
		resume.reserve(96 + context.sessionID.length() + context.token.length());
		resume +=
			"{"
				"\"op\":7," //RESUME
				"\"d\":{"
					"\"server_id\":\"" ; resume += context.serverID ; resume += "\","
					"\"session_id\":\""; resume += context.sessionID; resume += "\","
					"\"token\":\""     ; resume += context.token    ; resume += "\","
					"\"seq_ack\":"     ; resume += std::to_string(lastVoiceSequence); resume +=
				"}"
			"}";
		origin->send(resume, connection);
	}

	void VoiceConnection::processMessage(const std::string &message) {
	        // Voice payloads can contain session credentials; never print them.
		//json::Values values = json::getValues(message.c_str(),
		//	{ "op", "d" });
		rapidjson::Document values;
		values.Parse(message.c_str(), message.length());
		if (encryptionFailed || values.HasParseError() || !values.IsObject() || !values.HasMember("op") || !values["op"].IsInt())
			return;
		if (values.HasMember("seq") && values["seq"].IsUint() && values["seq"].GetUint() <= 65535)
			lastVoiceSequence = values["seq"].GetUint();

		VoiceOPCode op = static_cast<VoiceOPCode>(json::toInt(values["op"]));
		json::Value nullValue;
		json::Value& d = values.HasMember("d") ? values["d"] : nullValue;
		switch (op) {
		case HELLO: {
			if (!d.IsObject() || !d.HasMember("heartbeat_interval") || !d["heartbeat_interval"].IsNumber())
				break;
			if (!(d["heartbeat_interval"].GetDouble() > 0 && d["heartbeat_interval"].GetDouble() <= 120000)) {
				failEncryption(); return;
			}
			heartbeatInterval = static_cast<time_t>(json::toDouble(d["heartbeat_interval"]));
			if (heartbeatInterval <= 0 || !dave) { failEncryption(); return; }

			//Don't sent a identity during resumes
			if (state & OPEN) {
				if (!heart.isValid()) heartbeat();
				break;
			}

			std::string identity;
			identity.reserve(220 + context.sessionID.length() + context.token.length());
			identity +=
				"{"
					"\"op\": 0," //VoiceOPCode::IDENTIFY
					"\"d\": {"
						"\"server_id\": \"" ; identity += context.serverID ; identity += "\","
						"\"user_id\": \""   ; identity += origin->getID()  ; identity += "\","
						"\"session_id\": \""; identity += context.sessionID; identity += "\","
						"\"token\": \""     ; identity += context.token    ; identity += "\","
						"\"video\": true,"
						"\"streams\": [{\"type\":\"video\",\"rid\":\"100\",\"quality\":100}],"
						"\"max_dave_protocol_version\":"; identity += std::to_string(NXCord::DaveSession::maxVersion); identity +=
					"}"
				"}";
			origin->send(identity, connection);
			}
			state = static_cast<State>(state | CONNECTED);
			if (!heart.isValid()) heartbeat();
			break;
		case READY: {
			if (!d.IsObject() || !d.HasMember("ssrc") || !d["ssrc"].IsUint() ||
			    !d.HasMember("port") || !d["port"].IsUint() || d["port"].GetUint() > 65535 ||
			    !d.HasMember("ip") || !d["ip"].IsString())
				break;
			sSRC = json::toUint(d["ssrc"]);
			videoSSRC = sSRC + 1;
			rtxSSRC = sSRC + 2;
			if (d.HasMember("streams") && d["streams"].IsArray() && !d["streams"].Empty() &&
			    d["streams"][0].IsObject()) {
				const auto& stream = d["streams"][0];
				if (stream.HasMember("ssrc") && stream["ssrc"].IsUint() && stream["ssrc"].GetUint() > 0)
					videoSSRC = stream["ssrc"].GetUint();
				if (stream.HasMember("rtx_ssrc") && stream["rtx_ssrc"].IsUint() && stream["rtx_ssrc"].GetUint() > 0)
					rtxSSRC = stream["rtx_ssrc"].GetUint();
			}
			port = static_cast<uint16_t>(json::toUint(d["port"]));
			std::string ip = json::toStdString(d["ip"]);
			UDP.setReceiveHandler(std::bind(&VoiceConnection::ipDiscovery, this,
				std::placeholders::_1));
			//connect to UDP
			if (!UDP.connect(ip, port)) { failEncryption(); return; }
			//IP Discovery (74-byte v4 format)
			unsigned char packet[74] = { 0 };
			packet[0] = 0x00;
			packet[1] = 0x01;
			packet[2] = 0x00;
			packet[3] = 0x46;
			packet[4] = (sSRC >> 24) & 0xff;
			packet[5] = (sSRC >> 16) & 0xff;
			packet[6] = (sSRC >>  8) & 0xff;
			packet[7] = (sSRC      ) & 0xff;
			UDP.send(packet, 74);
			}
			state = static_cast<State>(state | State::OPEN);
			break;
		case SESSION_DESCRIPTION: {
			if (!d.IsObject() || !d.HasMember("secret_key") || !d["secret_key"].IsArray() ||
			    d["secret_key"].Size() != SECRET_KEY_SIZE || !d.HasMember("mode") ||
			    !d["mode"].IsString() || std::string(d["mode"].GetString()) != "aead_xchacha20_poly1305_rtpsize") {
				failEncryption(); return;
			}
			if (!d.HasMember("dave_protocol_version") || !d["dave_protocol_version"].IsUint() ||
			    d["dave_protocol_version"].GetUint() > NXCord::DaveSession::maxVersion) {
				failEncryption(); return;
			}
			const uint16_t version = d["dave_protocol_version"].GetUint();
			const json::Value& secretKeyJSON = d["secret_key"];
			json::Array secretKeyJSONArray = secretKeyJSON.GetArray();
			const std::size_t secretKeyJSONArraySize = secretKeyJSONArray.Size();
			for (std::size_t i = 0; i < SECRET_KEY_SIZE && i < secretKeyJSONArraySize; ++i) {
					if (!secretKeyJSONArray[i].IsUint() || secretKeyJSONArray[i].GetUint() > 255) {
						failEncryption(); return;
					}
					secretKey[i] = json::toUint(secretKeyJSONArray[i]) & 0xFF;
			}
			if (!dave || !dave->configure(version, sSRC)) { failEncryption(); return; }
			dave->configureVideo(videoSSRC ? videoSSRC : (sSRC + 1));
			// Set speaking to true first, to bypass check
			wasPreviouslySpeaking = true;
			sendSpeaking(false);
			lastTimeSentSpeakingState = 0;
			}
			state = static_cast<State>(state | State::AUDIO_ENABLED);
			if (videoEnabled) {
				setVideoEnabled(true);
			}
			notifyCryptoState(4);
			enableAudioIfReady();
			break;
		case SPEAKING: {
			if (!d.IsObject() || !d.HasMember("ssrc") || !d["ssrc"].IsUint() ||
			    !d.HasMember("user_id") || !d["user_id"].IsString())
				break;
			uint32_t ssrc = json::toUint(d["ssrc"]);
			Snowflake<User> user_id = d["user_id"];
			if (dave && !dave->addUser(user_id.string())) {
				failEncryption(); return;
			}

			std::lock_guard<std::mutex> lock(ssrcLock);
			if (userSSRCs.size() < NXCord::DaveSession::maxMembers || userSSRCs.count(user_id.number()))
				userSSRCs[user_id.number()] = ssrc;
			}
			if (context.eventHandler != nullptr)
				context.eventHandler->onSpeaking(*this);
			break;
		case RESUMED:
			break;
		case HEARTBEAT:
			send_heartbeat(false);
			break;
		case HEARTBEAT_ACK: {
			if (!d.IsObject() || !d.HasMember("t") || !d["t"].IsUint64()) break;
			time_t previous_time = static_cast<time_t>(d["t"].GetUint64());
			if (context.eventHandler != nullptr)
				context.eventHandler->onHeartbeatAck(*this,
					origin->getEpochTimeMillisecond() - previous_time);
			}
			break;
		case CLIENT_DISCONNECT: {
			if (!d.IsObject() || !d.HasMember("user_id") || !d["user_id"].IsString())
				break;
			Snowflake<User> user_id = d["user_id"];
			if (dave) dave->removeUser(user_id.string());
			std::lock_guard<std::mutex> ssrcGuard(ssrcLock);
			auto it = userSSRCs.find(user_id.number());
			if (it != userSSRCs.end()) {
				std::lock_guard<std::mutex> lock(decodersLock);
				decoders.erase(it->second);
				userSSRCs.erase(it);
			}
			}
			break;
		case static_cast<VoiceOPCode>(11): {
			if (!d.IsObject() || !d.HasMember("user_ids") || !d["user_ids"].IsArray()) break;
			for (const auto& id : d["user_ids"].GetArray()) {
				if (!id.IsString() || !dave || !dave->addUser(std::string(id.GetString(), id.GetStringLength()))) {
					failEncryption(); return;
				}
			}
			break;
		}
		case static_cast<VoiceOPCode>(12): {
			if (!d.IsObject() || !d.HasMember("user_id") || !d["user_id"].IsString()) break;
			Snowflake<User> user_id = d["user_id"];
			if (!dave || !dave->addUser(user_id.string())) {
				failEncryption(); return;
			}
			if (d.HasMember("audio_ssrc") && d["audio_ssrc"].IsUint() && d["audio_ssrc"].GetUint() > 0) {
				std::lock_guard<std::mutex> lock(ssrcLock);
				if (userSSRCs.size() < NXCord::DaveSession::maxMembers || userSSRCs.count(user_id.number()))
					userSSRCs[user_id.number()] = d["audio_ssrc"].GetUint();
			}
			break;
		}
		case static_cast<VoiceOPCode>(21):
		case static_cast<VoiceOPCode>(22):
		case static_cast<VoiceOPCode>(24): {
			if (!d.IsObject() || !dave) { failEncryption(); return; }
			const char* key = op == 24 ? "epoch" : "transition_id";
			if (!d.HasMember(key) || !d[key].IsUint64()) { failEncryption(); return; }
			const uint64_t id = d[key].GetUint64();
			if (op != 24 && id > 65535) { failEncryption(); return; }
			uint16_t version = 0;
			if (op != 22) {
				if (!d.HasMember("protocol_version") || !d["protocol_version"].IsUint() ||
				    d["protocol_version"].GetUint() > NXCord::DaveSession::maxVersion) { failEncryption(); return; }
				version = d["protocol_version"].GetUint();
			}
			const bool ok = op == 24 ? dave->prepareEpoch(id, version) :
			                op == 21 ? dave->prepareTransition(static_cast<uint16_t>(id), version) :
			                           dave->executeTransition(static_cast<uint16_t>(id));
			if (!ok) { failEncryption(); return; }
			notifyCryptoState(op);
			enableAudioIfReady();
			break;
		}
		default:
			break;
		}
	}

	void VoiceConnection::processBinaryMessage(const uint8_t* message, size_t length) {
		if (encryptionFailed) return;
		if (length >= 3) lastVoiceSequence = (uint16_t(message[0]) << 8) | message[1];
		if (!dave || !dave->processBinary(message, length)) { failEncryption(); return; }
		notifyCryptoState(message[2]);
		enableAudioIfReady();
	}

	void VoiceConnection::failEncryption() {
		if (encryptionFailed) return;
		encryptionFailed = true;
		if (dave) dave->stop();
		if (context.eventHandler) context.eventHandler->onClosed(*this, -2);
	}

	void VoiceConnection::notifyCryptoState(unsigned int opcode) {
		if (context.eventHandler) context.eventHandler->onCryptoState(*this, opcode, dave && dave->ready());
	}

	void VoiceConnection::enableAudioIfReady() {
		if (!audioStarted && !encryptionFailed && (state & State::ABLE) == State::ABLE && dave && dave->ready()) {
			audioStarted = true;
			if (context.eventHandler) context.eventHandler->onReady(*this);
		}
	}

	void VoiceConnection::processCloseCode(const int16_t code) {
		if (context.eventHandler) context.eventHandler->onClosed(*this, code);
		getDiscordClient().removeVoiceConnectionAndContext(*this);
	}

	void VoiceConnection::heartbeat() {
		send_heartbeat(true);
		heart = origin->schedule([this]() {
			this->heartbeat();
		}, heartbeatInterval);
	}

	void VoiceConnection::send_heartbeat(bool includeUDP) {
		std::string currentTime = std::to_string(origin->getEpochTimeMillisecond());
		std::string heartbeat;
		heartbeat.reserve(40 + currentTime.length());
		heartbeat +=
			"{"
				"\"op\": 3, "
				"\"d\": {\"t\":"; heartbeat += currentTime; heartbeat +=
				",\"seq_ack\":"; heartbeat += std::to_string(lastVoiceSequence); heartbeat += "}}";
		origin->send(heartbeat, connection);

		(void)includeUDP; // v8 needs only websocket heartbeats, not legacy UDP keepalive.

		if (context.eventHandler != nullptr)
			context.eventHandler->onHeartbeat(*this);
	}

	inline void VoiceConnection::scheduleNextTime(AudioTimer& timer, TimedTask code, const time_t interval) {
		timer.nextTime += interval;
		time_t delay = timer.nextTime - origin->getEpochTimeMillisecond();
		delay = 0 < delay ? delay : 0;

		timer.timer = origin->schedule(code, delay);
	}

	void VoiceConnection::startSpeaking() {
		if ((state & State::ABLE) != State::ABLE) return;

		if (!audioSource->isOpusEncoded())
#if defined(NONEXISTENT_OPUS)
			return;
#else
			if (!(state & CAN_ENCODE) || encoder == nullptr) {
				//init opus
				int opusError = 0;
				encoder = opus_encoder_create(
					/*Sampling rate(Hz)*/AudioTransmissionDetails::bitrate(),
					/*Channels*/         AudioTransmissionDetails::channels(),
					/*Mode*/             OPUS_APPLICATION_VOIP,
					&opusError);
				if (opusError) {//error check
					return;
				}
				state = static_cast<State>(state | State::CAN_ENCODE);
			}
#endif

		//say something
		speechTimer.nextTime = origin->getEpochTimeMillisecond();
		speak();
	}

	void VoiceConnection::sendSpeaking(bool isNowSpeaking) {
		if (isNowSpeaking) {
			state = static_cast<State>(state | State::SENDING_AUDIO);
		} else {
			state = static_cast<State>(state & ~State::SENDING_AUDIO);
		}

		if (lastTimeSentSpeakingState == 0) {
			lastTimeSentSpeakingState = origin->getEpochTimeMillisecond();
		} else {
			time_t currentTime = origin->getEpochTimeMillisecond();
			if (currentTime - lastTimeSentSpeakingState < 500) {
				return; // Send speaking state in 500 ms interval to reduce traffic
			}
			lastTimeSentSpeakingState = currentTime;
		}

		if (wasPreviouslySpeaking == isNowSpeaking) {
			return;
		}
		wasPreviouslySpeaking = isNowSpeaking;

		std::string ssrc = std::to_string(sSRC);
		/*The number 44 comes from 1 plus the length of this string
			{"op":5,"d":{"speaking":0,"delay":0,"ssrc":}}
		*/
		std::string speaking;
		speaking.reserve(44 + ssrc.length());
		speaking +=
			"{"
				"\"op\":5,"
				"\"d\":{"
					"\"speaking\":"; speaking += isNowSpeaking ? "1" : "0"; speaking += ","
					"\"delay\":0,"
					"\"ssrc\":"; speaking += ssrc; speaking +=
				"}"
			"}";
		origin->send(speaking, connection);
	}

	void VoiceConnection::speak() {
		//check that we are can still send audio data
		if ((state & State::ABLE) != State::ABLE)
			return;

		AudioTransmissionDetails details(context, 0, samplesSentLastTime);

		std::size_t length = 0;

		//send the audio data
		if (silenceCounter < 10) {
			++silenceCounter;
			std::array<AudioSample, AudioTransmissionDetails::proposedLength()> silenceBytes{};
			AudioSample* ptr = silenceBytes.data();
			speak(ptr, silenceBytes.size(), false);
		} else {
			bool available = audioSource->frameAvailable();
			if (available) {
				sendSpeaking(true);
				if (audioSource->type == AUDIO_CONTAINER) {
					auto audioVectorSource = &static_cast<BasicAudioSourceForContainers&>(*audioSource);
					audioVectorSource->speak(*this, details, length);
				} else {
					AudioSample* audioBuffer = nullptr;
					bool opus = audioSource->isOpusEncoded();
					audioSource->read(details, audioBuffer, length);
					speak(audioBuffer, length, opus);
				}
			} else {
				sendSpeaking(false);
			}
		}

		//schedule next send
		const time_t interval = static_cast<time_t>(
			(static_cast<float>(length) / static_cast<float>(
				AudioTransmissionDetails::bitrate() * AudioTransmissionDetails::channels()
			)) * 1000.0f
		);

		scheduleNextTime(speechTimer,
			[this]() {
				this->speak();
			}, std::max(static_cast<size_t>(interval),
				AudioTransmissionDetails::proposedLengthOfTime())
		);
	}

	void VoiceConnection::speak(AudioSample*& audioData, const std::size_t & length, bool isOpus)  {
		samplesSentLastTime = 0;
		//This is only called in speak() so already checked that we can still send audio data

		//stop sending data when there's no data
		if (length == 0) {
			return;
		}

		//the >>1 cuts it in half since you are using 2 channels
		const std::size_t frameSize = length >> 1;

		if (!isOpus) {
#if defined(NONEXISTENT_OPUS)
			return;
#else
			//encode data
			constexpr opus_int32 encodedAudioMaxLength =
				static_cast<opus_int32>(AudioTransmissionDetails::proposedLength());
			unsigned char encodedAudioData[encodedAudioMaxLength]; //11.52 kilobytes
			opus_int32 encodedAudioLength = opus_encode(
				encoder, audioData, static_cast<int>(frameSize),
				encodedAudioData, encodedAudioMaxLength);
			if (encodedAudioLength <= 0) return;
			//send it
			uint8_t * encodedAudioDataPointer = encodedAudioData;
			sendAudioData(encodedAudioDataPointer, encodedAudioLength, frameSize);
#endif
		} else {
			//encoded data should be in uint8
			sendAudioData(reinterpret_cast<uint8_t*&>(audioData), length, frameSize);
		}
	}

	void VoiceConnection::sendAudioData(
		uint8_t*& encodedAudioData,
		const std::size_t & length,
		const std::size_t & frameSize
	) {
#ifndef NONEXISTENT_SODIUM
		std::vector<uint8_t> encryptedFrame;
		if (!dave || !dave->encrypt(encodedAudioData, length, encryptedFrame)) return;
		const uint8_t* media = encryptedFrame.data();
		const size_t mediaLength = encryptedFrame.size();
		std::lock_guard<std::mutex> lock(rtpSendLock);
		if ((state & State::ABLE) != State::ABLE) return;
		++sequence;
		++encryptNonce;

		const uint8_t header[rtpHeaderLength] = {
			0x80,
			0x78,
			static_cast<uint8_t>((sequence  >> (8 * 1)) & 0xff),
			static_cast<uint8_t>((sequence  >> (8 * 0)) & 0xff),
			static_cast<uint8_t>((timestamp >> (8 * 3)) & 0xff),
			static_cast<uint8_t>((timestamp >> (8 * 2)) & 0xff),
			static_cast<uint8_t>((timestamp >> (8 * 1)) & 0xff),
			static_cast<uint8_t>((timestamp >> (8 * 0)) & 0xff),
			static_cast<uint8_t>((sSRC      >> (8 * 3)) & 0xff),
			static_cast<uint8_t>((sSRC      >> (8 * 2)) & 0xff),
			static_cast<uint8_t>((sSRC      >> (8 * 1)) & 0xff),
			static_cast<uint8_t>((sSRC      >> (8 * 0)) & 0xff),
		};

		uint8_t nonce[nonceSize] = { 0 };
		nonce[0] = static_cast<uint8_t>((encryptNonce >> 24) & 0xff);
		nonce[1] = static_cast<uint8_t>((encryptNonce >> 16) & 0xff);
		nonce[2] = static_cast<uint8_t>((encryptNonce >>  8) & 0xff);
		nonce[3] = static_cast<uint8_t>((encryptNonce      ) & 0xff);

		const size_t numOfBytes = rtpHeaderLength + mediaLength + crypto_aead_xchacha20poly1305_ietf_ABYTES + 4;
		std::vector<uint8_t> audioDataPacket(numOfBytes);
		std::memcpy(audioDataPacket.data(), header, rtpHeaderLength);

		unsigned long long ciphertextLen = 0;
		crypto_aead_xchacha20poly1305_ietf_encrypt(
			audioDataPacket.data() + rtpHeaderLength, &ciphertextLen,
			media, mediaLength,
			header, rtpHeaderLength,
			nullptr, nonce, secretKey);

		std::memcpy(audioDataPacket.data() + rtpHeaderLength + ciphertextLen, nonce, 4);

		UDP.send(audioDataPacket.data(), audioDataPacket.size());
		samplesSentLastTime = frameSize << 1;
		timestamp += static_cast<uint32_t>(frameSize);
#else
	#error Can not use voice without libsodium, libsodium not detected.
#endif
	}

	void VoiceConnection::setVideoEnabled(bool enabled, int width, int height) {
		videoEnabled = enabled;
		std::string stateUpdate;
		stateUpdate.reserve(160);
		stateUpdate +=
			"{"
				"\"op\":4,"
				"\"d\":{"
					"\"guild_id\":\""; stateUpdate += context.serverID; stateUpdate += "\","
					"\"channel_id\":\""; stateUpdate += context.channelID; stateUpdate += "\","
					"\"self_mute\":false,"
					"\"self_deaf\":false,"
					"\"self_video\":"; stateUpdate += enabled ? "true" : "false"; stateUpdate +=
				"}"
			"}";
		origin->send(stateUpdate, origin->connection);

		if (!(state & State::OPEN) || sSRC == 0) return;
		const uint32_t vSsrc = videoSSRC ? videoSSRC : (sSRC + 1);
		const uint32_t rSsrc = rtxSSRC ? rtxSSRC : (vSsrc + 1);
		if (dave && enabled) dave->configureVideo(vSsrc);

		std::string videoMsg;
		videoMsg.reserve(320);
		videoMsg +=
			"{"
				"\"op\":12,"
				"\"d\":{"
					"\"audio_ssrc\":"; videoMsg += std::to_string(sSRC); videoMsg += ","
					"\"video_ssrc\":"; videoMsg += std::to_string(enabled ? vSsrc : 0); videoMsg += ","
					"\"rtx_ssrc\":"; videoMsg += std::to_string(enabled ? rSsrc : 0); videoMsg += ","
					"\"streams\":[{"
						"\"type\":\"video\","
						"\"rid\":\"100\","
						"\"ssrc\":"; videoMsg += std::to_string(vSsrc); videoMsg += ","
						"\"active\":"; videoMsg += enabled ? "true" : "false"; videoMsg += ","
						"\"quality\":100,"
						"\"rtx_ssrc\":"; videoMsg += std::to_string(rSsrc); videoMsg += ","
						"\"max_bitrate\":1500000,"
						"\"max_framerate\":10,"
						"\"max_resolution\":{"
							"\"type\":\"fixed\","
							"\"width\":"; videoMsg += std::to_string(width); videoMsg += ","
							"\"height\":"; videoMsg += std::to_string(height); videoMsg +=
						"}"
					"}]"
				"}"
			"}";
		origin->send(videoMsg, connection);
	}

	void VoiceConnection::sendVideoRtpPacket(
		const uint8_t* payload,
		std::size_t payloadLen,
		uint32_t timestamp90k,
		bool marker
	) {
#ifndef NONEXISTENT_SODIUM
		if (!payload || payloadLen == 0) return;
		std::lock_guard<std::mutex> lock(rtpSendLock);
		if ((state & State::ABLE) != State::ABLE) return;
		const uint32_t vSsrc = videoSSRC ? videoSSRC : (sSRC + 1);
		if (!vSsrc) return;
		++videoSequence;
		++encryptNonce;

		// One-byte RTP header extension (RFC 8285) carrying playout-delay = 0/0,
		// like the official client. With *_rtpsize modes the 4-byte extension
		// header is authenticated (AAD) and the extension body is encrypted
		// together with the payload.
		const uint8_t header[rtpHeaderLength + 4] = {
			0x90,
			static_cast<uint8_t>((marker ? 0x80 : 0x00) | 101),
			static_cast<uint8_t>((videoSequence >> 8) & 0xff),
			static_cast<uint8_t>((videoSequence >> 0) & 0xff),
			static_cast<uint8_t>((timestamp90k  >> 24) & 0xff),
			static_cast<uint8_t>((timestamp90k  >> 16) & 0xff),
			static_cast<uint8_t>((timestamp90k  >>  8) & 0xff),
			static_cast<uint8_t>((timestamp90k  >>  0) & 0xff),
			static_cast<uint8_t>((vSsrc         >> 24) & 0xff),
			static_cast<uint8_t>((vSsrc         >> 16) & 0xff),
			static_cast<uint8_t>((vSsrc         >>  8) & 0xff),
			static_cast<uint8_t>((vSsrc         >>  0) & 0xff),
			0xBE, 0xDE, 0x00, 0x01,
		};
		static const uint8_t extBody[4] = { 0x52, 0x00, 0x00, 0x00 }; // id 5, 3 bytes

		uint8_t nonce[nonceSize] = { 0 };
		nonce[0] = static_cast<uint8_t>((encryptNonce >> 24) & 0xff);
		nonce[1] = static_cast<uint8_t>((encryptNonce >> 16) & 0xff);
		nonce[2] = static_cast<uint8_t>((encryptNonce >>  8) & 0xff);
		nonce[3] = static_cast<uint8_t>((encryptNonce      ) & 0xff);

		const std::size_t plainLen = sizeof(extBody) + payloadLen;
		const std::size_t numOfBytes = sizeof(header) + plainLen + crypto_aead_xchacha20poly1305_ietf_ABYTES + 4;
		if (numOfBytes > 1400) return;
		uint8_t plain[1400];
		std::memcpy(plain, extBody, sizeof(extBody));
		std::memcpy(plain + sizeof(extBody), payload, payloadLen);
		uint8_t packet[1400];
		std::memcpy(packet, header, sizeof(header));

		unsigned long long ciphertextLen = 0;
		crypto_aead_xchacha20poly1305_ietf_encrypt(
			packet + sizeof(header), &ciphertextLen,
			plain, plainLen,
			header, sizeof(header),
			nullptr, nonce, secretKey);

		std::memcpy(packet + sizeof(header) + ciphertextLen, nonce, 4);
		UDP.send(packet, numOfBytes);
#endif
	}

	void VoiceConnection::packetizeAndSendNal(
		const uint8_t* nal,
		std::size_t nalLen,
		uint32_t timestamp90k,
		bool isLastNal,
		std::size_t& packetsInBurst
	) {
		if (!nal || nalLen == 0) return;
		constexpr std::size_t kMaxSingleNalSize = 1150;
		if (nalLen <= kMaxSingleNalSize) {
			sendVideoRtpPacket(nal, nalLen, timestamp90k, isLastNal);
			if (++packetsInBurst % 6 == 0 && !isLastNal) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return;
		}
		const uint8_t nalHeader = nal[0];
		const uint8_t fuIndicator = static_cast<uint8_t>((nalHeader & 0xE0) | 28);
		const uint8_t nalType = static_cast<uint8_t>(nalHeader & 0x1F);
		constexpr std::size_t kMaxFragPayload = kMaxSingleNalSize - 2;

		uint8_t fuPacket[kMaxSingleNalSize];
		std::size_t offset = 1;
		while (offset < nalLen) {
			const std::size_t remaining = nalLen - offset;
			const std::size_t chunkLen = std::min(remaining, kMaxFragPayload);
			const bool isFirstFrag = (offset == 1);
			const bool isLastFrag = (offset + chunkLen == nalLen);
			const uint8_t fuHeader = static_cast<uint8_t>(
				(isFirstFrag ? 0x80 : 0x00) |
				(isLastFrag  ? 0x40 : 0x00) |
				nalType
			);
			fuPacket[0] = fuIndicator;
			fuPacket[1] = fuHeader;
			std::memcpy(fuPacket + 2, nal + offset, chunkLen);

			sendVideoRtpPacket(fuPacket, chunkLen + 2, timestamp90k, isLastNal && isLastFrag);
			if (++packetsInBurst % 6 == 0 && !isLastFrag) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			offset += chunkLen;
		}
	}

	void VoiceConnection::sendVideoFrame(
		const uint8_t* annexBFrame,
		std::size_t length,
		std::size_t spsLen,
		std::size_t ppsLen,
		uint32_t timestamp90k
	) {
		if (!videoEnabled || !annexBFrame || length <= 4 || (state & State::ABLE) != State::ABLE || !dave)
			return;
		const uint32_t vSsrc = videoSSRC ? videoSSRC : (sSRC + 1);
		std::vector<uint8_t> encryptedFrame;
		if (!dave->encryptVideo(vSsrc, annexBFrame, length, encryptedFrame))
			return;

		std::size_t packetsInBurst = 0;
		if (spsLen > 0 && ppsLen > 0) {
			const std::size_t sliceStart = 12 + spsLen + ppsLen;
			if (encryptedFrame.size() <= sliceStart) return;
			const uint8_t* spsPtr = encryptedFrame.data() + 4;
			const uint8_t* ppsPtr = encryptedFrame.data() + 8 + spsLen;
			const uint8_t* slicePtr = encryptedFrame.data() + sliceStart;
			const std::size_t sliceLen = encryptedFrame.size() - sliceStart;

			packetizeAndSendNal(spsPtr, spsLen, timestamp90k, false, packetsInBurst);
			packetizeAndSendNal(ppsPtr, ppsLen, timestamp90k, false, packetsInBurst);
			packetizeAndSendNal(slicePtr, sliceLen, timestamp90k, true, packetsInBurst);
		} else {
			if (encryptedFrame.size() <= 4) return;
			const uint8_t* slicePtr = encryptedFrame.data() + 4;
			const std::size_t sliceLen = encryptedFrame.size() - 4;
			packetizeAndSendNal(slicePtr, sliceLen, timestamp90k, true, packetsInBurst);
		}
	}

	//To do test this
	void VoiceConnection::startListening() {
		listenTimer.nextTime = origin->getEpochTimeMillisecond();
		UDP.setReceiveHandler(std::bind(&VoiceConnection::processIncomingAudio,
			this, std::placeholders::_1));
	}

	int VoiceConnection::getPayloadOffset(const uint8_t* data, int csrcLength) const {
        // headerLength defines number of 4-byte words in the extension
        int16_t headerLength = (data[rtpHeaderLength + 2 + csrcLength] << 8)
			| (data[rtpHeaderLength + 2 + csrcLength + 1] & 0xff);
        size_t i = rtpHeaderLength // RTP header = 12 bytes
                + 4                    // header which defines a profile and length each 2-bytes = 4 bytes
                + csrcLength           // length of CSRC list (this seems to be always 0 when an extension exists)
                + headerLength * 4;    // number of 4-byte words in extension = len * 4 bytes

        // strip excess 0 bytes
        while (data[i] == 0) ++i;
        return i;
    }

	// Taken from JDA
	size_t VoiceConnection::getRTPOffset(const uint8_t* data) const {
		bool hasExtension = (data[0] & 0x10) != 0;
		uint8_t cc = data[0] & 0x0f;
		size_t csrcLength = cc * 4;
		uint16_t extension = hasExtension ?
			(data[rtpHeaderLength + csrcLength] << 8) | (data[rtpHeaderLength + csrcLength + 1] & 0xff) : 0;
		size_t offset = rtpHeaderLength + csrcLength;
		if (hasExtension && extension == discordRTPExtension) {
			offset = getPayloadOffset(data, csrcLength);
		}
		return offset;
	}

	void VoiceConnection::ipDiscovery(const std::vector<uint8_t>& iPDiscovery) {
		if (iPDiscovery.size() != 74 || iPDiscovery[0] != 0 || iPDiscovery[1] != 2 ||
		    iPDiscovery[2] != 0 || iPDiscovery[3] != 70)
			return;
		const uint32_t discoveredSsrc = (uint32_t(iPDiscovery[4]) << 24) | (uint32_t(iPDiscovery[5]) << 16) |
		    (uint32_t(iPDiscovery[6]) << 8) | iPDiscovery[7];
		const auto end = std::find(iPDiscovery.begin() + 8, iPDiscovery.begin() + 72, 0);
		if (discoveredSsrc != sSRC || end == iPDiscovery.begin() + 72 || end == iPDiscovery.begin() + 8) return;
		std::string receiveHost(iPDiscovery.begin() + 8, end);
		if (receiveHost.find_first_not_of("0123456789.:") != std::string::npos) return;
		const uint16_t receivePort = (uint16_t(iPDiscovery[72]) << 8) | iPDiscovery[73];
		if (!receivePort) return;
		UDP.unsetReceiveHandler();
		//send Select Protocol Payload
		std::string protocol;
		protocol.reserve(320 + receiveHost.length());
		protocol +=
			"{"
				"\"op\": 1," //VoiceOPCode::SELECT_PROTOCOL
				"\"d\": {"
					"\"protocol\": \"udp\","
					"\"data\": {"
						"\"address\": \""; protocol += receiveHost                ; protocol += "\","
						"\"port\": "     ; protocol += std::to_string(receivePort); protocol +=   ","
						"\"mode\": \"aead_xchacha20_poly1305_rtpsize\""
					"},"
					"\"codecs\": ["
						"{\"name\":\"opus\",\"type\":\"audio\",\"priority\":1000,\"payload_type\":120},"
						"{\"name\":\"H264\",\"type\":\"video\",\"priority\":1000,\"payload_type\":101,\"rtx_payload_type\":102}"
					"]"
				"}"
			"}";
		// The discovery callback runs on the UDP thread. wslay's send/receive
		// queues belong to the client loop; never mutate them from this thread.
		std::lock_guard<std::mutex> lock(protocolLock);
		pendingProtocol = std::move(protocol);
	}

	void VoiceConnection::flushPendingUDP() {
		std::string protocol;
		{
			std::lock_guard<std::mutex> lock(protocolLock);
			protocol.swap(pendingProtocol);
		}
		if (!protocol.empty()) origin->send(std::move(protocol), connection);
	}

	void VoiceConnection::processIncomingAudio(const std::vector<uint8_t>& data)
	{
#if !defined(NONEXISTENT_SODIUM) || !defined(NONEXISTENT_OPUS)
		if (data.size() < rtpHeaderLength + crypto_aead_xchacha20poly1305_ietf_ABYTES + 4)
			return;
		if ((data[0] >> 6) != 2 || (data[1] & 0x7F) != 0x78)
			return;

		uint32_t ssrc = (static_cast<uint32_t>(data[8]) << 24)
			| (static_cast<uint32_t>(data[9]) << 16)
			| (static_cast<uint32_t>(data[10]) << 8)
			| static_cast<uint32_t>(data[11]);
		std::string sender;
		{
			std::lock_guard<std::mutex> lock(ssrcLock);
			for (const auto& entry : userSSRCs) {
				if (entry.second == ssrc) { sender = std::to_string(entry.first); break; }
			}
		}
		if (sender.empty()) return;

		bool hasExtension = (data[0] & 0x10) != 0;
		size_t csrcCount = data[0] & 0x0f;
		size_t aadLen = rtpHeaderLength + csrcCount * 4 + (hasExtension ? 4 : 0);
		if (data.size() < aadLen + crypto_aead_xchacha20poly1305_ietf_ABYTES + 4)
			return;

		size_t extByteLen = 0;
		if (hasExtension) {
			size_t extWords = (static_cast<size_t>(data[aadLen - 2]) << 8)
				| static_cast<size_t>(data[aadLen - 1]);
			extByteLen = extWords * 4;
		}

		// Extract 4-byte nonce suffix into 24-byte nonce
		uint8_t nonce[nonceSize] = { 0 };
		std::memcpy(nonce, data.data() + data.size() - 4, 4);

		size_t ciphertextLen = data.size() - aadLen - 4;
		// Reserve rtpHeaderLength (12 bytes) padding at start for HwopusHeader
		std::vector<uint8_t> decryptedData(rtpHeaderLength + ciphertextLen);
		unsigned long long decryptedLen = 0;

		bool isForged = crypto_aead_xchacha20poly1305_ietf_decrypt(
			decryptedData.data() + rtpHeaderLength, &decryptedLen,
			nullptr,
			data.data() + aadLen, ciphertextLen,
			data.data(), aadLen,
			nonce, secretKey
		) != 0;
		if (isForged || decryptedLen <= extByteLen)
			return;

		uint8_t* opusData = decryptedData.data() + rtpHeaderLength + extByteLen;
		size_t opusLen = static_cast<size_t>(decryptedLen) - extByteLen;
		if ((data[0] & 0x20) != 0) {
			const size_t padding = opusData[opusLen - 1];
			if (!padding || padding >= opusLen) return;
			opusLen -= padding;
		}

		uint8_t silenceBytes[] = {0xF8, 0xFF, 0xFE};
		if (opusLen == 3 && std::memcmp(opusData, silenceBytes, 3) == 0)
			return;
		std::vector<uint8_t> plaintext;
		if (!dave || !dave->decrypt(sender, opusData, opusLen, plaintext)) return;
		// Hardware Opus writes its header immediately before the frame pointer.
		// Preserve that headroom after removing the DAVE envelope too.
		opusLen = plaintext.size();
		decryptedData.resize(rtpHeaderLength + opusLen);
		opusData = decryptedData.data() + rtpHeaderLength;
		std::memcpy(opusData, plaintext.data(), opusLen);

		int read;
		AudioSample buf[AudioTransmissionDetails::proposedLength()] = { 0 };
		{
			std::lock_guard<std::mutex> lock(decodersLock);
			auto it = decoders.find(ssrc);
			if (it == decoders.end()) {
				it = decoders.insert(std::make_pair(ssrc, std::make_unique<CustomOpusDecoder>())).first;
			}

			//decode
			read = it->second->decodeOpus(opusData, opusLen, buf);

			if (read <= 0) return;
		}

		{
			std::lock_guard<std::mutex> lock(audioOutputLock);
			if (!audioOutput) {
				return;
			}
			AudioTransmissionDetails details(context, ssrc, 0);
			std::vector<AudioSample> decodedAudio(buf, buf + read * 2); // 2 channels
			audioOutput->write(decodedAudio, details);
		}
#endif
	}
}
#else
void SleepyDiscord::VoiceConnection::initialize() {}
void SleepyDiscord::VoiceConnection::processMessage(const std::string &/*message*/) {}
void SleepyDiscord::VoiceConnection::processCloseCode(const int16_t /*code*/) {}
#endif
