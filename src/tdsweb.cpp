#include <tdscpp.h>
#include <wscpp.h>
#include <string>
#include <iostream>
#include <stdint.h>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

static const string DB_SERVER = "luthien"; // FIXME - move to command line
static const string DB_APP = "tdsweb";
static const uint16_t PORT = 52441; // FIXME - move to command line?
static const unsigned int BACKLOG = 10;

static void send_error(ws::client_thread& ct, const string& msg) {
    json j;

    j["type"] = "error";
    j["message"] = msg;

    ct.send(j.dump());
}

class client {
public:
    client(ws::client_thread& ct) : ct(ct) { }

    ~client() {
        if (tds)
            delete tds;
    }

    void login(const json& j);
    void logout();
    void query(const json& j);

    void msg_handler(const string_view& server, const string_view& message, const string_view& proc_name,
         const string_view& sql_state, int32_t msgno, int32_t line_number, int16_t state, uint8_t priv_msg_type,
         uint8_t severity, int oserr);

    ws::client_thread& ct;
    tds::Conn* tds = nullptr;
};

void client::login(const json& j) {
    if (j.count("username") == 0)
        throw runtime_error("Username not provided.");

    if (j.count("password") == 0)
        throw runtime_error("Password not provided.");

    if (tds)
        delete tds;

    auto mh = bind(&client::msg_handler, this, placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4,
                   placeholders::_5, placeholders::_6, placeholders::_7, placeholders::_8, placeholders::_9, placeholders::_10);

    tds = new tds::Conn(DB_SERVER, j["username"], j["password"], DB_APP, mh);

    ct.send(json{
        {"type", "login"},
        {"success", true},
        {"server", DB_SERVER},
        {"username", j["username"]}
    }.dump());
}

void client::logout() {
    if (!tds)
        throw runtime_error("Can't logout as not logged in.");

    delete tds;
    tds = nullptr;

    ct.send(json{
        {"type", "logout"},
        {"success", true}
    }.dump());
}

void client::query(const json& j) {
    if (j.count("query") == 0)
        throw runtime_error("No query given.");

    if (!tds)
        throw runtime_error("Not logged in.");

    string q = j["query"];

    // FIXME - what about question marks?

    try {
        tds->run(q);
    } catch (...) {
        // so we don't return "tds_submit_execute" failed to client
    }
}

void client::msg_handler(const string_view& server, const string_view& message, const string_view& proc_name,
                         const string_view& sql_state, int32_t msgno, int32_t line_number, int16_t state, uint8_t priv_msg_type,
                         uint8_t severity, int oserr) {
    ct.send(json{
        {"type", "message"},
        {"server", server},
        {"message", message},
        {"proc_name", proc_name},
        {"sql_state", sql_state},
        {"msgno", msgno},
        {"line_number", line_number},
        {"state", state},
        {"priv_msg_type", priv_msg_type},
        {"severity", severity},
        {"oserr", oserr}
    }.dump());
}

static void ws_recv(ws::client_thread& ct, const string& msg) {
    try {
        json j = json::parse(msg);

        if (j.count("type") == 0)
            throw runtime_error("No message type given.");

        auto& c = *(client*)ct.context;

        string type = j["type"];

        if (type == "login")
            c.login(j);
        else if (type == "logout")
            c.logout();
        else if (type == "query")
            c.query(j);
        else
            throw runtime_error("Unrecognized message type \"" + type + "\".");
    } catch (const exception& e) {
        send_error(ct, e.what());
    }
}

static void conn_handler(ws::client_thread& ct) {
    ct.context = new client(ct);
}

static void disconn_handler(ws::client_thread& ct) {
    if (ct.context) {
        auto c = (client*)ct.context;

        delete c;
    }
}

static void init() {
    ws::server serv(PORT, BACKLOG, ws_recv, conn_handler, disconn_handler);

    serv.start();
}

int main() {
    try {
        init();
    } catch (const exception& e) {
        cerr << e.what() << endl;
        return 1;
    }

    return 0;
}
