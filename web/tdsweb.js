'use strict';

let ws;

document.addEventListener("DOMContentLoaded", init);

function change_status(msg, error) {
    let status = document.getElementById("status");

    while (status.hasChildNodes()) { status.removeChild(status.firstChild); }

    status.appendChild(document.createTextNode(msg));

    if (error) {
        status.style.fontWeight = "bold";
        status.style.color = "red";
    } else {
        status.style.fontWeight = "";
        status.style.color = "";
    }
}

function message_received(ev) {
    try {
        let msg = JSON.parse(ev.data);

        if (msg.type == undefined)
            throw Error("No message type given.");

        if (msg.type == "error")
            throw Error(msg.message);
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

    document.getElementById("username").disabled = true;
    document.getElementById("password").disabled = true;
    document.getElementById("login-button").disabled = true;

    // FIXME - poll reconenct
}

function login_button_clicked() {
    ws.send(JSON.stringify({
        "type": "login",
        "username": document.getElementById("username").value,
        "password": document.getElementById("password").value,
    }));
    // FIXME
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

    init_websocket();

    // FIXME - reconnect if goes down
}
