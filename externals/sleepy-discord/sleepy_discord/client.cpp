#if _MSC_VER && !__INTEL_COMPILER
#pragma warning( disable: 4307 )  //ignore integer overflow, becuase we are taking advantage of it
#endif

#include <chrono>
#include <functional>
#include <memory>
#include <iterator>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include "client.h"
#include "version_helper.h"
//#include "json.h"
#include "rapidjson/document.h"
#ifdef SLEEPY_USE_HARD_CODED_GATEWAY
	#include <cstring>
#endif

namespace SleepyDiscord {
	void BaseDiscordClient::start(const std::string& _token, const char maxNumOfThreads, int _shardID, int _shardCount) {
		if (!scheduleHandler) {
			setError(CANT_SCHEDULE);
			return;
		}

		ready = false;
		quiting = false;
		lastHeartbeat = 0;
		wasHeartbeatAcked = true;
		if (_token.rfind("Bot ", 0) == 0) {
			bot = true;
			token = _token.substr(4);
		} else {
			bot = false;
			token = _token; //add client to list
		}
		if (_shardID != 0 || _shardCount != 0)
			setShardID(_shardID, _shardCount);

		messagesRemaining = 4;
		getTheGateway();
		connect(theGateway, this, connection);
#ifndef SLEEPY_ONE_THREAD
		if (USE_RUN_THREAD <= maxNumOfThreads) runAsync();
#endif
	}

	BaseDiscordClient::~BaseDiscordClient() {
		ready = false;
		if (heart.isValid()) heart.stop();
	}

	Response BaseDiscordClient::request(const RequestMethod method, Route path, const std::string jsonParameters/*,
		cpr::Parameters httpParameters*/, const std::initializer_list<Part>& multipartParameters,
		RequestCallback callback, RequestMode mode) {
		//check if rate limited
		Response response;
		const time_t currentTime = getEpochTimeMillisecond();
		response.birth = currentTime;
		const auto continueBeingRateLimited = [&]() {
			onExceededRateLimit(isGlobalRateLimited, nextRetry - currentTime, mode, { *this, method, path, jsonParameters, multipartParameters, callback });
			response.statusCode = TOO_MANY_REQUESTS;
			setError(response.statusCode);
			return response;
		};
		if (isGlobalRateLimited) {
			if (nextRetry <= currentTime) {
				isGlobalRateLimited = false;
			} else {
				return continueBeingRateLimited();
			}
		}
		const std::string bucket = path.bucket(method);
		auto bucketResetTimestamp = buckets.find(bucket);
		if (bucketResetTimestamp != buckets.end()) {
			if (bucketResetTimestamp->second <= currentTime) {
				buckets.erase(bucketResetTimestamp);
			} else {
				return continueBeingRateLimited();
			}
		}
		{	//the { is used so that onResponse is called after session is removed to make debugging performance issues easier
			//request starts here
			Session session;
			session.setUrl("https://discord.com/api/v9/" + path.url());
			std::vector<HeaderPair> header = {
				{ "Authorization", bot ? "Bot " + getToken() : getToken() },
			};
			if (bot) {
				header.push_back({ "User-Agent", userAgent });
			}
			if (jsonParameters != "") {
				session.setBody(&jsonParameters);
				header.push_back({ "Content-Type"  , "application/json"                      });
				header.push_back({ "Content-Length", std::to_string(jsonParameters.length()) });
			//} else if (httpParameters.content != "") {	//this is broken for now
			//	session.SetParameters(httpParameters);
			} else if (0 < multipartParameters.size()) {
				session.setMultipart(multipartParameters);
				header.push_back({ "Content-Type", "multipart/form-data" });
			} else {
				header.push_back({ "Content-Length", "0" });
			}
			session.setHeader(header);

			//Do the response
			switch (method) {
			case Post: case Patch: case Delete: case Get: case Put:
				response = session.request(method);
				break;
			default: response.statusCode = BAD_REQUEST; break; //unexpected method
			}

			//status checking
			switch (response.statusCode) {
			case OK: case CREATED: case NO_CONTENT: case NOT_MODIFIED: break;
			case TOO_MANY_REQUESTS:
				{   //this should fall down to default
					std::string rawRetryAfter = response.header["retry-after"];
					//the 5 is an arbitrary number, and there's 1000 ms in a second
					int retryAfter = rawRetryAfter != "" ? static_cast<int>(std::strtod(rawRetryAfter.c_str(), nullptr) * 1000) : 5 * 1000;
					if (retryAfter <= 0) retryAfter = 5 * 1000;
					isGlobalRateLimited = response.header["x-ratelimit-global"] == "true";
					nextRetry = getEpochTimeMillisecond() + retryAfter;
					if (!isGlobalRateLimited) {
						buckets[bucket] = nextRetry;
						onDepletedRequestSupply(bucket, retryAfter);
					}
					onExceededRateLimit(isGlobalRateLimited, retryAfter, mode, { *this, method, path, jsonParameters, multipartParameters, callback });
				}
			default:
				{		//error
                                  const ErrorCode code = static_cast<ErrorCode>(response.statusCode);
                                  setError(code);		//https error
                                  if (!response.text.empty()) {
                                    //json::Values values = json::getValues(response.text.c_str(),
                                    //{ "code", "message" });	//parse json to get code and message
                                    rapidjson::Document document;
                                    document.Parse(response.text.c_str());
                                    if (!document.IsObject()) {
                                      onError(GENERAL_ERROR, "No error code or message from Discord");
                                    } else {
                                      auto errorCode = document.FindMember("code");
                                      auto errorMessage = document.FindMember("message");
                                      if (errorCode != document.MemberEnd() && errorCode->value.IsInt())
                                        onError(
                                            static_cast<ErrorCode>(errorCode->value.GetInt()),
                                            { errorMessage != document.MemberEnd() && errorMessage->value.IsString() ? errorMessage->value.GetString() : "" }
                                        );
                                      else if (!response.text.empty())
                                        onError(ERROR_NOTE, response.text);
                                    }
                                  }
				} break;
			}

			//rate limit check
			if (response.header["x-ratelimit-remaining"] == "0" && response.statusCode != TOO_MANY_REQUESTS) {
				std::tm date = {};
				//for some reason std::get_time requires gcc 5
				std::istringstream dateStream(response.header["date"]);
				dateStream >> std::get_time(&date, "%a, %d %b %Y %H:%M:%S GMT");
				const time_t reset = static_cast<time_t>(std::strtol(response.header["x-ratelimit-reset"].c_str(), nullptr, 10));
				if (reset > 0) {
#if defined(_WIN32) || defined(_WIN64)
					std::tm gmTM;
					std::tm*const resetGM = &gmTM;
					gmtime_s(resetGM, &reset);
#else
					std::tm* resetGM = std::gmtime(&reset);
#endif
					const time_t resetDelta = (std::mktime(resetGM) - std::mktime(&date)) * 1000;
					buckets[bucket] = resetDelta + getEpochTimeMillisecond();
					onDepletedRequestSupply(bucket, resetDelta);
				}
			}

			if (callback)
				callback(response);
		}
		onResponse(response);
		return response;
	}

