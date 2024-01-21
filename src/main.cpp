#include <apclient.hpp>
#include <apuuid.hpp>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <algorithm>
#include <memory>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/bind.h>
#else
#define EM_BOOL bool
#define EM_TRUE true
#define EM_FALSE false
#if !defined WIN32 && !defined _WIN32
#include <poll.h>
#endif
#endif
#include <math.h>
#include <limits>

#if defined(WIN32) && !defined(PRId64 )
#define PRId64 "I64d"
#endif

#define VERSION_TUPLE {0,4,1}


/*
 * This is the entry point for our cpp ap client.
 */


using nlohmann::json;


std::unique_ptr<APClient> ap;
bool ap_sync_queued = false;
bool ap_connect_sent = false; // TODO: move to APClient::State ?
bool is_https = false; // set to true if the context only supports wss://
bool is_wss = false; // set to true if the connection specifically asked for wss://
bool is_ws = false; // set to true if the connection specifically asked for ws://
unsigned connect_error_count = 0;
bool awaiting_password = false;
std::string slot = "Player";


#if __cplusplus < 201500L
decltype(APClient::DEFAULT_URI) constexpr APClient::DEFAULT_URI;  // c++14 needs a proper declaration
#endif

#ifdef __EMSCRIPTEN__
#define VIRTUAL_HOME_DIR "/settings"
#define OLD_DATAPACKAGE_CACHE "/settings/datapackage.json"
#define UUID_FILE "/settings/uuid"
#define CERT_STORE "" // not required in a browser context
#else
#define OLD_DATAPACKAGE_CACHE "datapackage.json"
#define UUID_FILE "uuid" // TODO: place in %appdata%
#define CERT_STORE "cacert.pem"
#endif


bool isEqual(double a, double b)
{
    return fabs(a - b) < std::numeric_limits<double>::epsilon() * fmax(fabs(a), fabs(b));
}


void set_status_color(const std::string& field, const std::string& color)
{

#ifdef __EMSCRIPTEN__
    EM_ASM({
        document.getElementById(UTF8ToString($0)).style.color = UTF8ToString($1);
    }, field.c_str(), color.c_str());
#else
#endif
}

void disconnect_ap()
{
    ap.reset();
    set_status_color("ap", "#777777");
}

void ask_password()
{
    printf("Enter Password\n");
    awaiting_password = true;
}

void abort_password()
{
    awaiting_password = false;
}

void connect_slot(const std::string& password)
{
    if (ap) {
        std::list<std::string> tags = {"TextOnly"};
        ap->ConnectSlot(slot, password, 0, tags, VERSION_TUPLE);
        ap_connect_sent = true; // TODO: move to APClient::State ?
    } else {
        printf("Connection lost!\n");
    }
}

