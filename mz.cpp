#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <ncurses.h>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>

// For Audio Playback (using PortAudio)
#include <portaudio.h>

// For Audio File Decoding (using libsndfile)
#include <sndfile.h>

// For MP3 Metadata and Tagging (using TagLib)
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/id3v2frame.h>
#include <taglib/attachedpictureframe.h>

namespace fs = std::filesystem;

// ASCII art generation settings
const std::string ASCII_CHARS = " .:-=+*#%@";
const int THUMBNAIL_WIDTH = 40;
const int THUMBNAIL_HEIGHT = 20;

// Simple RGB struct for color processing
struct RGB {
    unsigned char r, g, b;
    int brightness() const {
        return (r + g + b) / 3;
    }
};

// Convert brightness to ASCII character
char brightnessToASCII(int brightness) {
    int index = (brightness * (ASCII_CHARS.length() - 1)) / 255;
    return ASCII_CHARS[index];
}

// Generate ASCII art from album art data
std::vector<std::string> generateASCIIArt(const char* imageData, size_t dataSize) {
    std::vector<std::string> asciiArt(THUMBNAIL_HEIGHT, std::string(THUMBNAIL_WIDTH, ' '));
    
    if (!imageData || dataSize == 0) {
        // Generate a clean musical themed pattern using simple ASCII
        std::vector<std::string> musicPattern = {
            "+--------------------------------------+",
            "|            ALBUM ARTWORK             |",
            "|               ~ * ~                  |",
            "|          +-------------+             |",
            "|          |  *       ~  |             |",
            "|          |             |             |",
            "|          |    ~   *    |             |",
            "|          |             |             |",
            "|          |  *       ~  |             |",
            "|          +-------------+             |",
            "|                                      |",
            "|         ##############               |",
            "|         ##          ##               |",
            "|         ##    *~    ##               |",
            "|         ##          ##               |",
            "|         ##############               |",
            "|                                      |",
            "|            NO IMAGE FOUND            |",
            "+--------------------------------------+",
            "                                        "
        };
        
        for (int i = 0; i < THUMBNAIL_HEIGHT && i < musicPattern.size(); i++) {
            std::string line = musicPattern[i];
            if (line.length() > THUMBNAIL_WIDTH) {
                asciiArt[i] = line.substr(0, THUMBNAIL_WIDTH);
            } else {
                asciiArt[i] = line + std::string(THUMBNAIL_WIDTH - line.length(), ' ');
            }
        }
        return asciiArt;
    }
    
    // Better algorithm for actual image data
    std::vector<std::vector<int>> brightness(THUMBNAIL_HEIGHT, std::vector<int>(THUMBNAIL_WIDTH, 0));
    
    // Create a more sophisticated sampling approach
    for (int y = 0; y < THUMBNAIL_HEIGHT; y++) {
        for (int x = 0; x < THUMBNAIL_WIDTH; x++) {
            // Sample multiple points around each pixel position for better averaging
            int totalBrightness = 0;
            int sampleCount = 0;
            
            // Sample a 3x3 area for each ASCII character position
            for (int sy = -1; sy <= 1; sy++) {
                for (int sx = -1; sx <= 1; sx++) {
                    int sampleY = y + sy;
                    int sampleX = x + sx;
                    
                    if (sampleY >= 0 && sampleY < THUMBNAIL_HEIGHT && 
                        sampleX >= 0 && sampleX < THUMBNAIL_WIDTH) {
                        
                        size_t index = ((sampleY * THUMBNAIL_WIDTH + sampleX) * dataSize) / 
                                     (THUMBNAIL_WIDTH * THUMBNAIL_HEIGHT);
                        
                        if (index < dataSize) {
                            // Use a more sophisticated brightness calculation
                            unsigned char byte1 = static_cast<unsigned char>(imageData[index]);
                            unsigned char byte2 = static_cast<unsigned char>(imageData[(index + 1) % dataSize]);
                            unsigned char byte3 = static_cast<unsigned char>(imageData[(index + 2) % dataSize]);
                            
                            // Simulate RGB to grayscale conversion weights
                            int pixelBrightness = (byte1 * 299 + byte2 * 587 + byte3 * 114) / 1000;
                            totalBrightness += pixelBrightness;
                            sampleCount++;
                        }
                    }
                }
            }
            
            if (sampleCount > 0) {
                brightness[y][x] = totalBrightness / sampleCount;
            }
        }
    }
    
    // Apply some smoothing to reduce noise
    for (int y = 1; y < THUMBNAIL_HEIGHT - 1; y++) {
        for (int x = 1; x < THUMBNAIL_WIDTH - 1; x++) {
            int smoothed = (brightness[y-1][x-1] + brightness[y-1][x] + brightness[y-1][x+1] +
                           brightness[y][x-1]   + brightness[y][x] * 2 + brightness[y][x+1] +
                           brightness[y+1][x-1] + brightness[y+1][x] + brightness[y+1][x+1]) / 10;
            
            int charIndex = (smoothed * (ASCII_CHARS.length() - 1)) / 255;
            charIndex = std::max(0, std::min((int)ASCII_CHARS.length() - 1, charIndex));
            asciiArt[y][x] = ASCII_CHARS[charIndex];
        }
    }
    
    // Add a simple ASCII border to make it look more like album art
    for (int x = 0; x < THUMBNAIL_WIDTH; x++) {
        if (x == 0 || x == THUMBNAIL_WIDTH - 1) {
            for (int y = 0; y < THUMBNAIL_HEIGHT; y++) {
                asciiArt[y][x] = '|';
            }
        }
    }
    for (int y = 0; y < THUMBNAIL_HEIGHT; y++) {
        if (y == 0 || y == THUMBNAIL_HEIGHT - 1) {
            for (int x = 0; x < THUMBNAIL_WIDTH; x++) {
                if (x == 0 || x == THUMBNAIL_WIDTH - 1) {
                    asciiArt[y][x] = '+';
                } else {
                    asciiArt[y][x] = '-';
                }
            }
        }
    }
    
    return asciiArt;
}

