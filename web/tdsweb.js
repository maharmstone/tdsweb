'use strict';

let ws;
let logged_in = false, logging_in = false;
let res_tbody = null;

document.addEventListener("DOMContentLoaded", init);

function change_status(msg, error) {
    let status = document.getElementById("status");

    while (status.hasChildNodes()) { status.removeChild(status.firstChild); }

    status.appendChild(document.createTextNode(msg));

    if (error)
        status.classList.add("error");
    else
        status.classList.remove("error");
}

function recv_login(msg) {
    logging_in = false;

    change_status("Logged in to " + msg.server + " as " + msg.username + ".", false);

    document.getElementById("username").disabled = true;
    document.getElementById("password").disabled = true;
    document.getElementById("login-button").disabled = false;
    document.getElementById("login-button").setAttribute("value", "Logout");
    document.getElementById("query-box").readOnly = false;
    document.getElementById("go-button").disabled = false;
    document.getElementById("stop-button").disabled = true;

    let dbc = document.getElementById("database-changer");

    while (dbc.hasChildNodes()) {
        dbc.removeChild(dbc.firstChild);
    }

    for (let i = 0; i < msg.databases.length; i++) {
        let opt = document.createElement("option");
        opt.appendChild(document.createTextNode(msg.databases[i]));

        if (msg.databases[i] == msg.database)
            opt.setAttribute("selected", "selected");

        dbc.appendChild(opt);
    }

    document.getElementById("database-changer-container").style.display = "";

    logged_in = true;
}

function recv_logout(msg) {
    change_status("Logged out.", false);

    document.getElementById("username").disabled = false;
    document.getElementById("password").disabled = false;
    document.getElementById("login-button").disabled = false;
    document.getElementById("login-button").setAttribute("value", "Login");
    document.getElementById("query-box").readOnly = true;
    document.getElementById("go-button").disabled = true;
    document.getElementById("stop-button").disabled = true;
    document.getElementById("database-changer-container").style.display = "none";

    logged_in = false;
}

function recv_message(msg) {
    let log = document.getElementById("messages");
    let s;

    s = JSON.stringify(msg);

    // FIXME - date and time?

    let p = document.createElement("p");

    if ((msg.msgno == 50000 || msg.msgno == 0) && msg.severity <= 10)
        p.appendChild(document.createTextNode(msg.message));
    else {
        if (msg.severity > 10)
            p.classList.add("error");

        p.appendChild(document.createTextNode("Msg " + msg.msgno + ", Level " + msg.severity + ", State " + msg.state + ", Line " + msg.line_number));
        p.appendChild(document.createElement("br"));
        p.appendChild(document.createTextNode(msg.message));
    }

    log.appendChild(p);

    p.scrollIntoView();
}

function recv_table(msg) {
    let tbl = document.createElement("table");
    let col = msg.columns;

    let thead = document.createElement("thead");

    let tr = document.createElement("tr");

    for (let i = 0; i < col.length; i++) {
        let th = document.createElement("th");

        if (col[i].name == "")
            th.appendChild(document.createTextNode("(No column name)"));
        else
            th.appendChild(document.createTextNode(col[i].name));

        tr.appendChild(th);
    }

    thead.appendChild(tr);
    tbl.appendChild(thead);

    let res = document.getElementById("results");

    res_tbody = document.createElement("tbody");
    tbl.appendChild(res_tbody);

    res.appendChild(tbl);
}

function recv_row(msg) {
    let col = msg.columns;

    let tr = document.createElement("tr");

    for (let i = 0; i < col.length; i++) {
        let td = document.createElement("td");

        if (col[i] === null) {
            td.appendChild(document.createTextNode("NULL"));
            td.classList.add("null");
        } else
            td.appendChild(document.createTextNode(col[i]));

        tr.appendChild(td);
    }

    res_tbody.appendChild(tr);
}