void connect_ap(std::string uri="", std::string newSlot="")
{
    ap.reset();

    if (!newSlot.empty())
        slot = newSlot;

    is_ws = uri.rfind("ws://", 0) == 0;
    is_wss = uri.rfind("wss://", 0) == 0;

    // remove schema from URI for UUID generation; UUID is per room this way
    std::string uuid = ap_get_uuid(UUID_FILE,
            uri.empty() ? APClient::DEFAULT_URI :
            is_ws ? uri.substr(5) :
            is_wss ? uri.substr(6) :
            uri);

    #ifdef __EMSCRIPTEN__
    if (is_https && is_ws) {
        EM_ASM({
            throw 'WS not supported';
        });
    } else if (is_https && !is_wss) {
        uri = "wss://" + uri; // only wss supported
    }
    #endif

    printf("Connecting to AP...\n");
    ap.reset(new APClient(uuid, "", uri.empty() ? APClient::DEFAULT_URI : uri, CERT_STORE));

    // load DataPackage cache
    try {
        ap->set_data_package_from_file(OLD_DATAPACKAGE_CACHE);
    } catch (std::exception) { /* ignore */ }

    // set state and callbacks
    ap_sync_queued = false;
    connect_error_count = 0;
    set_status_color("ap", "#ff0000");
    ap->set_socket_connected_handler([](){
        set_status_color("ap", "#ffff00");
        abort_password();
    });
    ap->set_socket_disconnected_handler([](){
        set_status_color("ap", "#ff0000");
        abort_password();
    });
    ap->set_socket_error_handler([](const std::string& error) {
        connect_error_count++;
        #ifdef __EMSCRIPTEN__
        set_status_color("ap", "#ff0000");
        if (is_https && !is_wss) {
            if (connect_error_count == 2) {
                printf("Error: could not connect to AP server! "
                       "Please check if the room is active and the port is correct.\n"
                       "You have to use the http:// version of ap-soeclient for non-SSL rooms.\n");
            } else if (connect_error_count > 2) {
                disconnect_ap();
            }
        }
        #else
        if (!error.empty() && error != "Unknown")
            printf("%s\n", error.c_str());
        #endif
    });
    ap->set_room_info_handler([](){
        if (ap->has_password()) {
            ask_password();
        } else {
            connect_slot("");
        }
    });
    ap->set_slot_connected_handler([](const json&){
        set_status_color("ap", "#00ff00");
    });
    ap->set_slot_disconnected_handler([](){
        set_status_color("ap", "#ffff00");
        ap_connect_sent = false;
    });
    ap->set_slot_refused_handler([](const std::list<std::string>& errors){
        set_status_color("ap", "#ffff00");
        ap_connect_sent = false;
        if (std::find(errors.begin(), errors.end(), "InvalidSlot") != errors.end()) {
            printf("Unknown or invalid slot: %s\n", slot.c_str());
        } else {
            printf("AP: Connection refused:");
            for (const auto& error: errors) printf(" %s", error.c_str());
            printf("\n");
        }
    });
    ap->set_items_received_handler([](const std::list<APClient::NetworkItem>& items) {
        if (!ap->is_data_package_valid()) {
            // NOTE: this should not happen since we ask for data package before connecting
            if (!ap_sync_queued) ap->Sync();
            ap_sync_queued = true;
            return;
        }
        for (const auto& item: items) {
            std::string itemname = ap->get_item_name(item.item, ap->get_player_game(ap->get_player_number()));
            std::string sender = ap->get_player_alias(item.player);
            std::string location = ap->get_location_name(item.location, ap->get_player_game(item.player));
            printf("  #%d: %s (%" PRId64 ") from %s - %s\n",
                   item.index, itemname.c_str(), item.item,
                   sender.c_str(), location.c_str());
        }
    });
    ap->set_data_package_changed_handler([](const json& data) {
        #ifdef __EMSCRIPTEN__
        EM_ASM(
            FS.syncfs(function (err) {});
        );
        #endif
    });
    ap->set_print_handler([](const std::string& msg) {
        printf("%s\n", msg.c_str());
    });
    ap->set_print_json_handler([](const std::list<APClient::TextNode>& msg) {
        printf("%s\n", ap->render_json(msg, APClient::RenderFormat::ANSI).c_str());
    });
}

void password_entered(const std::string& password)
{
    awaiting_password = false;
    connect_slot(password);
}

bool read_command(std::string& cmd)
{
#if defined __EMSCRIPTEN__
    return false;
#elif defined WIN32 || defined _WIN32
    static std::string cmd_buf;
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD n = 0;
    INPUT_RECORD rec;
    if (!PeekConsoleInputA(hStdin, &rec, 1, &n) || n < 1) return false;
    if (rec.EventType != KEY_EVENT) {
        // drop non-key-event
        ReadConsoleInput(hStdin, &rec, 1, &n);
        return false;
    }
    // read unicode from console buffer
    WCHAR wBuf[256];
    wBuf[255] = 0;
    n = 255;
    if (!ReadConsoleW(hStdin, wBuf, n, &n, NULL)) {
        return false;
    }
    // convert to utf8
    int mbSize = WideCharToMultiByte(CP_UTF8, 0, wBuf, n, NULL, 0, NULL, NULL);
    if (mbSize == 0) {
        return false;
    }
    char *mbBuf = new char[mbSize+1];
    mbBuf[mbSize] = 0;
    WideCharToMultiByte(CP_UTF8, 0, wBuf, n, mbBuf, mbSize, NULL, NULL);
    cmd_buf += mbBuf;
    delete[] mbBuf;
    while (!cmd_buf.empty() && (cmd_buf[0] == 0x08 || cmd_buf[0] == '\r' || cmd_buf[0] == '\n' || cmd_buf[0] == 127))
        cmd_buf = cmd_buf.substr(1);
    for (size_t i=1; i<cmd_buf.length(); i++) {
        if (cmd_buf[i] != 0x08) continue;
        // backspace
        if ((cmd_buf[i-1] & 0b10000000) == 0) {
            cmd_buf.erase(i-1, 2);
            i--;
        } else if ((cmd_buf[i-2] & 0b11100000) == 0b11000000) {
            cmd_buf.erase(i-2, 3);
            i-=2;
        } else if ((cmd_buf[i-3] & 0b11110000) == 0b11100000) {
            cmd_buf.erase(i-3, 4);
            i-=3;
        } else if ((cmd_buf[i-4] & 0b11111000) == 0b11110000) {
            cmd_buf.erase(i-4, 5);
            i-=4;
        }
    }
    WCHAR *wBuf2 = new WCHAR[cmd_buf.length() + 1];
    wBuf2[0] = 0;
    MultiByteToWideChar(CP_UTF8, 0, cmd_buf.c_str(), cmd_buf.length() + 1, wBuf2, cmd_buf.length() + 1);
    wprintf(L"\r                                        \r%ls", wBuf2);
    delete[] wBuf2;
    size_t p = cmd_buf.find('\r');
    if (p != cmd_buf.npos) {
        cmd = cmd_buf.substr(0, p);
        cmd_buf = cmd_buf.substr(p+1);
        printf("\n");
    }
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();
    return !cmd.empty();
#else
    struct pollfd fd = { STDIN_FILENO, POLLIN, 0 };
    int res = poll(&fd, 1, 5);
    if (res && fd.revents) {
        cmd.resize(1024);
        if (fgets((char*)cmd.data(), cmd.size(), stdin)) {
            cmd.resize(strlen(cmd.data()));
            while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) cmd.pop_back();
            return !cmd.empty();
        }
    }
    return false;