// Extract album art from MP3 file
std::vector<std::string> extractAlbumArtASCII(const std::string& filePath) {
    TagLib::MPEG::File file(filePath.c_str());
    
    if (!file.isValid()) {
        return generateASCIIArt(nullptr, 0);
    }
    
    TagLib::ID3v2::Tag* id3v2Tag = file.ID3v2Tag();
    if (!id3v2Tag) {
        return generateASCIIArt(nullptr, 0);
    }
    
    TagLib::ID3v2::FrameList frameList = id3v2Tag->frameList("APIC");
    if (frameList.isEmpty()) {
        return generateASCIIArt(nullptr, 0);
    }
    
    TagLib::ID3v2::AttachedPictureFrame* pictureFrame = 
        static_cast<TagLib::ID3v2::AttachedPictureFrame*>(frameList.front());
    
    if (!pictureFrame) {
        return generateASCIIArt(nullptr, 0);
    }
    
    TagLib::ByteVector imageData = pictureFrame->picture();
    return generateASCIIArt(imageData.data(), imageData.size());
}

// Global atomic flags and variables for playback control and progress
std::atomic<bool> is_playing(false);
std::atomic<bool> is_paused(false);
std::atomic<sf_count_t> total_frames(0);
std::atomic<sf_count_t> current_frame(0);

// The callback function for PortAudio
static int paCallback(const void *inputBuffer, void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData)
{
    SNDFILE* sf = (SNDFILE*)userData;
    
    if (is_paused) {
        memset(outputBuffer, 0, framesPerBuffer * sizeof(short) * 2);
        return paContinue;
    }

    sf_count_t framesRead = sf_readf_short(sf, (short*)outputBuffer, framesPerBuffer);
    current_frame = sf_seek(sf, 0, SF_SEEK_CUR);

    if (framesRead > 0 && is_playing) {
        return paContinue;
    } else {
        is_playing = false;
        return paComplete;
    }
}

