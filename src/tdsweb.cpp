#include <tdscpp.h>
#include <wscpp.h>
#include <string>
#include <iostream>
#include <stdint.h>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

static const string DB_SERVER = "luthien"; // FIXME - move to command line
static const uint16_t PORT = 52441; // FIXME - move to command line?
static const unsigned int BACKLOG = 10;

static void send_error(ws::client_thread& ct, const string& msg) {
    json j;

    j["type"] = "error";
    j["message"] = msg;

    ct.send(j.dump());
}

static void msg_handler(ws::client_thread& ct, const string& msg) {
    try {
        json j = json::parse(msg);

        if (j.count("type") == 0)
            throw runtime_error("No message type given.");

        string type = j["type"];

        if (type == "login") {
            // FIXME
            throw runtime_error(u8"¯\\_(ツ)_/¯");
        } else
            throw runtime_error("Unrecognized message type \"" + type + "\".");
    } catch (const exception& e) {
        send_error(ct, e.what());
    }
}

static void conn_handler(ws::client_thread& ct) {
    cout << "Client connected." << endl;
    // FIXME
}

static void disconn_handler(ws::client_thread& ct) {
    cout << "Client disconnected." << endl;
    // FIXME
}

static void init() {
    ws::server serv(PORT, BACKLOG, msg_handler, conn_handler, disconn_handler);

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
