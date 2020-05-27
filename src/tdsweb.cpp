#include <tdscpp.h>
#include <wscpp.h>
#include <string>
#include <iostream>
#include <stdint.h>
#include <nlohmann/json.hpp>
#include <xlcpp.h>
#include "base64.h"

#ifdef __MINGW32__
#include "mingw.thread.h"
#include "mingw.shared_mutex.h"
#else
#include <thread>
#include <shared_mutex>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;
using json = nlohmann::json;

static const string DB_APP = "tdsweb";
static const unsigned int BACKLOG = 10;

static void send_error(ws::client_thread& ct, const string& msg) {
    json j;

    j["type"] = "error";
    j["message"] = msg;

    ct.send(j.dump());
}

class client {
public:
    client(ws::client_thread& ct, const string& server) : ct(ct), server(server) { }

    ~client() {
        if (query_thread) {
            query_thread->join();
            delete query_thread;
        }
    }

    void login(const json& j);
    void logout();
    void query(const json& j);
    void cancel();
    void change_database(const json& j);
    void ping();

    void msg_handler(const string_view& server, const string_view& message, const string_view& proc_name,
         const string_view& sql_state, int32_t msgno, int32_t line_number, int16_t state, uint8_t priv_msg_type,
         uint8_t severity, int oserr);
    void tbl_handler(const vector<pair<string, tds::server_type>>& columns);
    void row_handler(const vector<tds::Field>& columns);
    void row_count_handler(unsigned int count);

    ws::client_thread& ct;
    string server;
    shared_ptr<tds::Conn> tds;
    thread* query_thread = nullptr;
    bool cancelled = false;
    unique_ptr<xlcpp::workbook> excel;
    xlcpp::sheet* sheet;
};

void client::login(const json& j) {
    if (j.count("username") == 0)
        throw runtime_error("Username not provided.");

    if (j.count("password") == 0)
        throw runtime_error("Password not provided.");

    auto mh = bind(&client::msg_handler, this, placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4,
                   placeholders::_5, placeholders::_6, placeholders::_7, placeholders::_8, placeholders::_9, placeholders::_10);
    auto mh2 = bind(&client::tbl_handler, this, placeholders::_1);
    auto mh3 = bind(&client::row_handler, this, placeholders::_1);
    auto mh4 = bind(&client::row_count_handler, this, placeholders::_1);

    tds.reset(new tds::Conn(server, j["username"], j["password"], DB_APP, mh, nullptr, mh2, mh3, mh4));

    string cur_db;

    {
        tds::Query sq(*tds, "SELECT DB_NAME()");

        sq.fetch_row();

        cur_db = sq[0];
    }

    vector<json> dbs;

    {
        tds::Query sq(*tds, "SELECT name FROM sys.databases ORDER BY name");

        while (sq.fetch_row()) {
            dbs.push_back(sq[0]);
        }
    }

    ct.send(json{
        {"type", "login"},
        {"success", true},
        {"server", server},
        {"username", j["username"]},
        {"database", cur_db},
        {"databases", dbs}
    }.dump());
}