	const Route BaseDiscordClient::path(const char * source, std::initializer_list<std::string> values) {
		return Route(source, values);
	}

	/*std::shared_ptr<ServerCache> BaseDiscordClient::createServerCache() {
		setServerCache(std::make_shared<ServerCache>());
		return getServerCache();
	}

	void BaseDiscordClient::setServerCache(std::shared_ptr<ServerCache> cache) {
		serverCache = cache;
		if ((ready || !isBot()) && serverCache->size() == 0)
			*serverCache = getServers().get<Cache>();
	}*/

	void BaseDiscordClient::onDepletedRequestSupply(const Route::Bucket&, time_t) {
	}

	void BaseDiscordClient::onExceededRateLimit(bool, std::time_t timeTilRetry, RequestMode mode, Request request) {
		if (mode == Async)
			schedule(request, timeTilRetry);
	}

	void BaseDiscordClient::updateStatus(std::string gameName, uint64_t idleSince, Status status, bool afk) {
		std::string statusString[] = {
			"online", "dnd", "idle", "invisible", "offline"
		};

		sendL(json::createJSON({
			{ "op", json::integer(STATUS_UPDATE) },
			{ "d", json::createJSON({
				{"since", idleSince != 0 ? json::UInteger(idleSince) : "null"},
				{"game", gameName != "" ? json::createJSON({
					{"name", json::string(gameName)},
					{"type", json::integer(0)}
				}) : "null"},
				{ "status", SleepyDiscord::json::string(statusString[status]) },
				{ "afk", SleepyDiscord::json::boolean(afk) }
			})}
		}));
	}

	void BaseDiscordClient::waitTilReady() {
		while (!ready) sleep(1000);
	}

	void BaseDiscordClient::setShardID(int _shardID, int _shardCount) {
		shardID = _shardID;
		shardCount = _shardCount;
	}