#endif
}

void on_command(const std::string& command)
{
    if (awaiting_password) {
        password_entered(command);
    } else if (command == "/help") {
        printf("Available commands:\n"
               "  /connect [addr[:port]] [slot] - connect to AP server\n"
               "  /disconnect - disconnect from AP server\n");
    } else if (command == "/connect") {
        connect_ap();
    } else if (command.find("/connect ") == 0) {
        std::string args = command.substr(9);
        auto p = args.find(' ');
        if (p != std::string::npos) {
            connect_ap(args.substr(0, p), args.substr(p+1));
        } else {
            connect_ap(args);
        }
    } else if (command == "/disconnect") {
        disconnect_ap();
    } else if (command.find("/") == 0) {
        printf("Unknown command: %s\n", command.c_str());
    } else if (!ap || ap->get_state() < APClient::State::SOCKET_CONNECTED) {
        printf("AP not connected. Can't send chat message.\n");
        if (command.length() >= 2 && command[1] == '/') {
            printf("Did you mean \"%s\"?\n", command.c_str()+1);
        } else if (command.substr(0, 7) == "connect") {
            auto p = command[7] ? 7 : command.npos;
            while (p != command.npos && command[p] == ' ') p++;
            printf("Did you mean \"/connect%s%s\"?\n",
                    p!=command.npos ? " " : "", p!=command.npos ? command.substr(p).c_str() : "");
        }
    } else {
        ap->Say(command);
    }
}

EM_BOOL step(double time, void* userData)
{
    // we run code that acts on elapsed time in the main loop
    // TODO: use async timers (for JS) instead and get rid of step() altogether
    if (ap) ap->poll();

    // parse stdin
    std::string cmd;
    if (read_command(cmd)) {
        on_command(cmd);
    }

    return EM_TRUE;
}
EM_BOOL interval_step(double time)
{
    return step(time, nullptr);
}

void start()
{
#ifndef __EMSCRIPTEN__ // HTML GUI has its own log
    // TODO: create log and redirect stdout
#endif

    printf("Running mainloop...\n");
    printf("use /connect [<host>] [<slot>] to connect to an AP server\n");
#ifdef __EMSCRIPTEN__
    // auto-connect to ap server given by #server=...
    EM_ASM({
        // TODO: use argv and set connect_ap instead?
        if (Module.apServer) Module.on_command('/connect '+Module.apServer);
    });
    //emscripten_request_animation_frame_loop(step, 0);
    EM_ASM({
        setInterval(function(){Module.step(0);}, 100);
    });
#else
    while (step(0, nullptr));
#endif
}

int main(int argc, char** argv)
{
#ifdef __EMSCRIPTEN__
    is_https = EM_ASM_INT({
        return (document.location.protocol == 'https:');
    });
#endif
#if defined WIN32 || defined _WIN32
    DWORD mode = 0;
    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) {
        mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_MOUSE_INPUT);
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), mode);
    }
#endif
#ifdef VIRTUAL_HOME_DIR
    // override home. used to default to persistent storage with emscripten
    setenv("HOME", VIRTUAL_HOME_DIR, true);
#endif
#ifdef USE_IDBFS
    // mount persistent storage, then run app
    EM_ASM({
        FS.mkdir('/settings');
        FS.mount(IDBFS, {}, '/settings');
        FS.syncfs(true, function(err) {
            Module.start()
        });
    });
#else
    start();
#endif

    return 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(main) {
    emscripten::function("start", &start);
    emscripten::function("step", &interval_step);
    emscripten::function("on_command", &on_command); // TODO: use stdin instead?
}
#endif
