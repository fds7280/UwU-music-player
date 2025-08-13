#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <ncurses.h>
#include <algorithm>
#include <thread>
#include <atomic>
#include <cstdio>
#include <unistd.h>

// For Audio Playback (using PortAudio)
#include <portaudio.h>

// For Audio File Decoding (using libsndfile)
#include <sndfile.h>

// For MP3 Metadata and Tagging (using TagLib)
#include <taglib/fileref.h>
#include <taglib/tag.h>

namespace fs = std::filesystem;

// Global atomic flag to control playback
std::atomic<bool> is_playing(false);

// File descriptor for original stderr
int original_stderr;

// Function to redirect stderr to /dev/null
void redirectStderr() {
    fflush(stderr);
    original_stderr = dup(fileno(stderr));
    if (original_stderr == -1) {
        return;
    }
    FILE* new_stderr = freopen("/dev/null", "w", stderr);
    if (new_stderr == nullptr) {
        // Handle error if redirection fails, though unlikely
    }
}

// Function to restore original stderr
void restoreStderr() {
    if (original_stderr != -1) {
        fflush(stderr);
        dup2(original_stderr, fileno(stderr));
        close(original_stderr);
    }
}

// The callback function for PortAudio
static int paCallback(const void *inputBuffer, void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData)
{
    SNDFILE* sf = (SNDFILE*)userData;
    sf_count_t framesRead = sf_readf_short(sf, (short*)outputBuffer, framesPerBuffer);

    if (framesRead > 0 && is_playing) {
        return paContinue;
    } else {
        is_playing = false;
        return paComplete;
    }
}

// Function to play audio using libsndfile and PortAudio
void PlayAudio(const std::string& file_path) {
    PaError pa_err;
    PaStreamParameters pa_params;
    PaStream *stream;

    SF_INFO sfinfo;
    SNDFILE* sf = sf_open(file_path.c_str(), SFM_READ, &sfinfo);
    if (!sf) {
        return;
    }

    pa_err = Pa_Initialize();
    if (pa_err != paNoError) {
        sf_close(sf);
        return;
    }

    pa_params.device = Pa_GetDefaultOutputDevice();
    pa_params.channelCount = sfinfo.channels;
    pa_params.sampleFormat = paInt16;
    pa_params.suggestedLatency = Pa_GetDeviceInfo(pa_params.device)->defaultLowOutputLatency;
    pa_params.hostApiSpecificStreamInfo = nullptr;

    pa_err = Pa_OpenStream(&stream, nullptr, &pa_params, sfinfo.samplerate, 512, paClipOff, paCallback, sf);
    if (pa_err != paNoError) {
        Pa_Terminate();
        sf_close(sf);
        return;
    }

    pa_err = Pa_StartStream(stream);
    if (pa_err != paNoError) {
        Pa_CloseStream(stream);
        Pa_Terminate();
        sf_close(sf);
        return;
    }

    is_playing = true;

    while (is_playing) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    sf_close(sf);
}