	void BaseDiscordClient::getTheGateway() {
#ifdef SLEEPY_USE_HARD_CODED_GATEWAY
		theGateway = "wss://gateway.discord.gg/?v=9";	//This is needed for when session is disabled
#else
		Session session;
		session.setUrl("https://discord.com/api/v9/gateway");
		Response a = session.request(Get);	//todo change this back to a post
		if (!a.text.length()) {	//error check
			quit(false, true);
			return setError(GATEWAY_FAILED);
		}
		if (!theGateway.empty())
			theGateway.clear();
		rapidjson::Document doc;
		doc.Parse(a.text.c_str(), a.text.length());
		if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("url") && doc["url"].IsString()) {
			theGateway = doc["url"].GetString();
			theGateway += "/?v=9";
		} else {
			theGateway = "wss://gateway.discord.gg/?v=9";
		}
#endif
	}

	void BaseDiscordClient::sendIdentity() {
#if defined(_WIN32) || defined(_WIN64)
		const char * os = "Windows";
#elif defined(__APPLE__) || defined(__MACH__)
		const char * os = "macOS";
#elif defined(__linux__) || defined(linux) || defined(__linux)
		const char * os = "Linux";
#elif defined __FreeBSD__
		const char * os = "FreeBSD";
#elif defined(unix) || defined(__unix__) || defined(__unix)
		const char * os = "Unix";
#else
		const char* os = "Linux";
#endif
		std::string identity;
		identity.reserve(320); //remember to change this number when editing identity
		identity +=
		"{"
			"\"op\":2,"
			"\"d\":{"
				"\"token\":\""; identity += getToken(); identity += "\","
				"\"properties\":{"
					"\"$os\":\""; identity += os; identity += "\","
					"\"$browser\":\"Chrome\","
					"\"$device\":\"\","
					"\"$referrer\":\"\","
					"\"$referring_domain\":\"\""
				"},"
				"\"v\": 9,"
				"\"compress\":false,";
		if (bot) {
			identity += "\"intents\":641,";
		}
		if (shardCount != 0 && shardID <= shardCount) {
			identity +=
				"\"shard\":[";
			identity +=
					std::to_string(shardID); identity += ",";
			identity +=
					std::to_string(shardCount);
			identity +=
				"],";
		}
		identity +=
				"\"large_threshold\":250"
			"}"
		"}";
		sendL(identity);
	}

	void BaseDiscordClient::sendResume() {
		std::string resume;
		resume.reserve(208);
		resume +=
		"{"
			"\"op\":6,"
			"\"d\":{"
				"\"token\":\""; resume += getToken(); resume += "\","
				"\"session_id\":\""; resume += sessionID; resume += "\","
				"\"seq\":"; resume += std::to_string(lastSReceived); resume +=
			"}"
		"}";
		sendL(resume);
		onResume();
	}

	void BaseDiscordClient::quit(bool isRestarting, bool isDisconnected) {
		if (!isRestarting)
			quiting = true;

#ifdef SLEEPY_VOICE_ENABLED
		//quit all voice connections
		for (VoiceConnection& voiceConnection : voiceConnections)
			voiceConnection.disconnect();
		voiceConnections.clear();
		waitingVoiceContexts.clear();
		voiceContexts.clear();
#endif
		if (heart.isValid()) heart.stop(); //stop heartbeating
		if (!isDisconnected) disconnectWebsocket(1000);
		stopClient();
		if (quiting) onQuit();
	}

	void BaseDiscordClient::restart() {
		quit(true);
		connect(theGateway, this, connection);
		onRestart();
	}

	void BaseDiscordClient::reconnect(const unsigned int status) {
		if (status != 1000) {         //check for a deliberate reconnect
			heartbeatInterval = 0;    //stop heartbeating
			wasHeartbeatAcked = true; //stops the library from spamming discord
		}
		disconnectWebsocket(status);
		if (consecutiveReconnectsCount == 10) getTheGateway();
		if (reconnectTimer.isValid())
			reconnectTimer.stop();
		reconnectTimer = schedule([this]() {
			connect(theGateway, this, connection);
		}, consecutiveReconnectsCount < 50 ? consecutiveReconnectsCount * 5000 : 5000 * 50);
		++consecutiveReconnectsCount;
	}

	void BaseDiscordClient::disconnectWebsocket(unsigned int code, const std::string& reason) {
		disconnect(code, reason, connection);
		onDisconnect();
	}

	bool BaseDiscordClient::sendL(std::string message) {
		if (nextHalfMin <= getEpochTimeMillisecond()) {
			const unsigned int maxMessagesPerMin = 116;
			const unsigned int halfMinMilliseconds = 30000;
			const unsigned int maxMessagesPerHalfMin = maxMessagesPerMin / 2;

			nextHalfMin += halfMinMilliseconds;
			messagesRemaining = maxMessagesPerHalfMin;
		}

		if (--messagesRemaining < 0) {
			messagesRemaining = 0;
			setError(RATE_LIMITED);
			return false;
		}
		send(message, connection);
		return true;
	}

	constexpr unsigned int hash(const char* key, unsigned int i = 0) {
		return !key[i] ? 0 : (hash(key, i + 1) * 31) + key[i] - 'A';
	}

        /**
         * This handler is only responsible to parse the contents of READY.
         * Usually READY is the biggest data we get from discord by far, so read
         * only the necessary data from it as a stream.
         */
        struct MessageHandler : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, MessageHandler> {
          JsonInputStream& _stream;
          int _depth = 0;
          int _array_depth = 0;
          bool _in_d = false;
          bool _in_user = false;
          bool _in_guilds = false;
          bool _in_voice_states = false;
          int _voice_object_depth = 4;
          bool _is_ready = false;
          int _last_s = 0;
          bool _has_s = false;
          std::string _current_key;
          std::string _event_type;
          std::string _guild_id, _guild_name;
          std::string _session_id;
          bool _bot = false;
          Snowflake<User> _user_id;
          VoiceState _cur_vs;
          std::vector<VoiceState> _voice_states;

          explicit MessageHandler(SleepyDiscord::JsonInputStream& is) : _stream(is) {
          }

          bool done() const {
            return _is_ready && !_session_id.empty() && !_user_id.string().empty();
          }

          bool Null() {
            return true;
          }
          bool Bool(bool b) {
            if (_in_user && _depth == 3 && _array_depth == 0 && _current_key == "bot") {
              _bot = b;
            }
            return true;
          }
          bool Int(int i) {
            if (_depth == 1) {
              if (_current_key == "op") {
                return true;
              } else if (_current_key == "s") {
                _last_s = i;
                _has_s = true;
              }
            }
            return true;
          }
          bool Uint(unsigned u) {
            if (_depth == 1) {
              if (_current_key == "op") {
                return true;
              } else if (_current_key == "s") {
                _last_s = static_cast<int>(u);
                _has_s = true;
              }
            }
            return true;
          }
          bool Int64(int64_t) { return true; }
          bool Uint64(uint64_t) { return true; }
          bool Double(double) { return true; }
          bool String(const char* str, SizeType length, bool) {
            if (_depth == 1 && _current_key == "t") {
              _event_type.assign(str, length <= 64 ? length : 0);
              if (length == 5 && std::memcmp(str, "READY", 5) == 0) {
                _is_ready = true;
                _stream.SetCache(false);
                return true;
              }
              return true;
            } else if (_in_d && _depth == 2 && _array_depth == 0 && _current_key == "session_id" && _session_id.empty()) {
              _session_id.assign(str, length);
              return true;
            } else if (_in_d && _depth == 2 && _array_depth == 0 && _current_key == "id" && _guild_id.empty()) {
              _guild_id.assign(str, length);
            } else if (_in_d && _depth == 2 && _array_depth == 0 && _current_key == "name" && _guild_name.empty()) {
              _guild_name.assign(str, length);
            } else if (_in_user && _depth == 3 && _array_depth == 0 && _current_key == "id" && _user_id.string().empty()) {
              _user_id = std::string(str, length);
              return true;
            } else if (_in_voice_states) {
              if (_depth == _voice_object_depth) {
                if (_current_key == "user_id") {
                  _cur_vs.userID = std::string(str, length);
                } else if (_current_key == "channel_id") {
                  _cur_vs.channelID = std::string(str, length);
                } else if (_current_key == "session_id") {
                  _cur_vs.sessionID.assign(str, length);
                }
              } else if (_depth == _voice_object_depth + 1 && _current_key == "nick") {
                _cur_vs.serverMember.nick.assign(str, length);
              } else if (_depth == _voice_object_depth + 2 && _current_key == "username") {
                _cur_vs.serverMember.user.username.assign(str, length);
              }
            }
            return true;
          }
          bool StartObject() {
            ++_depth;
            if (_depth == 2 && _array_depth == 0 && _current_key == "d") {
              _in_d = true;
            } else if (_in_d && !_in_guilds && _depth == 3 && _array_depth == 0 &&
                       _current_key == "user" && _user_id.string().empty()) {
              _in_user = true;
            } else if (_in_voice_states && _depth == _voice_object_depth) {
              _cur_vs = VoiceState();
            }
            return true;
          }
          bool Key(const char* str, SizeType length, bool) {
            _current_key.assign(str, length);
            return true;
          }
          bool EndObject(SizeType) {
            _current_key.clear();
            if (_in_voice_states && _depth == _voice_object_depth) {
              if (!_cur_vs.channelID.string().empty() && !_cur_vs.userID.string().empty() &&
                  _voice_states.size() < 1000) {
                _voice_states.push_back(std::move(_cur_vs));
              }
              _cur_vs = VoiceState();
            }
            if (_in_user && _depth == 3) {
              _in_user = false;
              --_depth;
              return true;
            }
            if (_in_d && _depth == 2) {
              _in_d = false;
            }
            --_depth;
            return true;
          }
          bool StartArray() {
            if (_in_d && _depth == 2 && _array_depth == 0 && _current_key == "guilds") {
              _in_guilds = true;
            } else if (_in_d && !_in_guilds && _depth == 2 && _array_depth == 0 && _current_key == "voice_states") {
              _in_voice_states = true;
              _voice_object_depth = 3;
            } else if (_in_guilds && _depth == 3 && _array_depth == 1 && _current_key == "voice_states") {
              _in_voice_states = true;
              _voice_object_depth = 4;
            }
            ++_array_depth;
            return true;
          }
          bool EndArray(SizeType) {
            _current_key.clear();
            --_array_depth;
            if (_in_voice_states && _depth == _voice_object_depth - 1) {
              _in_voice_states = false;
            } else if (_in_guilds && _depth == 2 && _array_depth == 0) {
              _in_guilds = false;
            }
            return true;
          }
        };

        void BaseDiscordClient::processStream(SleepyDiscord::JsonInputStream& is) {
          is.SetCache(true);
          MessageHandler handler(is);
          rapidjson::Reader reader;
          JsonTokenStream tokens(is);
          rapidjson::ParseResult result = reader.Parse(tokens, handler);
          if (result && handler.done()) {
            if (handler._has_s) {
              lastSReceived = handler._last_s;
            }
            consecutiveReconnectsCount = 0;
            sessionID = handler._session_id;
            bot = handler._bot;
            userID = handler._user_id;
            onReady();
            for (const auto& vs : handler._voice_states) {
              onEditVoiceState(vs);
            }
            ready = true;
          } else if (result && useLightweightServerEvents() &&
                     (handler._event_type == "GUILD_CREATE" || handler._event_type == "GUILD_UPDATE")) {
            if (handler._has_s) lastSReceived = handler._last_s;
            if (!handler._guild_id.empty() && shouldProcessDispatch(handler._event_type)) {
              // Do not instantiate thousands of member/role/channel models
              // merely to update a guild name or initial voice participants.
              Server server;
              server.ID = handler._guild_id;
              server.name = handler._guild_name;
              server.voiceStates.assign(std::make_move_iterator(handler._voice_states.begin()),
                                        std::make_move_iterator(handler._voice_states.end()));
              if (handler._event_type == "GUILD_CREATE") onServer(std::move(server));
              else onEditServer(std::move(server));
            }
            is.SetCache(false);
          } else if (result) {
            // Cache implementations may cap storage. Never parse a truncated
            // message or allocate the remainder of large supplemental events.
            if (handler._has_s) lastSReceived = handler._last_s;
            if (!handler._event_type.empty() && !shouldProcessDispatch(handler._event_type)) {
              is.SetCache(false);
              return;
            }
            if (is.GetCache().size() == is.Tell()) {
              processMessage(is.GetCache());
            } else {
              onError(ERROR_NOTE, "Skipped oversized Gateway event");
            }
            is.SetCache(false);
          } else {
            printf("json parsing error %u\n", result.Code());
          }
        }

	void BaseDiscordClient::processMessage(const std::string &message) {
		rapidjson::Document document;
		rapidjson::ParseResult result = document.Parse(message.c_str(), message.length());
		if (!result || !document.IsObject() || !document.HasMember("op") || !document["op"].IsInt()) {
		  printf("Parsing error %u\n", result.Code());
		  return;
		}
		//json::Values values = json::getValues(message.c_str(),
		//	{ "op", "d", "s", "t" });
		int op = document["op"].GetInt();
		const json::Value nullValue;
		const json::Value& t = document.HasMember("t") ? document["t"] : nullValue;
		//const nonstd::string_view t(tValue.GetString(), tValue.GetStringLength);
		const json::Value& d = document.HasMember("d") ? document["d"] : nullValue;
		switch (op) {
		case DISPATCH:
			if (document.HasMember("s") && document["s"].IsInt()) {
				lastSReceived = document["s"].GetInt();
			}
			consecutiveReconnectsCount = 0; //Successfully connected
			if (!t.IsString()) break;
			switch (hash(json::toStdString(t).c_str())) {
			case hash("READY"                      ): {
				Ready readyData = d;
				sessionID = readyData.sessionID;
				bot = readyData.user.bot;
				userID = readyData.user;
				onReady();
				ready = true;
				} break;
			case hash("RESUMED"                    ): onResumed            (); break;
			case hash("GUILD_CREATE"               ): {
				Server server(d);
				/*if (serverCache)
					serverCache->insert(server);*/
				onServer(server);
				} break;
			case hash("GUILD_DELETE"               ): {
				UnavailableServer server(d);
				/*if (serverCache) {
					findServerInCache(server.ID, [=](ServerCache::iterator& found) {
						serverCache->erase(found);
					});
				}*/
				onDeleteServer(server);
				} break;
			case hash("GUILD_UPDATE"               ): {
				Server server(d);
				/*accessServerFromCache(server.ID, [server](Server& foundServer) {
					foundServer = server;
				});*/
				onEditServer(server);
				} break;
			case hash("GUILD_BAN_ADD"              ): onBan  (d["guild_id"], d["user"]); break;
			case hash("GUILD_BAN_REMOVE"           ): onUnban(d["guild_id"], d["user"]); break;
			case hash("GUILD_INTEGRATIONS_UPDATE"  ):                          break; //to do add this
			case hash("GUILD_MEMBER_ADD"           ): {
				Snowflake<Server> serverID = d["guild_id"];
				ServerMember member(d);
				//appendObjectToCache(serverID, &Server::members, member);
				onMember(serverID, member);
				} break;
			case hash("GUILD_MEMBER_REMOVE"        ): {
				Snowflake<Server> serverID = d["guild_id"];
				User user = d["user"];
				//eraseObjectFromCache(serverID, &Server::members, user.ID);
				onRemoveMember(serverID, user);
				} break;
			case hash("GUILD_MEMBER_UPDATE"        ): {
				Snowflake<Server> serverID = d["guild_id"];
				User user = d["user"];
				std::vector<Snowflake<Role>> roles = json::toArray<Snowflake<Role>>(d["roles"]);
                                auto nickValue = d.FindMember("nick");
                                std::string nick = nickValue != d.MemberEnd() && nickValue->value.IsString() ?
                                             json::toStdString(nickValue->value) : "";
				/*accessObjectFromCache(serverID, &Server::members, user.ID,
					[user, roles, nick](Server&, ServerMember& member) {
						member.user = user;
						member.roles = roles;
						member.nick = nick;
					}
				);*/
				onEditMember(serverID, user, roles, nick);
				} break;
			case hash("GUILD_MEMBERS_CHUNK"        ): onMemberChunk       (d["guild_id"], json::toArray<ServerMember>(d["members"])); break;
			case hash("GUILD_ROLE_CREATE"          ): {
				Snowflake<Server> serverID = d["guild_id"];
				Role role = d["role"];
				//appendObjectToCache(serverID, &Server::roles, role);
				onRole(serverID, role);
				} break;
			case hash("GUILD_ROLE_UPDATE"):
			{
				Snowflake<Server> serverID = d["guild_id"];
				Role role = d["role"];
				/*accessObjectFromCache(serverID, &Server::roles, role.ID,
					[role](Server&, Role& foundRole) {
						foundRole = role;
					}
				);*/
				onEditRole(serverID, role);
			} break;
			case hash("GUILD_ROLE_DELETE"          ): {
				Snowflake<Server> serverID = d["guild_id"];
				Snowflake<Role> roleID = d["role_id"];
				//eraseObjectFromCache(serverID, &Server::roles, roleID);
				onDeleteRole(serverID, roleID);
				} break;
			case hash("GUILD_EMOJIS_UPDATE"        ): onEditEmojis        (d["guild_id"], json::toArray<Emoji>(d["emojis"])); break;
			case hash("CHANNEL_CREATE"             ): {
				Channel channel = d;
				//appendObjectToCache(channel.serverID, &Server::channels, channel);
				onChannel(d);
				} break;
			case hash("CHANNEL_UPDATE"             ): {
				Channel channel = d;
				/*accessObjectFromCache(channel.serverID, &Server::channels, channel.ID,
					[channel](Server&, Channel& foundChannel) {
						foundChannel = channel;
					}
				);*/
				onEditChannel(d);
				} break;
			case hash("CHANNEL_DELETE"             ): {
				Channel channel = d;
				//eraseObjectFromCache(channel.serverID, &Server::channels, channel.ID);
				onDeleteChannel(d);
				} break;
			case hash("CHANNEL_PINS_UPDATE"): {
				const json::Value& lastPinTimeValue = d["last_pin_timestamp"];
				onPinMessage(
					d["channel_id"],
					lastPinTimeValue.IsString() ?
					json::toStdString(d["last_pin_timestamp"]) : ""
				);
			} break;
			case hash("PRESENCE_UPDATE"            ): onPresenceUpdate    (d); break;
			case hash("PRESENCES_REPLACE"          ):                          break;
			case hash("USER_UPDATE"                ): onEditUser          (d); break;
			case hash("USER_NOTE_UPDATE"           ): onEditUserNote      (d); break;
			case hash("USER_SETTINGS_UPDATE"       ): onEditUserSettings  (d); break;
			case hash("VOICE_STATE_UPDATE"         ): {
				VoiceState state(d);
#ifdef SLEEPY_VOICE_ENABLED
				if (!waitingVoiceContexts.empty()) {
					auto iterator = find_if(waitingVoiceContexts.begin(), waitingVoiceContexts.end(),
						[this, &state](const VoiceContext* w) {
						return state.channelID == w->channelID &&
						       ((!sessionID.empty() && state.sessionID == sessionID) ||
						        userID.string().empty() || state.userID == userID);
					});
					if (iterator != waitingVoiceContexts.end()) {
						if (!state.userID.string().empty() &&
						    (userID.string().empty() || (!sessionID.empty() && state.sessionID == sessionID))) {
							userID = state.userID;
						}
						VoiceContext& context = **iterator;
						context.sessionID = state.sessionID;
						connectToVoiceIfReady(context);
					}
				}
#endif
				onEditVoiceState(state);
				} break;
			case hash("TYPING_START"               ): onTyping            (d["channel_id"], d["user_id"], json::toInt64(d["timestamp"]) * 1000); break;
			case hash("MESSAGE_CREATE"             ): onMessage           (d); break;
			case hash("MESSAGE_UPDATE"             ): onEditMessage       (d); break;
			case hash("MESSAGE_DELETE"             ): onDeleteMessages    (d["channel_id"], { d["id"] }); break;
			case hash("MESSAGE_DELETE_BULK"        ): onDeleteMessages    (d["channel_id"], json::toArray<Snowflake<Message>>(d["ids"])); break;
			case hash("VOICE_SERVER_UPDATE"        ): {
				VoiceServerUpdate voiceServer(d);
#ifdef SLEEPY_VOICE_ENABLED
				if (!waitingVoiceContexts.empty() && !voiceServer.endpoint.empty() && !voiceServer.token.empty()) {
					auto iterator = find_if(waitingVoiceContexts.begin(), waitingVoiceContexts.end(),
						[&voiceServer](const VoiceContext* w) {
						return voiceServer.serverID == w->serverID;
					});
					if (iterator != waitingVoiceContexts.end()) {
						VoiceContext& context = **iterator;
						context.token    = voiceServer.token;
						context.endpoint = voiceServer.endpoint;
						connectToVoiceIfReady(context);
					}
				}
#endif
				onEditVoiceServer(voiceServer);
				} break;
			case hash("GUILD_SYNC"                 ): onServerSync        (d); break;
			case hash("RELATIONSHIP_ADD"           ): onRelationship      (d); break;
			case hash("RELATIONSHIP_REMOVE"        ): onDeleteRelationship(d); break;
			case hash("MESSAGE_REACTION_ADD"       ): onReaction          (d["user_id"], d["channel_id"], d["message_id"], d["emoji"]); break;
			case hash("MESSAGE_REACTION_REMOVE"    ): onDeleteReaction    (d["user_id"], d["channel_id"], d["message_id"], d["emoji"]); break;
			case hash("MESSAGE_REACTION_REMOVE_ALL"): onDeleteAllReaction (d["guild_id"], d["channel_id"], d["message_id"]); break;
			default:
				setError(EVENT_UNKNOWN);
				onError(ERROR_NOTE, json::toStdString(t));
				break;
			}
			onDispatch(d);
		break;
		case HELLO:
			if (d.IsObject() && d.HasMember("heartbeat_interval")) {
				heartbeatInterval = json::toInt(d["heartbeat_interval"]);
			}
			lastHeartbeat = 0;
			wasHeartbeatAcked = true;
			heartbeat();
			if (!ready) sendIdentity();
			else sendResume();
			if (reconnectTimer.isValid())
				reconnectTimer.stop();
			break;
		case RECONNECT:
			reconnect();
			break;
		case INVALID_SESSION:
			if (json::toBool(d) == true) {
				schedule(&BaseDiscordClient::sendResume, 2500);
			} else {
				sessionID = "";
				schedule(&BaseDiscordClient::sendIdentity, 2500);
			}
			break;
		case HEARTBEAT_ACK:
			wasHeartbeatAcked = true;
			onHeartbeatAck();
			break;
		}
	}

	void BaseDiscordClient::processCloseCode(const int16_t code) {
		setError(code);

		switch (code) {
		case 4013:
			if (!bot) {
				bot = true;
				break;
			}
			return quit(false, true);
		case 4014:
			return quit(false, true);
		//Just reconnect
		case 1006:
		case UNKNOWN_ERROR:
		case UNKNOWN_OPCODE:
		case DECODE_ERROR:
		case NOT_AUTHENTICATED:
		case ALREADY_AUTHENTICATED:
		case INVALID_SEQ:
		case RATE_LIMITED:
		case SESSION_TIMEOUT:
		default:
			break;

		case 1000:
			if (!isQuiting())
				break;
			//else fall through

		//Might be Unrecoveralbe
		//We may need to stop to prevent a restart loop.
		case AUTHENTICATION_FAILED:
		case INVALID_SHARD:
		case SHARDING_REQUIRED:
			return quit(false, true);
			break;
		}
		reconnect(1001);
	}

	void BaseDiscordClient::heartbeat() {
		if (heartbeatInterval <= 0 || isQuiting()) return; //sanity test

		//if time and timer are out of sync, trust time
		time_t currentTime = getEpochTimeMillisecond();
		time_t nextHeartbest;
		if (currentTime < (nextHeartbest = lastHeartbeat + heartbeatInterval)) {
			heart = schedule(&BaseDiscordClient::heartbeat, nextHeartbest - currentTime);
			return;
		}

		if (!wasHeartbeatAcked) {
			reconnect(1001);
		} else {
			sendHeartbeat();
		}

		lastHeartbeat = currentTime;

		heart = schedule(&BaseDiscordClient::heartbeat, heartbeatInterval);
	}

	void BaseDiscordClient::sendHeartbeat() {
		std::string str = std::to_string(lastSReceived);
		std::string heartbeat;
		//The number 18 comes from 1 plus the length of {\"op\":1,\"d\":}
		heartbeat.reserve(18 + str.length());
		heartbeat +=
			"{"
				"\"op\":1,"
				"\"d\":"; heartbeat += str; heartbeat +=
			"}";
		sendL(heartbeat);
		wasHeartbeatAcked = false;
		onHeartbeat();
	}

	//
	//Voice
	//