function recv_row_count(msg) {
    let log = document.getElementById("messages");
    let s;

    s = JSON.stringify(msg);

    // FIXME - date and time?

    let p = document.createElement("p");

    if (msg.count == 1)
        p.appendChild(document.createTextNode("(1 row affected)"));
    else
        p.appendChild(document.createTextNode("(" + msg.count + " rows affected)"));

    log.appendChild(p);

    p.scrollIntoView();
}

function recv_query_finished() {
    document.getElementById("query-box").readOnly = false;
    document.getElementById("go-button").disabled = false;
    document.getElementById("stop-button").disabled = true;
    document.getElementById("database-changer").disabled = false;
}

function message_received(ev) {
    try {
        let msg = JSON.parse(ev.data);

        if (msg.type == undefined)
            throw Error("No message type given.");

        if (msg.type == "error") {
            if (logging_in) {
                logging_in = false;
                document.getElementById("login-button").disabled = false;
            }

            throw Error(msg.message);
        } else if (msg.type == "login")
            recv_login(msg);
        else if (msg.type == "logout")
            recv_logout(msg);
        else if (msg.type == "message")
            recv_message(msg);
        else if (msg.type == "table")
            recv_table(msg);
        else if (msg.type == "row")
            recv_row(msg);
        else if (msg.type == "row_count")
            recv_row_count(msg);
        else if (msg.type == "query_finished")
            recv_query_finished();
        else
            throw Error("Unrecognized message type " + msg.type + ".");
    } catch (e) {
        change_status(e.message, true);
    }
}

function socket_opened() {
    change_status("Connected.", false);

    document.getElementById("username").disabled = false;
    document.getElementById("password").disabled = false;
    document.getElementById("login-button").disabled = false;

    ws.addEventListener("message", message_received);
}

function socket_closed() {
    change_status("Disconnected.", true);

    logged_in = false;
    logging_in = false;

    document.getElementById("username").disabled = true;
    document.getElementById("password").disabled = true;
    document.getElementById("login-button").disabled = true;
    document.getElementById("login-button").setAttribute("value", "Login");
    document.getElementById("query-box").readOnly = true;
    document.getElementById("go-button").disabled = true;
    document.getElementById("stop-button").disabled = true;
    document.getElementById("database-changer-container").style.display = "none";

    setTimeout(function() {
        init_websocket();
    }, 5000);
}

function login_button_clicked() {
    if (!logged_in) {
        logging_in = true;

        document.getElementById("login-button").disabled = true;
        change_status("Logging in...", false);

        ws.send(JSON.stringify({
            "type": "login",
            "username": document.getElementById("username").value,
            "password": document.getElementById("password").value,
        }));
    } else {
        ws.send(JSON.stringify({
            "type": "logout"
        }));
    }
}

function go_button_clicked() {
    if (!logged_in)
        return;

    let res = document.getElementById("results");

    while (res.hasChildNodes()) {
        res.removeChild(res.firstChild);
    }

    let q = document.getElementById("query-box").value;

    if (q == "")
        return;

    ws.send(JSON.stringify({
        "type": "query",
        "query": q
    }));

    document.getElementById("go-button").disabled = true;
    document.getElementById("stop-button").disabled = false;
    document.getElementById("query-box").readOnly = true;
    document.getElementById("database-changer").disabled = true;
}

function stop_button_clicked() {
    if (!logged_in)
        return;

    ws.send(JSON.stringify({
        "type": "cancel"
    }));
}

function init_websocket() {
    ws = new WebSocket("ws://localhost:52441/");

    ws.addEventListener("open", socket_opened);
    ws.addEventListener("close", socket_closed);
}

function init() {
    document.getElementById("login-form").addEventListener("submit", function(ev) {
        login_button_clicked();
        ev.preventDefault();
    });

    document.getElementById("go-button").addEventListener("click", function(ev) {
        go_button_clicked();
        ev.preventDefault();
    });

    document.getElementById("stop-button").addEventListener("click", function(ev) {
        stop_button_clicked();
        ev.preventDefault();
    });

    init_websocket();

    // FIXME - reconnect if goes down
}