// Function to play audio using libsndfile and PortAudio
void PlayAudio(const std::string& file_path, PaStream*& stream, SNDFILE*& sf, SF_INFO& sfinfo) {
    sf = sf_open(file_path.c_str(), SFM_READ, &sfinfo);
    if (!sf) return;

    if (Pa_Initialize() != paNoError) {
        sf_close(sf);
        return;
    }
    
    // Get total frames and reset to start
    sf_seek(sf, 0, SF_SEEK_END);
    total_frames = sf_seek(sf, 0, SF_SEEK_CUR);
    sf_seek(sf, 0, SF_SEEK_SET);
    current_frame = 0;

    PaStreamParameters pa_params = {
        .device = Pa_GetDefaultOutputDevice(),
        .channelCount = sfinfo.channels,
        .sampleFormat = paInt16,
        .suggestedLatency = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice())->defaultLowOutputLatency,
        .hostApiSpecificStreamInfo = nullptr
    };

    if (Pa_OpenStream(&stream, nullptr, &pa_params, sfinfo.samplerate, 512, paClipOff, paCallback, sf) != paNoError) {
        Pa_Terminate();
        sf_close(sf);
        return;
    }

    if (Pa_StartStream(stream) != paNoError) {
        Pa_CloseStream(stream);
        Pa_Terminate();
        sf_close(sf);
        return;
    }
}

// File browser to select a directory
std::string run_file_browser(const std::string& start_path) {
    std::string current_path = start_path;
    int highlight = 0;
    std::vector<fs::path> entries;

    while (true) {
        clear();
        printw("Current Directory: %s\n\n", current_path.c_str());
        printw("Use arrow keys to navigate, 's' to select, Enter to enter directory.\n\n");

        entries.clear();
        entries.push_back("..");

        try {
            for (const auto& entry : fs::directory_iterator(current_path)) {
                entries.push_back(entry.path());
            }
        } catch (const fs::filesystem_error&) {
            return "";
        }
        
        std::sort(entries.begin() + 1, entries.end(), [](const fs::path& a, const fs::path& b) {
            bool a_is_dir = fs::is_directory(a);
            bool b_is_dir = fs::is_directory(b);
            if (a_is_dir != b_is_dir) return a_is_dir > b_is_dir;
            return a.filename().string() < b.filename().string();
        });

        for (size_t i = 0; i < entries.size(); ++i) {
            if (i == highlight) attron(A_REVERSE);
            
            if (fs::is_directory(entries[i])) {
                attron(COLOR_PAIR(1));
                printw(" %s\n", entries[i].filename().string().c_str());
                attroff(COLOR_PAIR(1));
            } else {
                printw(" %s\n", entries[i].filename().string().c_str());
            }
            
            if (i == highlight) attroff(A_REVERSE);
        }
        refresh();

        int ch = getch();
        switch (ch) {
            case KEY_UP:
                highlight = (highlight > 0) ? highlight - 1 : entries.size() - 1;
                break;
            case KEY_DOWN:
                highlight = (highlight < entries.size() - 1) ? highlight + 1 : 0;
                break;
            case 10: // Enter
                if (highlight < entries.size() && fs::is_directory(entries[highlight])) {
                    current_path = fs::canonical(entries[highlight]).string();
                    highlight = 0;
                }
                break;
            case 's':
                if (highlight < entries.size() && fs::is_directory(entries[highlight])) {
                    return fs::canonical(entries[highlight]).string();
                }
                break;
            case 27: // Escape
                return "";
        }
    }
}