#ifdef SLEEPY_VOICE_ENABLED

	VoiceContext& BaseDiscordClient::createVoiceContext(Snowflake<Server> server, Snowflake<Channel> channel, BaseVoiceEventHandler * eventHandler) {
		Snowflake<Server> serverTarget = server != "" ? server : getChannel(channel).cast().serverID;
		voiceContexts.push_front({ serverTarget, channel, eventHandler });
		return voiceContexts.front();
	}

	void BaseDiscordClient::connectToVoiceChannel(VoiceContext& voiceContext, VoiceMode settings) {
		waitingVoiceContexts.emplace_front(&voiceContext);
		std::string voiceState;
		/*The number 131 came from the number of letters in this string:
		  {"op": 4,"d" : {"guild_id": "18446744073709551615",
		  "channel_id" : "18446744073709551615","self_mute" : false,"self_deaf" : false}}
		  plus one
		*/
		voiceState.reserve(131);  //remember to update this when making changes to voice status
		voiceState +=
			"{"
				"\"op\": 4,"
				"\"d\": {"
					"\"guild_id\": \""; voiceState += voiceContext.serverID; voiceState += "\","
					"\"channel_id\""": \""; voiceState += voiceContext.channelID; voiceState += "\","
					"\"self_mute\"" ": "; voiceState += settings & mute   ? "true" : "false"; voiceState += ","
					"\"self_deaf\"" ": "; voiceState += settings & deafen ? "true" : "false"; voiceState +=
				"}"
			"}";
		sendL(voiceState);
		/*Discord will response by sending a VOICE_STATE_UPDATE and a
		  VOICE_SERVER_UPDATE payload. Take a look at processMessage
		  function at case VOICE_STATE_UPDATE and voiceServerUpdate
		  */
	}

	VoiceContext& BaseDiscordClient::connectToVoiceChannel(Snowflake<Server> server, Snowflake<Channel> channel, VoiceMode settings) {
		VoiceContext& target = createVoiceContext(server, channel);
		connectToVoiceChannel(target, settings);
		return target;
	}

	void BaseDiscordClient::connectToVoiceIfReady(VoiceContext& context) {
		if (context.endpoint == "" || context.sessionID == "") //check that we are ready
			return;

		// Preserve explicit voice server port (e.g. :2096, :2083, :443) required by Discord Voice v8;
		// only strip legacy plain-HTTP :80 suffix if present.
		std::string& givenEndpoint = context.endpoint;
		if (givenEndpoint.size() > 3 && givenEndpoint.compare(givenEndpoint.size() - 3, 3, ":80") == 0) {
			givenEndpoint = givenEndpoint.substr(0, givenEndpoint.size() - 3);
		}

		std::string endpoint;
		endpoint.reserve(12 + givenEndpoint.length());
		endpoint += "wss://";
		endpoint += givenEndpoint;
		endpoint += "/?v=8";

		//Add a new connection to the list of connections
		voiceConnections.emplace_front( this, context );
		VoiceConnection& voiceConnection = voiceConnections.front();

		const bool connected = connect(endpoint, &voiceConnection, voiceConnection.connection);

		//remove from wait list
		waitingVoiceContexts.remove(&context);
		if (!connected) voiceConnection.processCloseCode(1006);
	}

	void BaseDiscordClient::removeVoiceConnectionAndContext(VoiceConnection & connection) {
		connection.disconnect();
		VoiceContext* context = &connection.getContext();
		waitingVoiceContexts.remove(context);
		voiceConnections.remove_if(
			[&connection](VoiceConnection& right) {
				return connection == right;
			}
		);
		voiceContexts.remove_if(
			[context](const VoiceContext& right) {
				return &right == context;
			}
		);
	}

	void BaseDiscordClient::disconnectAllVoiceChannels() {
		for (auto& context : voiceContexts) {
			auto active = std::find_if(voiceConnections.begin(), voiceConnections.end(),
				[&context](VoiceConnection& voice) { return &voice.getContext() == &context; });
			if (active == voiceConnections.end()) {
				sendL("{\"op\":4,\"d\":{\"guild_id\":\"" + context.serverID.string() +
				      "\",\"channel_id\":null,\"self_mute\":false,\"self_deaf\":false}}");
			}
		}
		for (auto& voice : voiceConnections) voice.disconnect();
		voiceConnections.clear();
		waitingVoiceContexts.clear();
		voiceContexts.clear();
	}

