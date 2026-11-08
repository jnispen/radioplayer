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
#include <poll.h>
#include <cstdlib>
#include <signal.h>
#include <fstream>
#include <cerrno>
#include <ctime>
#include <random>

volatile sig_atomic_t stop_requested = 0;

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
#define KEY_M_CODE      50 // Linux input keycode for M
#define KEY_R_CODE      19 // Linux input keycode for R

#ifndef AUDIO_PLAYER
#define AUDIO_PLAYER "mpg123"
#endif

class RadioPlayer {
private:
    std::vector<std::string> inetstreams;
    std::vector<std::string> songs;
    std::string device;
    std::string str_input_file;
    std::string str_songlist_file;
    std::string log_file;
    std::string player_;
    int fevdev;
    pid_t player_pid;
    unsigned int idx;
    bool music_mode;
    bool advance_on_completion;
    std::mt19937 random_generator;

public:
    RadioPlayer(const std::string& device_path,
                const std::string& streams_file_path,
                const std::string& player,
                const std::string& songlist_file_path)
        : device(device_path),
          str_input_file(streams_file_path),
          str_songlist_file(songlist_file_path),
          log_file("/tmp/radioplayer.log"),
          player_(player),
          player_pid(-1),
          idx(0),
          music_mode(false),
          advance_on_completion(false),
          random_generator(static_cast<std::mt19937::result_type>(std::time(nullptr)) ^
                           static_cast<std::mt19937::result_type>(getpid())) {

        fevdev = -1;
        if (!readstreams()) {
            exit(1);
        }

        if (inetstreams.empty()) {
            std::cerr << "Error: No streams loaded from " << streams_file_path << std::endl;
            exit(1);
        }

        if (!str_songlist_file.empty() && !readsongs()) {
            exit(1);
        }

        int log_fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_fd == -1) {
            std::cerr << "Warning: Unable to initialize log file " << log_file
                      << ": " << strerror(errno) << std::endl;
        } else {
            close(log_fd);
        }

