#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <regex>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cstdlib>
#include <signal.h>
#include <fstream>

#define KEY_1_CODE      2
#define KEY_2_CODE      3
#define KEY_3_CODE      4
#define KEY_4_CODE      5
#define KEY_5_CODE      6
#define KEY_6_CODE      7
#define KEY_7_CODE      8
#define KEY_8_CODE      9
#define KEY_9_CODE      10
#define KEY_0_CODE      11
#define KEY_LEFT_CODE   105
#define KEY_RIGHT_CODE  106

#ifndef AUDIO_PLAYER
#define AUDIO_PLAYER "mpg123"
#endif

class RadioPlayer {
private:
    std::vector<std::string> inetstreams;
    std::string device;
    std::string str_input_file;
    std::string system_cmd;
    std::string system_kill_cmd;
    std::string player_str;
    std::string player_;
    int fevdev;
    unsigned int idx;
    unsigned int inet_len;
    unsigned int offset;

public:
    RadioPlayer(const std::string& device_path,
                const std::string& streams_file_path,
                const std::string& player)
        : device(device_path),
          str_input_file(streams_file_path),
          player_(player),
          idx(0) {

        fevdev = -1;
        if (!readstreams()) {
            exit(1);
        }

        if (inetstreams.empty()) {
            std::cerr << "Error: No streams loaded from " << streams_file_path << std::endl;
            exit(1);
        }

        system("> /tmp/radioplayer.log");

        if (player == "mpg123") {
			player_str = "mpg123 -o alsa --buffer 2096 --preload 0.8 -r 44100 ";
        } else if (player == "mplayer") {
            player_str = "mplayer -quiet ";
        } else {
            std::cerr << "Error: Unknown audio player: " << player << std::endl;
            exit(1);
        }
        system_kill_cmd = "killall " + player + " 2>/dev/null; echo 'STOP playback' >> /tmp/radioplayer.log";

		inet_len = inetstreams.size();
        idx = inet_len;
        offset = idx * 100;
    }

    ~RadioPlayer() {
        if (fevdev != -1) {
            close(fevdev);
        }
    }

    bool initialize() {
        fevdev = open(device.c_str(), O_RDONLY);
        if (fevdev == -1) {
            std::cerr << "Failed to open event device: " << device << std::endl;
            return false;
        }

        char name[256] = "Unknown";
        if (ioctl(fevdev, EVIOCGNAME(sizeof(name)), name) == -1) {
            std::cerr << "Failed to get device name" << std::endl;
        }
        std::cout << "Reading from: " << device << " (" << name << ")" << std::endl;

        if (ioctl(fevdev, EVIOCGRAB, 1) != 0) {
            std::cerr << "Failed to grab exclusive access" << std::endl;
            return false;
        }
        std::cout << "Exclusive access granted" << std::endl;

        return true;
    }

    bool readstreams() {
        std::ifstream inputFile(str_input_file);

        if (inputFile.is_open()) {
            std::string line;
            while (std::getline(inputFile, line)) {
                if (isValidUrl(line)) {
                    inetstreams.push_back(line);
                } else {
                    std::cerr << "Warning: Ignoring invalid URL: [" << line << "]" << std::endl;
                }
            }
            inputFile.close();

            std::cout << "Read [" << inetstreams.size() << "] streams:" << std::endl; 
            for (const auto& l : inetstreams) {
                std::cout << "[" << l << "]" << std::endl;
            }

        } else {
            std::cerr << "Unable to open streams file" << std::endl;
            return false;
        }

        return true;
    }

    void run() {
        if (!initialize()) {
            return;
        }

        const int MAX_EVENTS = 64;
        std::vector<input_event> ev(MAX_EVENTS);
        const int BYTES_TO_READ = ev.size() * sizeof(input_event);

        int rd;
        while (true) {
            rd = read(fevdev, ev.data(), BYTES_TO_READ);
            if (rd < (int)sizeof(input_event)) {
                break;
            }

            int num_events = rd / sizeof(input_event);
            for (int i = 0; i < num_events; i++) {
                if (ev[i].type == EV_KEY && ev[i].value == 1) {
                    handleKeyEvent(ev[i].code);
                }
            }
        }

        if (fevdev != -1) {
            close(fevdev);
            fevdev = -1;
        }

        std::cout << "Exiting" << std::endl; 
    }

private:
    bool isValidUrl(const std::string& url) {
        static const std::regex url_regex(
            R"(^https?://)"  // http or https
            R"(([a-zA-Z0-9\-._~:/?#\[\]@!$&'()*+,;=])+$)"  // Valid URL characters
        );

        if (url.empty()) {
            return false;
        }

        return std::regex_match(url, url_regex);
    }

    void handleKeyEvent(int code) {
        unsigned int mod_idx;
        switch (code) {
            case KEY_1_CODE:  if (0 < inet_len) playStream(0); idx = offset; break;     // play stream 0
            case KEY_2_CODE:  if (1 < inet_len) playStream(1); idx = offset + 1; break; // play stream 1
            case KEY_3_CODE:  if (2 < inet_len) playStream(2); idx = offset + 2; break; // etc.
            case KEY_4_CODE:  if (3 < inet_len) playStream(3); idx = offset + 3; break;
            case KEY_5_CODE:  if (4 < inet_len) playStream(4); idx = offset + 4; break;
            case KEY_6_CODE:  if (5 < inet_len) playStream(5); idx = offset + 5; break;
            case KEY_7_CODE:  if (6 < inet_len) playStream(6); idx = offset + 6; break;
            case KEY_8_CODE:  if (7 < inet_len) playStream(7); idx = offset + 7; break;
            case KEY_9_CODE:  if (8 < inet_len) playStream(8); idx = offset + 8; break;
            case KEY_0_CODE:  killStream(); break; // stop playback
            case KEY_LEFT_CODE:  idx = idx - 1; mod_idx = idx % inet_len; playStream(mod_idx); break; // play previous stream
            case KEY_RIGHT_CODE: idx = idx + 1; mod_idx = idx % inet_len; playStream(mod_idx); break; // play next stream
        }
    }

    void killStream() {
        std::cout << "\n\n>>> CMD [STOP playback]\n";
        system((system_kill_cmd).c_str());
    }

    void playStream(int index) {
        if (index < 0 || index >= (int)inetstreams.size()) {
            std::cerr << "Invalid stream index: " << index << std::endl;
            return;
        }

        system((system_kill_cmd).c_str());

        pid_t pid = fork();
        if (pid == -1) {
            std::cerr << "Error: fork() failed" << std::endl;
            return;
        }
        if (pid == 0) {
			system_cmd = player_str + inetstreams[index] + " 2>&1 | tee -a /tmp/radioplayer.log";
            std::cout << "\n\n>>> CMD [" << system_cmd << "]\n";
            execlp("sh", "sh", "-c", 
                (system_cmd).c_str(), nullptr);

            std::cerr << "Error: execlp() failed to execute shell: " << strerror(errno) << std::endl;
            exit(1);
        }

        waitpid(pid, nullptr, WNOHANG);
    }
};

void handle_sigint(int /*sig*/) {
    std::cout << "Caught SIGINT. Exiting gracefully.." << std::endl;
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <device_path> <streams_file_path>" << std::endl;
        return 1;
    }

    signal(SIGINT, handle_sigint);

    RadioPlayer player(argv[1], argv[2], AUDIO_PLAYER);
    player.run();

    return 0;
}