// TUI function for the playback screen
void run_playback_tui(const std::string& music_directory) {
    std::vector<fs::path> files;
    
    if (!fs::exists(music_directory) || !fs::is_directory(music_directory)) return;
    
    for (const auto& entry : fs::directory_iterator(music_directory)) {
        if (entry.path().extension() == ".mp3") {
            files.push_back(entry.path());
        }
    }

    int selected_item = 0;
    PaStream* stream = nullptr;
    SNDFILE* sf = nullptr;
    SF_INFO sfinfo;

    nodelay(stdscr, TRUE);

    while (true) {
        int ch = getch();
        if (ch == 27) break; // Escape to exit

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
                mvprintw(i + 2, 0, "%s", files[i].filename().c_str());
            }
        }
        
        // Draw the playback status on the right
        WINDOW *info_win = newwin(max_y, max_x / 2, 0, max_x / 2);
        box(info_win, 0, 0);

        mvwprintw(info_win, 1, 1, "Now Playing:");
        if (is_playing && selected_item != -1) {
            TagLib::FileRef f(files[selected_item].string().c_str());
            TagLib::Tag *tag = f.tag();
            mvwprintw(info_win, 2, 1, "Title: %s", tag->title().toCString(true));
            mvwprintw(info_win, 3, 1, "Artist: %s", tag->artist().toCString(true));
            mvwprintw(info_win, 4, 1, "Album: %s", tag->album().toCString(true));

            // Draw ASCII art thumbnail
            std::vector<std::string> asciiArt = extractAlbumArtASCII(files[selected_item].string());
            int art_start_y = 6;
            for (size_t i = 0; i < asciiArt.size() && (art_start_y + i) < (max_y - 8); ++i) {
                mvwprintw(info_win, art_start_y + i, 1, "%s", asciiArt[i].c_str());
            }

            // Progress bar (positioned below ASCII art)
            int progress_y = art_start_y + THUMBNAIL_HEIGHT + 1;
            if (total_frames > 0 && progress_y < max_y - 4) {
                float progress = static_cast<float>(current_frame) / total_frames;
                int bar_width = std::min(THUMBNAIL_WIDTH, max_x / 2 - 4);
                int progress_bar_fill = static_cast<int>(bar_width * progress);
                
                mvwprintw(info_win, progress_y, 1, "[");
                for (int i = 0; i < bar_width; ++i) {
                    waddch(info_win, i < progress_bar_fill ? '#' : '-');
                }
                wprintw(info_win, "] %d%%", static_cast<int>(progress * 100));

                long total_seconds = static_cast<long>(static_cast<float>(total_frames) / sfinfo.samplerate);
                long current_seconds = static_cast<long>(static_cast<float>(current_frame) / sfinfo.samplerate);

                mvwprintw(info_win, progress_y + 1, 1, "%02ld:%02ld / %02ld:%02ld", 
                         current_seconds / 60, current_seconds % 60,
                         total_seconds / 60, total_seconds % 60);
            }

            mvwprintw(info_win, max_y - 2, 1, is_paused ? "PAUSED. Press SPACE to resume." : "Press SPACE to pause.");
        } else {
            mvwprintw(info_win, 2, 1, "No song playing.");
            mvwprintw(info_win, 3, 1, "Press Enter to play selected song.");
            
            // Show ASCII art for selected song even when not playing
            if (!files.empty() && selected_item < files.size()) {
                std::vector<std::string> asciiArt = extractAlbumArtASCII(files[selected_item].string());
                int art_start_y = 5;
                for (size_t i = 0; i < asciiArt.size() && (art_start_y + i) < (max_y - 3); ++i) {
                    mvwprintw(info_win, art_start_y + i, 1, "%s", asciiArt[i].c_str());
                }
            }
        }
        
        refresh();
        wrefresh(info_win);
        delwin(info_win);
        
        switch(ch) {
            case KEY_UP:
                if (selected_item > 0) selected_item--;
                break;
            case KEY_DOWN:
                if (selected_item < files.size() - 1) selected_item++;
                break;
            case 10: // Enter key
                if (is_playing) {
                    Pa_StopStream(stream);
                    Pa_CloseStream(stream);
                    Pa_Terminate();
                    sf_close(sf);
                    is_playing = false;
                    is_paused = false;
                }
                
                PlayAudio(files[selected_item].string(), stream, sf, sfinfo);
                is_playing = true;
                break;
            case ' ': // Space key to pause/resume
                if (is_playing) {
                    if (is_paused) {
                        Pa_StartStream(stream);
                        is_paused = false;
                    } else {
                        Pa_StopStream(stream);
                        is_paused = true;
                    }
                }
                break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    nodelay(stdscr, FALSE);

    // Clean up the audio stream before exiting
    if (is_playing) {
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        Pa_Terminate();
        sf_close(sf);
    }
}

int main() {
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
    return 0;
}