        if (player != "mpg123" && player != "mplayer") {
            std::cerr << "Error: Unknown audio player: " << player << std::endl;
            exit(1);
        }
    }

    ~RadioPlayer() {
        stopPlayer();
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

    bool readsongs() {
        std::ifstream inputFile(str_songlist_file);

        if (!inputFile.is_open()) {
            std::cerr << "Unable to open songlist file: " << str_songlist_file << std::endl;
            return false;
        }

        std::string line;
        while (std::getline(inputFile, line)) {
            // Handle songlists created on Windows as well as Unix systems.
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty() || line[0] == '#') {
                continue;
            }

            if (isValidSongPath(line)) {
                songs.push_back(line);
            } else {
                std::cerr << "Warning: Ignoring invalid song path: [" << line << "]" << std::endl;
            }
        }

        std::cout << "Read [" << songs.size() << "] songs from ["
                  << str_songlist_file << "]" << std::endl;
        return true;
    }

    void run() {
        if (!initialize()) {
            return;
        }

        const int MAX_EVENTS = 64;
        std::vector<input_event> ev(MAX_EVENTS);
        const int BYTES_TO_READ = ev.size() * sizeof(input_event);

        while (!stop_requested) {
            struct pollfd event_fd;
            event_fd.fd = fevdev;
            event_fd.events = POLLIN;
            event_fd.revents = 0;

            int poll_result = poll(&event_fd, 1, 250);
            if (poll_result < 0) {
                if (errno == EINTR) {
                    checkPlaybackCompletion();
                    continue;
                }
                std::cerr << "Error while waiting for input events" << std::endl;
                break;
            }

            if (poll_result > 0) {
                if (event_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    break;
                }

                if (event_fd.revents & POLLIN) {
                    int rd = read(fevdev, ev.data(), BYTES_TO_READ);
                    if (rd < 0 && errno == EINTR) {
                        checkPlaybackCompletion();
                        continue;
                    }
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
            }

            checkPlaybackCompletion();
        }

        if (fevdev != -1) {
            close(fevdev);
            fevdev = -1;
        }

        stopPlayer();

        std::cout << "Exiting" << std::endl; 
    }

private:
    const std::vector<std::string>& currentList() const {
        return music_mode ? songs : inetstreams;
    }

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

    bool isValidSongPath(const std::string& path) {
        if (path.empty() || path[0] != '/') {
            return false;
        }

        struct stat file_info;
        return stat(path.c_str(), &file_info) == 0 &&
               S_ISREG(file_info.st_mode) &&
               access(path.c_str(), R_OK) == 0;
    }

    void handleKeyEvent(int code) {
        const unsigned int list_len = currentList().size();
        switch (code) {
            case KEY_1_CODE:  if (0 < list_len) playItem(0); break;
            case KEY_2_CODE:  if (1 < list_len) playItem(1); break;
            case KEY_3_CODE:  if (2 < list_len) playItem(2); break;
            case KEY_4_CODE:  if (3 < list_len) playItem(3); break;
            case KEY_5_CODE:  if (4 < list_len) playItem(4); break;
            case KEY_6_CODE:  if (5 < list_len) playItem(5); break;
            case KEY_7_CODE:  if (6 < list_len) playItem(6); break;
            case KEY_8_CODE:  if (7 < list_len) playItem(7); break;
            case KEY_9_CODE:  if (8 < list_len) playItem(8); break;
            case KEY_0_CODE:  killStream(); break; // stop playback
            case KEY_LEFT_CODE:
                if (list_len > 0) {
                    idx = (idx + list_len - 1) % list_len;
                    playItem(idx);
                }
                break;
            case KEY_RIGHT_CODE:
                if (list_len > 0) {
                    idx = (idx + 1) % list_len;
                    playItem(idx);
                }
                break;
            case KEY_R_CODE:
                playRandomItem();
                break;
            case KEY_M_CODE:
                toggleMode();
                break;
        }
    }

    void playRandomItem() {
        const unsigned int list_len = currentList().size();
        if (list_len == 0) {
            return;
        }

        std::uniform_int_distribution<unsigned int> distribution(0, list_len - 1);
        const unsigned int random_idx = distribution(random_generator);
        std::cout << "Randomly selected entry " << (random_idx + 1)
                  << " of " << list_len << std::endl;
        playItem(random_idx);
    }

    void toggleMode() {
        if (!music_mode && songs.empty()) {
            std::cerr << "No playable songs are loaded; cannot switch to music mode" << std::endl;
            return;
        }

        killStream();
        music_mode = !music_mode;
        idx = 0;
        std::cout << "Switched to " << (music_mode ? "music" : "radio") << " mode" << std::endl;
    }

    void killStream() {
        advance_on_completion = false;
        std::cout << "\n\n>>> CMD [STOP playback]\n";
        stopPlayer();
    }

    void playItem(unsigned int index) {
        const std::vector<std::string>& list = currentList();
        if (index >= list.size()) {
            std::cerr << "Invalid item index: " << index << std::endl;
            return;
        }

        idx = index;
        advance_on_completion = false;
        stopPlayer();

        std::vector<std::string> arguments;
        arguments.push_back(player_);
        if (player_ == "mpg123") {
            arguments.push_back("-o");
            arguments.push_back("alsa");
            arguments.push_back("--buffer");
            arguments.push_back("2096");
            arguments.push_back("--preload");
            arguments.push_back("0.8");
            arguments.push_back("-r");
            arguments.push_back("44100");
        } else {
            arguments.push_back("-quiet");
        }
        arguments.push_back(list[index]);

        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (std::string& argument : arguments) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = fork();
        if (pid == -1) {
            std::cerr << "Error: fork() failed" << std::endl;
            return;
        }
        if (pid == 0) {
            int child_log_fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (child_log_fd != -1) {
                if (dup2(child_log_fd, STDOUT_FILENO) == -1 ||
                    dup2(child_log_fd, STDERR_FILENO) == -1) {
                    _exit(126);
                }
                close(child_log_fd);
            }

            execvp(player_.c_str(), argv.data());
            const char message[] = "RadioPlayer: failed to execute audio player\n";
            const ssize_t ignored = write(STDERR_FILENO, message, sizeof(message) - 1);
            (void)ignored;
            _exit(127);
        }

        player_pid = pid;
        advance_on_completion = music_mode;
    }

    void checkPlaybackCompletion() {
        if (player_pid <= 0) {
            return;
        }

        int status;
        pid_t result = waitpid(player_pid, &status, WNOHANG);
        if (result == 0) {
            return;
        }
        if (result < 0 && errno != ECHILD) {
            return;
        }

        player_pid = -1;
        bool should_advance = advance_on_completion;
        advance_on_completion = false;

        if (should_advance && music_mode && !songs.empty()) {
            idx = (idx + 1) % songs.size();
            std::cout << "Song finished; advancing to song " << (idx + 1)
                      << " of " << songs.size() << std::endl;
            playItem(idx);
        }
    }

    void stopPlayer() {
        if (player_pid <= 0) {
            return;
        }

        const pid_t pid = player_pid;
        if (kill(pid, SIGTERM) == -1 && errno != ESRCH) {
            std::cerr << "Warning: Unable to stop audio player: "
                      << strerror(errno) << std::endl;
        }

        int status;
        for (int attempt = 0; attempt < 20; ++attempt) {
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid || (result == -1 && errno == ECHILD)) {
                player_pid = -1;
                return;
            }
            if (result == -1 && errno != EINTR) {
                break;
            }
            usleep(50000);
        }

        if (kill(pid, SIGKILL) == -1 && errno != ESRCH) {
            std::cerr << "Warning: Unable to force-stop audio player: "
                      << strerror(errno) << std::endl;
        }
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }
        player_pid = -1;
    }
};

void handle_stop_signal(int /*sig*/) {
    stop_requested = 1;
}

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <device_path> <streams_file_path> [songlist_file_path]" << std::endl;
        return 1;
    }

    signal(SIGINT, handle_stop_signal);
    signal(SIGTERM, handle_stop_signal);

    RadioPlayer player(argv[1], argv[2], AUDIO_PLAYER, argc == 4 ? argv[3] : "");
    player.run();

    return 0;
}
