#include "crow.h"
#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

std::string jwt(const std::string& account) { return "ok" + account; }
    
std::string account_from_request(const crow::request& req) {
    crow::query_string body_args(req.body);
    if (const char* username = body_args.get("username")) {
        return username;
    }
    if (const char* email = body_args.get("email")) {
        return email;
    }
    return "anon@keradev.com";
}

std::string display_name_from_account(const std::string& account) {
    const auto at = account.find('@');
    if (at != std::string::npos) {
        return account.substr(0, at);
    }
    return account;
}

std::string get_current_utc_time_string() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm buf;
    gmtime_s(&buf, &time_t_now);

    std::stringstream ss;
    ss << std::put_time(&buf, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

crow::json::wvalue make_public_account(const std::string& account_id) {
    const std::string display_name = display_name_from_account(account_id);
    const std::string email = account_id.find('@') != std::string::npos
        ? account_id
        : account_id + "@keradev.com";

    crow::json::wvalue response;
    response["id"] = account_id;
    response["displayName"] = display_name;
    response["name"] = "kera";
    response["email"] = email;
    response["failedLoginAttempts"] = 0;
    response["lastLogin"] = get_current_utc_time_string();
    response["numberOfDisplayNameChanges"] = 0;
    response["ageGroup"] = "UNKNOWN";
    response["headless"] = false;
    response["country"] = "US";
    response["lastName"] = "Server";
    response["preferredLanguage"] = "en";
    response["canUpdateDisplayName"] = false;
    response["tfaEnabled"] = false;
    response["emailVerified"] = true;
    response["minorVerified"] = false;
    response["minorExpected"] = false;
    response["minorStatus"] = "NOT_MINOR";
    response["cabinedMode"] = false;
    response["hasHashedEmail"] = false;
    return response;
}

crow::json::wvalue make_lightswitch_status(bool include_allowed_actions) {
    crow::json::wvalue launcher;
    launcher["appName"] = "Fortnite";
    launcher["catalogItemId"] = "4fe75bbc5a674f4f9b356b5c90567da5";
    launcher["namespace"] = "fn";

    crow::json::wvalue::list catalog_ids;
    catalog_ids.push_back(crow::json::wvalue("a7f138b2e51945ffbfdacc1af0541053"));

    crow::json::wvalue status;
    status["serviceInstanceId"] = "fortnite";
    status["status"] = "UP";
    status["message"] = include_allowed_actions ? "fortnite is up." : "Fortnite is online";
    status["maintenanceUri"] = crow::json::wvalue();
    status["overrideCatalogIds"] = std::move(catalog_ids);
    status["banned"] = false;
    status["launcherInfoDTO"] = std::move(launcher);

    if (include_allowed_actions) {
        crow::json::wvalue::list actions;
        actions.push_back(crow::json::wvalue("PLAY"));
        actions.push_back(crow::json::wvalue("DOWNLOAD"));
        status["allowedActions"] = std::move(actions);
    } else {
        status["allowedActions"] = crow::json::wvalue::list();
    }

    return status;
}

std::string read_text_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

std::string resolve_profile_path(const std::string& profile_id) {
    const std::string filename = profile_id + ".json";
    const std::vector<std::string> candidates = {
        "profiles/" + filename,
        "../profiles/" + filename,
        "../../profiles/" + filename,
    };

    for (const auto& path : candidates) {
        std::ifstream file(path);
        if (file) {
            return path;
        }
    }

    return candidates.front();
}

int parse_query_int(const crow::query_string& params, const char* key, int default_value) {
    if (const char* value = params.get(key)) {
        try {
            return std::stoi(value);
        }
        catch (...) {
            return default_value;
        }
    }
    return default_value;
}



crow::response make_mcp_profile_response(
    const std::string& account_id,
    const std::string& profile_id,
    int query_revision)
{
    const std::string profile_path = resolve_profile_path(profile_id);
    const std::string profile_text = read_text_file(profile_path);
    if (profile_text.empty()) {
        crow::json::wvalue error_body;
        error_body["errorCode"] = "errors.com.kera.common.not_found";
        error_body["errorMessage"] = "Profile not found: " + profile_id;
        error_body["numericErrorCode"] = 1004;
        return crow::response(404, error_body);
    }

    const auto profile_r = crow::json::load(profile_text);
    if (profile_r.t() != crow::json::type::Object) {
        crow::json::wvalue error_body;
        error_body["errorCode"] = "errors.com.kera.common.server_error";
        error_body["errorMessage"] = "Invalid profile data: " + profile_id;
        return crow::response(500, error_body);
    }

    crow::json::wvalue profile(profile_r);
    profile["accountId"] = account_id;

    const int base_revision = profile_r.has("rvn") ? profile_r["rvn"].i() : 0;
    const int command_revision = profile_r.has("commandRevision") ? profile_r["commandRevision"].i() : 0;

    crow::json::wvalue::list profile_changes;
    if (query_revision != base_revision) {
        crow::json::wvalue change;
        change["changeType"] = "fullProfileUpdate";
        change["profile"] = std::move(profile);
        profile_changes.push_back(std::move(change));
    }

    crow::json::wvalue response;
    response["profileRevision"] = base_revision;
    response["profileId"] = profile_id;
    response["profileChangesBaseRevision"] = base_revision;
    response["profileChanges"] = std::move(profile_changes);
    response["profileCommandRevision"] = command_revision;
    response["serverTime"] = get_current_utc_time_string();
    response["responseVersion"] = 1;
    return crow::response(response);
}

int main()
{
    crow::SimpleApp app;

    CROW_ROUTE(app, "/fortnite/api/storefront/v2/keychain")
        .methods(crow::HTTPMethod::GET)
        ([]() {
        crow::response res(read_text_file("keychain.json"));
        res.set_header("Content-Type", "application/json");
        return res;
            });
    // ok so
    // ok

    CROW_ROUTE(app, "/account/api/oauth/sessions/kill")
        .methods(crow::HTTPMethod::POST, crow::HTTPMethod::Delete)([](const crow::request& req) {
        return crow::response(204);
            });

    CROW_ROUTE(app, "/account/api/oauth/sessions/kill/<string>")
        .methods(crow::HTTPMethod::Delete)([](std::string token) {
        return crow::response(204);
            });

    CROW_ROUTE(app, "/account/api/public/account/<string>/externalAuths")
        .methods(crow::HTTPMethod::Get)([](std::string account_id) {
        crow::json::wvalue response = crow::json::wvalue::list();
        return crow::response(response);
            });

    CROW_ROUTE(app, "/account/api/public/account/<string>")
        .methods(crow::HTTPMethod::Get)([](std::string account_id) {
        return crow::response(make_public_account(account_id));
            });

    CROW_ROUTE(app, "/account/api/epicdomains/ssodomains")
        .methods(crow::HTTPMethod::GET)([]() {
        crow::json::wvalue response = crow::json::wvalue::list();
        return crow::response(response);
            });

    CROW_ROUTE(app, "/fortnite/api/v2/versioncheck/<string>")
        .methods(crow::HTTPMethod::Get)([](std::string platform) {
        crow::json::wvalue response;
        response["type"] = "NO_UPDATE";
        return crow::response(response);
            });

    CROW_ROUTE(app, "/eulatracking/api/shared/agreements/fn")
        .methods(crow::HTTPMethod::GET)([]() {
        crow::json::wvalue response;
        response["response"] = "OK";
        return crow::response(response);
            });

    CROW_ROUTE(app, "/fortnite/api/game/v2/world/info")
        .methods(crow::HTTPMethod::GET)([]() {
        crow::json::wvalue response;
        return crow::response(response);
            });

    CROW_ROUTE(app, "/datarouter/api/v1/public/data")
        .methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        return crow::response(204);
            });

    CROW_ROUTE(app, "/fortnite/api/game/v2/tryPlayOnPlatform/account/<string>")
        .methods(crow::HTTPMethod::POST)([](std::string account_id) {
        crow::response res(200, "true");
        res.set_header("Content-Type", "text/plain");
        return res;
            });

    CROW_ROUTE(app, "/fortnite/api/game/v2/enabled_features")
        .methods(crow::HTTPMethod::Get)([]() {
        crow::json::wvalue response = crow::json::wvalue::list();
        return crow::response(response);
            });

    CROW_ROUTE(app, "/fortnite/api/game/v2/grant_access/<string>")
        .methods(crow::HTTPMethod::POST)([](std::string account_id) {
        return crow::response(204);
            });

    CROW_ROUTE(app, "/fortnite/api/game/v2/twitch/<string>")
        .methods(crow::HTTPMethod::GET)([](std::string account_id) {
        crow::json::wvalue response;
        return crow::response(response);
            });

    CROW_ROUTE(app, "/lightswitch/api/service/bulk/status")
        .methods(crow::HTTPMethod::Get)([]() {
        crow::json::wvalue::list services;
        services.push_back(make_lightswitch_status(true));
        return crow::response(crow::json::wvalue(std::move(services)));
            });

    CROW_ROUTE(app, "/lightswitch/api/service/Fortnite/status")
        .methods(crow::HTTPMethod::Get)([]() {
        return crow::response(make_lightswitch_status(false));
            });


    CROW_ROUTE(app, "/api/v1/user/setting")
        .methods(crow::HTTPMethod::POST)([](const crow::request& req) {
        crow::json::wvalue response;
        response["status"] = "ok";
        return crow::response(response);
            });


    CROW_ROUTE(app, "/unknown")
        .methods(crow::HTTPMethod::GET)([](const crow::request& req) {
        crow::json::wvalue response;
        response["status"] = "jew";
        return crow::response(response);
            });

    CROW_ROUTE(app, "/fortnite/api/cloudstorage/system")
        .methods(crow::HTTPMethod::GET)([]() {
        crow::json::wvalue response_body;
        response_body = crow::json::wvalue::list(); 
        return crow::response(response_body);   
            });

    CROW_ROUTE(app, "/fortnite/api/cloudstorage/user/<string>")
        .methods(crow::HTTPMethod::GET)([](std::string account_id) {
        crow::json::wvalue response_body;
        response_body = crow::json::wvalue::list();
        return crow::response(response_body);
            });

    CROW_ROUTE(app, "/fortnite/api/game/v2/profile/<string>/client/<string>")
        .methods(crow::HTTPMethod::POST)([](const crow::request& req, std::string account_id, std::string) {
        if (!req.url_params.get("profileId")) {
            crow::json::wvalue error_body;
            error_body["error"] = "Profile not defined.";
            return crow::response(404, error_body);
        }

        const std::string profile_id = req.url_params.get("profileId");
        const int query_revision = parse_query_int(req.url_params, "rvn", -1);
        return make_mcp_profile_response(account_id, profile_id, query_revision);
            });

    CROW_ROUTE(app, "/fortnite/api/game/v2/profile/<string>/dedicated_server/<string>")
        .methods(crow::HTTPMethod::POST)([](const crow::request& req, std::string account_id, std::string) {
        const std::string profile_id = req.url_params.get("profileId")
            ? req.url_params.get("profileId")
            : "athena";
        const int query_revision = parse_query_int(req.url_params, "rvn", -1);
        return make_mcp_profile_response(account_id, profile_id, query_revision);
            });

    CROW_ROUTE(app, "/fortnite/api/game/v2/profile/<string>/client/QueryProfile")
        .methods(crow::HTTPMethod::POST)([](const crow::request& req, std::string account_id) {
        if (!req.url_params.get("profileId")) {
            crow::json::wvalue error_body;
            error_body["error"] = "Profile not defined.";
            return crow::response(404, error_body);
        }

        const std::string profile_id = req.url_params.get("profileId");
        const int query_revision = parse_query_int(req.url_params, "rvn", -1);
        return make_mcp_profile_response(account_id, profile_id, query_revision);
            });

    CROW_ROUTE(app, "/fortnite/api/calendar/v1/timeline")
        .methods(crow::HTTPMethod::GET)([]() {
        int season = 13;
        std::string lobby = "LobbySeason" + std::to_string(season);

        crow::json::wvalue response;
        response["cacheIntervalMins"] = 10.0;
        response["currentTime"] = get_current_utc_time_string();
        crow::json::wvalue client_matchmaking;
        client_matchmaking["states"] = crow::json::wvalue::list();
        client_matchmaking["cacheExpire"] = "9999-01-01T00:00:00.000Z";
        crow::json::wvalue client_events;
        client_events["cacheExpire"] = "9999-01-01T00:00:00.000Z";

        crow::json::wvalue event1;
        event1["eventType"] = "EventFlag.Season" + std::to_string(season);
        event1["activeUntil"] = "9999-01-01T00:00:00.000Z";
        event1["activeSince"] = "2020-01-01T00:00:00.000Z";

        crow::json::wvalue event2;
        event2["eventType"] = "EventFlag." + lobby;
        event2["activeUntil"] = "9999-01-01T00:00:00.000Z";
        event2["activeSince"] = "2020-01-01T00:00:00.000Z";

        crow::json::wvalue::list active_events_list;
        active_events_list.push_back(std::move(event1));
        active_events_list.push_back(std::move(event2));

        crow::json::wvalue inner_state;
        inner_state["activeStorefronts"] = crow::json::wvalue::list();
        inner_state["eventNamedWeights"] = crow::json::wvalue();
        inner_state["seasonNumber"] = season;
        inner_state["seasonTemplateId"] = "AthenaSeason:athenaseason" + std::to_string(season);
        inner_state["matchXpBonusPoints"] = 10;
        inner_state["seasonBegin"] = "2020-01-01T00:00:00Z";
        inner_state["seasonEnd"] = "9999-01-01T00:00:00Z";
        inner_state["seasonDisplayedEnd"] = "9999-01-01T00:00:00Z";
        inner_state["weeklyStoreEnd"] = "2023-08-05T23:59:00.000Z";
        inner_state["stwEventStoreEnd"] = "9999-01-01T00:00:00.000Z";
        inner_state["stwWeeklyStoreEnd"] = "9999-01-01T00:00:00.000Z";
        inner_state["dailyStoreEnd"] = "2023-08-05T23:59:00.000Z";

        crow::json::wvalue section_store_ends;
        section_store_ends["Featured"] = "2023-08-05T23:59:00.000Z";
        inner_state["sectionStoreEnds"] = std::move(section_store_ends);

        crow::json::wvalue channel_state;
        channel_state["validFrom"] = "0001-01-01T00:00:00.000Z";
        channel_state["activeEvents"] = std::move(active_events_list);
        channel_state["state"] = std::move(inner_state);

        crow::json::wvalue::list client_events_states_list;
        client_events_states_list.push_back(std::move(channel_state));
        client_events["states"] = std::move(client_events_states_list);

        crow::json::wvalue channels;
        channels["client-matchmaking"] = std::move(client_matchmaking);
        channels["client-events"] = std::move(client_events);

        response["channels"] = std::move(channels);
        return crow::response(response);
            });


    CROW_ROUTE(app, "/account/api/oauth/token")
        .methods(crow::HTTPMethod::POST)([](const crow::request& req) {

        crow::query_string body_args(req.body);
        std::string grant_type = body_args.get("grant_type") ? body_args.get("grant_type") : "";
        std::string account = account_from_request(req);
        const std::string display_name = display_name_from_account(account);
        crow::json::wvalue response_body;

        if (grant_type == "exchange_code") {
            response_body["access_token"] = jwt(account);
            response_body["expires_in"] = 28800;
            response_body["expires_at"] = "9999-01-01T00:00:00.000Z";
            response_body["token_type"] = "bearer";
            response_body["client_id"] = "fortnite";
            response_body["internal_client"] = true;
            response_body["client_service"] = "fortnite";
            response_body["account_id"] = account;
            response_body["displayName"] = display_name;

            return crow::response(response_body);
        }
        else if (grant_type == "client_credentials" || grant_type == "password" || grant_type == "") {
            response_body["access_token"] = jwt(account);
            response_body["token_type"] = "bearer";
            response_body["expires_in"] = 28800;
            response_body["expires_at"] = "9999-12-31T23:59:59.999Z";
            response_body["refresh_token"] = jwt(account);
            response_body["refresh_expires_in"] = 86400;
            response_body["refresh_expires_at"] = "9999-12-31T23:59:59.999Z";
            response_body["nonce"] = "kera";
            response_body["deployment_id"] = "62a9473a2dca46b29ccf17577fcf42d7";
            response_body["organization_id"] = "kerasorganizationidlol";
            response_body["product_id"] = "prod-fn";
            response_body["sandbox_id"] = "fn";
            response_body["account_id"] = account;
            response_body["displayName"] = display_name;
            response_body["scope"] = "basic_profile friends_list openid presence";

            return crow::response(response_body);
        }

        crow::json::wvalue error_body;
        error_body["errorCode"] = "errors.com.epicgames.oauth.unsupported_grant_type";
        error_body["errorMessage"] = "Unsupported grant type: " + grant_type;
        return crow::response(400, error_body);
            });
    // ingame on 13.40 somehow?

    app.port(18080).run();
}