void client::logout() {
    if (!tds)
        throw runtime_error("Can't logout as not logged in.");

    tds->cancel();

    tds.reset();

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

    if (query_thread)
        throw runtime_error("Query already running.");

    if (j.count("export") > 0 && j.at("export") == "excel") {
        excel.reset(new xlcpp::workbook());
        sheet = &excel->add_sheet("Sheet1");
    }

    query_thread = new thread([&](string q) {
        bool failed = false;

        shared_ptr<tds::Conn> tds2 = tds;

        cancelled = false;

        // FIXME - what about question marks?

        try {
            try {
                tds2->run(q);
            } catch (...) {
                // swallow exception, so we don't return "tds_submit_execute failed" to client
                failed = true;
            }

            if (failed && tds2->is_dead())
                logout();
            else if (!failed || tds == tds2) { // don't send if stopping because logged out
                if (excel) {
                    ct.send(json{
                        {"type", "query_finished"},
                        {"mime", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
                        {"filename", "results.xlsx"},
                        {"data", base64_encode(excel->data())}
                    }.dump());

                    excel.reset(nullptr);
                } else {
                    ct.send(json{
                        {"type", "query_finished"}
                    }.dump());
                }
            }
        } catch (const exception& e) {
            send_error(ct, e.what());
        }

        query_thread->detach();

        delete query_thread;
        query_thread = nullptr;
    }, j.at("query"));
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

void client::tbl_handler(const vector<pair<string, tds::server_type>>& columns) {
    vector<json> ls;

    if (cancelled)
        return;

    if (excel) {
        // FIXME - add blank row if not first table

        auto& row = sheet->add_row();

        for (const auto& col : columns) {
            auto& c = row.add_cell(get<0>(col));
            c.set_font("Arial", 10, true);
        }
    } else {
        for (const auto& col : columns) {
            ls.emplace_back(json{
                {"name", get<0>(col)},
                {"type", get<1>(col)}
            });
        }

        ct.send(json{
            {"type", "table"},
            {"columns", ls}
        }.dump());
    }
}

void client::row_handler(const vector<tds::Field>& columns) {
    vector<json> ls;

    if (cancelled)
        return;

    if (excel) {
        auto& row = sheet->add_row();

        for (const auto& col : columns) {
            if (col.is_null())
                row.add_cell("NULL"); // FIXME - make italic?
            else {
                switch (col.type) {
                    case tds::server_type::SYBINTN:
                    case tds::server_type::SYBINT1:
                    case tds::server_type::SYBINT2:
                    case tds::server_type::SYBINT4:
                        row.add_cell((int64_t)col);
                    break;

                    case tds::server_type::SYBDATETIME:
                    case tds::server_type::SYBDATETIMN:
                    {
                        auto dt = (tds::DateTime)col;

                        row.add_cell(xlcpp::datetime{dt.d.year(), dt.d.month(), dt.d.day(), dt.t.h, dt.t.m, dt.t.s});
                        break;
                    }

                    case tds::server_type::SYBMSDATE:
                    {
                        auto d = (tds::Date)col;

                        row.add_cell(xlcpp::date{d.year(), d.month(), d.day()});
                        break;
                    }

                    case tds::server_type::SYBMSTIME:
                    {
                        auto t = (tds::Time)col;

                        row.add_cell(xlcpp::time{t.h, t.m, t.s});
                        break;
                    }

                    case tds::server_type::SYBFLT8:
                    case tds::server_type::SYBFLTN:
                    case tds::server_type::SYBREAL:
                        row.add_cell((double)col);
                    break;

                    case tds::server_type::SYBBIT:
                    case tds::server_type::SYBBITN:
                        row.add_cell((int)col != 0);
                    break;

                    default:
                        row.add_cell((string)col);
                }
            }
        }
    } else {
        for (const auto& col : columns) {
            if (col.is_null())
                ls.emplace_back(nullptr);
            else
                ls.emplace_back((string)col);
        }

        ct.send(json{
            {"type", "row"},
            {"columns", ls}
        }.dump());
    }
}

void client::row_count_handler(unsigned int count) {
    ct.send(json{
        {"type", "row_count"},
        {"count", count}
    }.dump());
}

void client::cancel() {
    if (tds) {
        cancelled = true;
        tds->cancel();
    }
}

void client::change_database(const json& j) {
    if (!tds)
        throw runtime_error("Not logged in.");

    if (j.count("database") == 0)
        throw runtime_error("No database given.");

    string db = j["database"];

    tds->run("USE " + tds::escape(db));
}

void client::ping() {
    ct.send(json{
        {"type", "pong"}
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
        else if (type == "cancel")
            c.cancel();
        else if (type == "change_database")
            c.change_database(j);
        else if (type == "ping")
            c.ping();
        else
            throw runtime_error("Unrecognized message type \"" + type + "\".");
    } catch (const exception& e) {
        send_error(ct, e.what());
    }
}

static void disconn_handler(ws::client_thread& ct) {
    if (ct.context) {
        auto c = (client*)ct.context;

        delete c;
    }
}

static void init(const string& server, uint16_t port) {
    ws::server serv(port, BACKLOG, ws_recv, [&](ws::client_thread& ct) {
        ct.context = new client(ct, server);
    }, disconn_handler);

    serv.start();
}

#ifdef _WIN32
static __inline string utf16_to_utf8(const u16string_view& s) {
    string ret;

    if (s.empty())
        return "";

    auto len = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)s.data(), s.length(), nullptr, 0,
                                   nullptr, nullptr);

    if (len == 0)
        throw runtime_error("WideCharToMultiByte 1 failed.");

    ret.resize(len);

    len = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)s.data(), s.length(), ret.data(), len,
                              nullptr, nullptr);

    if (len == 0)
        throw runtime_error("WideCharToMultiByte 1 failed.");

    return ret;
}

class last_error : public exception {
public:
    last_error(const string_view& function, int le) {
        string nice_msg;

        {
            char16_t* fm;

            if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                le, 0, reinterpret_cast<LPWSTR>(&fm), 0, nullptr)) {
                try {
                    u16string_view s = fm;

                    while (!s.empty() && (s[s.length() - 1] == u'\r' || s[s.length() - 1] == u'\n')) {
                        s.remove_suffix(1);
                    }

                    nice_msg = utf16_to_utf8(s);
                } catch (...) {
                    LocalFree(fm);
                    throw;
                }

                LocalFree(fm);
                }
        }

        msg = string(function) + " failed (error " + to_string(le) + (!nice_msg.empty() ? (", " + nice_msg) : "") + ").";
    }

    const char* what() const noexcept {
        return msg.c_str();
    }

private:
    string msg;
};

static void service_install() {
    SC_HANDLE sc_manager;
    wstring pathw;

    {
        WCHAR path[MAX_PATH];

        if (GetModuleFileNameW(nullptr, path, sizeof(path) / sizeof(WCHAR)) == 0)
            throw last_error("GetModuleFileName", GetLastError());

        pathw = L"\""s + path + L"\" service"s;
    }

    sc_manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!sc_manager)
        throw last_error("OpenSCManager", GetLastError());

    try {
        auto service = CreateServiceW(sc_manager, L"TDSweb", L"TDSweb", SERVICE_QUERY_STATUS,
                                      SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                                      pathw.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);

        if (!sc_manager) {
            CloseServiceHandle(service);
            throw last_error("CreateService", GetLastError());
        }

        CloseServiceHandle(service);
    } catch (...) {
        CloseServiceHandle(sc_manager);
        throw;
    }

    CloseServiceHandle(sc_manager);
}
#endif

int main(int argc, char* argv[]) {
    try {
#ifdef _WIN32
        if (argc == 2 && !strcmp(argv[1], "install")) {
            service_install();
            return 0;
        }
#endif
        if (argc < 3) {
            fprintf(stderr, "Usage: tdsweb server port\n");
            return 1;
        }

        auto port = stoul(argv[2]);

        if (port > 0xffff)
            throw runtime_error("Port out of range.");

        init(argv[1], port);
    } catch (const exception& e) {
        cerr << e.what() << endl;
        return 1;
    }

    return 0;
}