#endif

	const time_t BaseDiscordClient::getEpochTimeMillisecond() {
		auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now());
		return ms.time_since_epoch().count();
	}

	const std::string BaseDiscordClient::getEditPositionString(const std::vector<std::pair<std::string, uint64_t>>& positions) {
		std::vector<std::string> params(positions.size());
		for (auto& value : positions) {
			params.push_back(json::createJSON({
				{ "id", json::string(value.first) },
				{ "position", json::UInteger(value.second) }
			}));
		}
		return json::createJSONArray(params);
	}

	Route::Route(const std::string route, const std::initializer_list<std::string>& _values)
		: path(route), values(_values)
	{
		constexpr char channelIDIdentifier[] = "channel.id";
		constexpr char serverIDIdentifier[] = "guild.id";

		size_t targetSize = path.length();
		for (std::string replacement : values)
			targetSize += replacement.length();
		_url.reserve(targetSize);

		//In the future, use string view

		size_t offset = 0;
		for (std::string replacement : values) {
			const size_t start = path.find('{', offset);
			const size_t end = path.find('}', start);

			//the +1 and -1 removes the { and }
			const std::string identifier = path.substr(start + 1, end - start - 1);

			if (identifier == channelIDIdentifier)
				channelID = replacement;
			else if (identifier == serverIDIdentifier)
				serverID = replacement;

			_url += path.substr(offset, start - offset);
			_url += replacement;
			offset = end + 1; //the +1 removes the }
		}
		_url += path.substr(offset, path.length() - offset);

	}

	Route::Route(const char* route) : Route(route, {}) {}

	const std::string Route::bucket(RequestMethod method) {
		std::string target;
		std::string methodString = std::to_string(method);
		target.reserve(methodString.length() + channelID.string().length()
			+ serverID.string().length() + path.length());
		target += methodString;
		target += channelID;
		target += serverID;
		target += path;
		return target;
	}
}