// File browser to select a directory
std::string run_file_browser(const std::string& start_path) {
    std::string current_path_str = start_path;
    int highlight = 0;
    int ch;
    std::vector<fs::path> entries;

    while (true) {
        clear();
        printw("Current Directory: %s\n\n", current_path_str.c_str());
        printw("Use arrow keys to navigate, 's' to select, and Enter to enter a directory.\n\n");
        refresh();

        entries.clear();
        entries.push_back("..");

        try {
            for (const auto& entry : fs::directory_iterator(current_path_str)) {
                entries.push_back(entry.path());
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return "";
        }
        
        std::sort(entries.begin() + 1, entries.end(), [](const fs::path& a, const fs::path& b) {
            bool a_is_dir = fs::is_directory(a);
            bool b_is_dir = fs::is_directory(b);
            if (a_is_dir != b_is_dir) {
                return a_is_dir > b_is_dir;
            }
            return a.filename().string() < b.filename().string();
        });

        for (size_t i = 0; i < entries.size(); ++i) {
            if (i == highlight) {
                attron(A_REVERSE);
            }
            
            if (fs::is_directory(entries[i])) {
                attron(COLOR_PAIR(1));
                printw(" %s\n", entries[i].filename().string().c_str());
                attroff(COLOR_PAIR(1));
            } else {
                printw(" %s\n", entries[i].filename().string().c_str());
            }
            
            if (i == highlight) {
                attroff(A_REVERSE);
            }
        }
        refresh();

        ch = getch();

        switch (ch) {
            case KEY_UP:
                highlight = (highlight > 0) ? highlight - 1 : entries.size() - 1;
                break;
            case KEY_DOWN:
                highlight = (highlight < entries.size() - 1) ? highlight + 1 : 0;
                break;
            case 10:
                if (highlight >= 0 && highlight < entries.size() && fs::is_directory(entries[highlight])) {
                    current_path_str = fs::canonical(entries[highlight]).string();
                    highlight = 0;
                }
                break;
            case 's':
                if (highlight >= 0 && highlight < entries.size() && fs::is_directory(entries[highlight])) {
                    return fs::canonical(entries[highlight]).string();
                }
                break;
            case 27:
                return "";
        }
    }
}

// TUI function for the playback screen
void run_playback_tui(const std::string& music_directory) {
    std::vector<fs::path> files;
    
    if (fs::exists(music_directory) && fs::is_directory(music_directory)) {
        for (const auto& entry : fs::directory_iterator(music_directory)) {
            if (entry.path().extension() == ".mp3") {
                files.push_back(entry.path());
            }
        }
    } else {
        return;
    }

    int selected_item = 0;
    int ch = 0;
    std::thread audio_thread;

    while (ch != 27) {
        clear();
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        // Draw the song list on the left
        mvprintw(0, 0, "Music in: %s", music_directory.c_str());
        for (size_t i = 0; i < files.size(); ++i) {
            if (i == selected_item) {
                attron(A_REVERSE);
                mvprintw(i + 2, 0, "%s", files[i].filename().string().c_str());
                attroff(A_REVERSE);
            } else {
                mvprintw(i + 2, 0, "%s", files[i].filename().string().c_str());
            }
        }

        // Draw the playback status on the right
        WINDOW *info_win = newwin(max_y, max_x / 2, 0, max_x / 2);
        box(info_win, 0, 0);

        mvwprintw(info_win, 1, 1, "Now Playing:");
        if (is_playing && selected_item != -1) {
            TagLib::FileRef f(files[selected_item].string().c_str());
            TagLib::Tag *tag = f.tag();
            mvwprintw(info_win, 3, 1, "Title: %s", tag->title().toCString(true));
            mvwprintw(info_win, 4, 1, "Artist: %s", tag->artist().toCString(true));
            mvwprintw(info_win, 5, 1, "Album: %s", tag->album().toCString(true));
            mvwprintw(info_win, 7, 1, "Press SPACE to pause/resume.");
        } else {
            mvwprintw(info_win, 3, 1, "No song playing.");
            mvwprintw(info_win, 5, 1, "Press Enter to play selected song.");
        }
        
        refresh();
        wrefresh(info_win);
        delwin(info_win);
        
        ch = getch();
        switch(ch) {
            case KEY_UP:
                if (selected_item > 0)
                    selected_item--;
                break;
            case KEY_DOWN:
                if (selected_item < files.size() - 1)
                    selected_item++;
                break;
            case 10:
                if(audio_thread.joinable()) {
                    is_playing = false;
                    audio_thread.join();
                }
                audio_thread = std::thread(PlayAudio, files[selected_item].string());
                break;
            case ' ':
                if(is_playing) {
                    is_playing = false;
                } else {
                    is_playing = true;
                }
                break;
        }
    }
    if(audio_thread.joinable()) {
        is_playing = false;
        audio_thread.join();
    }
}

int main() {
    redirectStderr();
    
    initscr();
    start_color();
    init_pair(1, COLOR_YELLOW, COLOR_BLACK);
    noecho();
    cbreak();
    keypad(stdscr, TRUE);

    std::string music_dir = run_file_browser("/home");

    if (!music_dir.empty()) {
        run_playback_tui(music_dir);
    }
    
    endwin();
    
    restoreStderr();
    
    return 0;
}
















