// Gunsmith3D - internet radio: plays an MP3 stream (Icecast, SHOUTcast, a .pls/.m3u playlist link) as 48 kHz stereo
// floats, with the song title the station sends. The network and the decoding run on their own thread; read() takes
// what's ready (silence while buffering). No graphics or sound output here: the app mixes it into the world.
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace netradio {

constexpr int RATE = 48000;   // what read() hands out: stereo, interleaved

class Player {
public:
    explicit Player(const std::string& url);
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    // Fills `frames` stereo frames (2 floats each); silence where nothing is ready. Safe from any one thread.
    void read(float* lr, int frames);
    std::string state() const;    // "Connecting", "Buffering", "Playing", or "Error: ..." (it stops then)
    std::string title() const;    // the song now playing, if the station says
    std::string station() const;  // the station's own name, if it says
    bool failed() const;
    double buffered() const;      // seconds of sound waiting
    const std::string& url() const { return url_; }
    struct Impl;
private:
    std::string url_;
    std::shared_ptr<Impl> p_;
};

struct StationInfo { std::string name, url, tags, country, codec; int bitrate = 0; };
// Fetches a whole small page (station search). Blocking: call it off the main thread.
bool httpGet(const std::string& url, std::string& body, std::string* err = nullptr);
// Stations by name or genre from the public radio-browser.info directory (MP3 only: the ones this player can play).
std::vector<StationInfo> searchStations(const std::string& query, int limit = 30, std::string* err = nullptr);
std::vector<StationInfo> parseStations(const std::string& json);   // (the directory's answer: for tests)
// A few built-in stations (listener-supported stations that publish their direct stream links).
const std::vector<StationInfo>& builtinStations();
// The title out of an ICY metadata block ("StreamTitle='Artist - Song';...")
std::string icyTitle(const std::string& meta);
// The first stream link in a .pls or .m3u playlist ("" if none)
std::string playlistUrl(const std::string& text);

}  // namespace netradio
