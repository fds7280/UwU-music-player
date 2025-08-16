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
#include <sstream>
#include <array>
#include <memory>
#include <cstdio>
#include <sys/stat.h>  // for mkfifo
#include <unistd.h>    // for unlink

// For Audio Playback (using PipeWire)
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

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

// Search result structure
struct SearchResult {
    std::string id;
    std::string title;
};

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

// PipeWire data structure
struct PWData {
    struct pw_main_loop *loop;
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    
    SNDFILE* sf;
    SF_INFO sfinfo;
    
    std::thread loop_thread;
    std::atomic<bool> should_stop{false};
};

// Global PipeWire data pointer
PWData* g_pw_data = nullptr;

// Forward declaration
void StopAudio();

// PipeWire stream process callback
static void on_process(void *userdata) {
    PWData *data = static_cast<PWData*>(userdata);
    struct pw_buffer *b;
    struct spa_buffer *buf;
    float *dst;
    uint32_t n_frames;
    
    if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL) {
        pw_log_warn("out of buffers: %m");
        return;
    }
    
    buf = b->buffer;
    if ((dst = (float*)buf->datas[0].data) == NULL)
        return;
    
    n_frames = buf->datas[0].maxsize / sizeof(float) / data->sfinfo.channels;
    
    if (is_paused) {
        memset(dst, 0, n_frames * sizeof(float) * data->sfinfo.channels);
    } else {
        // Read audio data from file
        sf_count_t frames_read = sf_readf_float(data->sf, dst, n_frames);
        current_frame = sf_seek(data->sf, 0, SF_SEEK_CUR);
        
        if (frames_read < n_frames) {
            // Fill remaining with silence
            memset(dst + frames_read * data->sfinfo.channels, 0, 
                   (n_frames - frames_read) * sizeof(float) * data->sfinfo.channels);
            if (frames_read == 0) {
                is_playing = false;
            }
        }
    }
    
    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = sizeof(float) * data->sfinfo.channels;
    buf->datas[0].chunk->size = n_frames * sizeof(float) * data->sfinfo.channels;
    
    pw_stream_queue_buffer(data->stream, b);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

// Function to play audio using libsndfile and PipeWire
void PlayAudio(const std::string& file_path) {
    // ONLY CHANGE: Stop any existing playback first
    if (g_pw_data) {
        StopAudio();
    }
    
    g_pw_data = new PWData();
    memset(&g_pw_data->sfinfo, 0, sizeof(g_pw_data->sfinfo));
    
    g_pw_data->sf = sf_open(file_path.c_str(), SFM_READ, &g_pw_data->sfinfo);
    if (!g_pw_data->sf) {
        delete g_pw_data;
        g_pw_data = nullptr;
        return;
    }
    
    // Get total frames and reset to start
    sf_seek(g_pw_data->sf, 0, SF_SEEK_END);
    total_frames = sf_seek(g_pw_data->sf, 0, SF_SEEK_CUR);
    sf_seek(g_pw_data->sf, 0, SF_SEEK_SET);
    current_frame = 0;
    
    // Initialize PipeWire
    pw_init(nullptr, nullptr);
    
    g_pw_data->loop = pw_main_loop_new(nullptr);
    if (!g_pw_data->loop) {
        sf_close(g_pw_data->sf);
        delete g_pw_data;
        g_pw_data = nullptr;
        return;
    }
    
    // Create stream
    g_pw_data->stream = pw_stream_new_simple(
        pw_main_loop_get_loop(g_pw_data->loop),
        "Music Player",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Music",
            nullptr),
        &stream_events,
        g_pw_data);
    
    if (!g_pw_data->stream) {
        pw_main_loop_destroy(g_pw_data->loop);
        sf_close(g_pw_data->sf);
        delete g_pw_data;
        g_pw_data = nullptr;
        return;
    }
    
    // Setup audio format
    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[1];
    
    struct spa_audio_info_raw spa_audio_info = {};
    spa_audio_info.format = SPA_AUDIO_FORMAT_F32;
    spa_audio_info.rate = g_pw_data->sfinfo.samplerate;
    spa_audio_info.channels = g_pw_data->sfinfo.channels;
    
    // Set channel positions
    if (g_pw_data->sfinfo.channels == 1) {
        spa_audio_info.position[0] = SPA_AUDIO_CHANNEL_MONO;
    } else if (g_pw_data->sfinfo.channels == 2) {
        spa_audio_info.position[0] = SPA_AUDIO_CHANNEL_FL;
        spa_audio_info.position[1] = SPA_AUDIO_CHANNEL_FR;
    }
    
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &spa_audio_info);
    
    // Connect stream
    pw_stream_connect(g_pw_data->stream,
                      PW_DIRECTION_OUTPUT,
                      PW_ID_ANY,
                      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                                   PW_STREAM_FLAG_MAP_BUFFERS |
                                                   PW_STREAM_FLAG_RT_PROCESS),
                      params, 1);
   // Start the main loop in a separate thread
    g_pw_data->loop_thread = std::thread([&]() {
        pw_main_loop_run(g_pw_data->loop);
    });
}

void StopAudio() {
    if (!g_pw_data) return;

    is_playing = false;
    is_paused = false;

    g_pw_data->should_stop = true;
    if (g_pw_data->loop) {
        pw_main_loop_quit(g_pw_data->loop);
    }
    if (g_pw_data->loop_thread.joinable()) {
        g_pw_data->loop_thread.join();
    }
    if (g_pw_data->stream) pw_stream_destroy(g_pw_data->stream);
    if (g_pw_data->loop) pw_main_loop_destroy(g_pw_data->loop);
    if (g_pw_data->sf) sf_close(g_pw_data->sf);
    
    delete g_pw_data;
    g_pw_data = nullptr;
    
    total_frames = 0;
    current_frame = 0;
}

// --- YouTube Streaming & Caching ---

// Function to run a command and capture its output
std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, int(*)(FILE*)> pipe(popen(cmd, "r"), pclose); 
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// Get search query from user in a popup box
std::string get_search_query(int max_y, int max_x) {
    // Create a centered window for the search bar
    int height = 3;
    int width = max_x / 2;
    int start_y = (max_y - height) / 2;
    int start_x = (max_x - width) / 2;
    
    WINDOW* search_win = newwin(height, width, start_y, start_x);
    box(search_win, 0, 0);
    mvwprintw(search_win, 1, 1, "Search YouTube: ");
    wrefresh(search_win);
    
    echo(); // Temporarily turn echo on to see what we type
    char str[200];
    wgetstr(search_win, str);
    noecho(); // Turn it back off
    
    delwin(search_win);
    return std::string(str);
}

// Search YouTube and return a list of results
std::vector<SearchResult> search_youtube(const std::string& query) {
    std::vector<SearchResult> results;
    if (query.empty()) return results;

    // Construct the yt-dlp command for searching
    // Get results in JSON format for easier parsing
    std::string command = "yt-dlp \"ytsearch5:" + query + "\" --flat-playlist -j --no-warnings 2>/dev/null";
    
    mvprintw(LINES - 1, 0, "Searching...");
    refresh();

    std::string output = exec(command.c_str());
    
    // Clear "Searching..." message
    move(LINES - 1, 0);
    clrtoeol();
    refresh();

    std::stringstream ss(output);
    std::string line;
    
    // Parse JSON output (simple parsing for id and title)
    while (std::getline(ss, line)) {
        SearchResult result;
        
        // Find "id": "xxxxx"
        size_t id_pos = line.find("\"id\": \"");
        if (id_pos != std::string::npos) {
            id_pos += 7; // Skip past "id": "
            size_t id_end = line.find("\"", id_pos);
            if (id_end != std::string::npos) {
                result.id = line.substr(id_pos, id_end - id_pos);
            }
        }
        
        // Find "title": "xxxxx"
        size_t title_pos = line.find("\"title\": \"");
        if (title_pos != std::string::npos) {
            title_pos += 10; // Skip past "title": "
            size_t title_end = line.find("\"", title_pos);
            if (title_end != std::string::npos) {
                result.title = line.substr(title_pos, title_end - title_pos);
            }
        }
        
        if (!result.id.empty() && !result.title.empty()) {
            results.push_back(result);
        }
    }

    return results;
}

// Let the user select a song from the results and return the chosen one
SearchResult select_from_results(const std::vector<SearchResult>& results) {
    if (results.empty()) {
        mvprintw(0, 0, "No results found. Press any key to search again.");
        refresh();
        getch();
        return {};
    }

    int highlight = 0;
    int choice = -1;

    while (true) {
        clear();
        mvprintw(0, 0, "YouTube Search Results (Select with Enter, Esc to cancel):");
        for (size_t i = 0; i < results.size(); ++i) {
            if (i == highlight) attron(A_REVERSE);
            mvprintw(i + 2, 1, "%s", results[i].title.c_str());
            if (i == highlight) attroff(A_REVERSE);
        }
        refresh();

        int ch = getch();
        switch(ch) {
            case KEY_UP:
                highlight = (highlight > 0) ? highlight - 1 : results.size() - 1;
                break;
            case KEY_DOWN:
                highlight = (highlight < results.size() - 1) ? highlight + 1 : 0;
                break;
            case 10: // Enter
                choice = highlight;
                goto end_loop; // Break out of the while loop
            case 27: // Escape
                return {};
        }
    }

end_loop:
    return results[choice];
}


void progressive_stream_youtube(const std::string& video_id, const std::string& title, const std::string& cache_dir) {
    clear();
    int max_y, max_x;
    
    // PROPER FIX: Stop audio AND kill background processes
    if (g_pw_data) {
        StopAudio();
    }
    // Kill any existing streaming processes
    system("pkill -f yt-dlp");
    system("pkill -f ffmpeg");
    // Small delay to ensure processes are killed
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    getmaxyx(stdscr, max_y, max_x);
    
    std::string final_file = cache_dir + "/" + video_id + ".mp3";
    std::string fifo_path = "/tmp/moz_stream_" + video_id + ".fifo";
    
    // Remove old FIFO if exists
    unlink(fifo_path.c_str());
    
    // Create named pipe
    if (mkfifo(fifo_path.c_str(), 0666) != 0) {
        mvprintw(max_y/2, (max_x-30)/2, "Failed to create pipe!");
        refresh();
        getch();
        return;
    }
    
    // Create status window
    WINDOW* status_win = newwin(6, 60, max_y/2 - 3, max_x/2 - 30);
    box(status_win, 0, 0);
    mvwprintw(status_win, 1, 2, "Streaming: %.50s", title.c_str());
    mvwprintw(status_win, 2, 2, "Starting stream...");
    mvwprintw(status_win, 3, 2, "Buffering...");
    wrefresh(status_win);
    
    // Start yt-dlp streaming to FIFO and also save to cache
    std::string dl_command = "yt-dlp -f 'bestaudio[ext=m4a]/bestaudio/best' -o - \"https://youtube.com/watch?v=" + 
                            video_id + "\" 2>/dev/null | ffmpeg -i pipe:0 -acodec libmp3lame -ab 192k -f mp3 - 2>/dev/null | " +
                            "tee \"" + final_file + "\" > \"" + fifo_path + "\" &";
    
    int result = system(dl_command.c_str());
    if (result != 0) {
        mvwprintw(status_win, 4, 2, "Failed to start stream!");
        wrefresh(status_win);
        getch();
        delwin(status_win);
        unlink(fifo_path.c_str());
        return;
    }
    
    // Give it a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    mvwprintw(status_win, 4, 2, "Starting playback in 2 seconds...");
    wrefresh(status_win);
    
    // Wait a bit for initial buffer
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    delwin(status_win);
    
    // Start playing from FIFO
    is_playing = true;
    is_paused = false;
    
    // Start playback in a separate thread
    std::thread play_thread([fifo_path]() {
        PlayAudio(fifo_path);
    });
    
    // Simple playback control loop
    clear();
    mvprintw(0, 0, "Now Streaming: %s", title.c_str());
    mvprintw(2, 0, "Press 'q' to stop, SPACE to pause/resume.");
    mvprintw(3, 0, "Streaming in real-time...");
    nodelay(stdscr, TRUE);
    
    while(is_playing) {
        int ch = getch();
        if (ch == 'q') {
            is_playing = false;
            // Kill the streaming pipeline
            system("pkill -f yt-dlp");
            system("pkill -f ffmpeg");
        } else if (ch == ' ') {
            is_paused = !is_paused;
            mvprintw(4, 0, is_paused ? "PAUSED " : "PLAYING");
            clrtoeol();
        }
        
        // Check if cache file is growing
        if (fs::exists(final_file)) {
            size_t size = fs::file_size(final_file);
            mvprintw(5, 0, "Cached: %zu KB", size / 1024);
            clrtoeol();
        }
        
        // Show current playback time if available
        if (g_pw_data && g_pw_data->sfinfo.samplerate > 0) {
            long current_seconds = static_cast<long>(static_cast<float>(current_frame) / g_pw_data->sfinfo.samplerate);
            mvprintw(6, 0, "Time: %02ld:%02ld", current_seconds / 60, current_seconds % 60);
            clrtoeol();
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    nodelay(stdscr, FALSE);
    
    // Wait for playback thread
    if (play_thread.joinable()) {
        play_thread.join();
    }
    
    // Cleanup
    unlink(fifo_path.c_str());
}



// The main loop for online mode
void run_online_mode() {
    // Define and create the cache directory
    const std::string cache_dir = std::string(getenv("HOME")) + "/.tui_player_cache";
    fs::create_directories(cache_dir);

    while(true) {
        clear();
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        std::string query = get_search_query(max_y, max_x);
        if (query.empty()) break; // User might have hit Ctrl+C or something

        auto results = search_youtube(query);
        SearchResult selection = select_from_results(results);

        if (selection.id.empty()) continue; // User escaped selection

        // Check if already fully cached
        std::string cached_file_path = cache_dir + "/" + selection.id + ".mp3";
        if (fs::exists(cached_file_path)) {
            // PROPER FIX: Stop everything before playing cached file
            if (g_pw_data) {
                StopAudio();
            }
            system("pkill -f yt-dlp");
            system("pkill -f ffmpeg");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // File already cached, play immediately
            is_playing = true;
            is_paused = false;
            PlayAudio(cached_file_path);
            
            // Simple playback loop for cached files
            clear();
            mvprintw(0, 0, "Now Playing (Cached): %s", selection.title.c_str());
            mvprintw(2, 0, "Press 'q' to stop and search again, SPACE to pause/resume.");
            nodelay(stdscr, TRUE);
            
            while(is_playing) {
                int ch = getch();
                if (ch == 'q') {
                    is_playing = false;
                } else if (ch == ' ') {
                    is_paused = !is_paused;
                    mvprintw(3, 0, is_paused ? "PAUSED " : "PLAYING");
                    clrtoeol();
                }
                
                // Show current playback time if available
                if (g_pw_data && g_pw_data->sfinfo.samplerate > 0 && total_frames > 0) {
                    long current_seconds = static_cast<long>(static_cast<float>(current_frame) / g_pw_data->sfinfo.samplerate);
                    long total_seconds = static_cast<long>(static_cast<float>(total_frames) / g_pw_data->sfinfo.samplerate);
                    mvprintw(4, 0, "Time: %02ld:%02ld / %02ld:%02ld", 
                             current_seconds / 60, current_seconds % 60,
                             total_seconds / 60, total_seconds % 60);
                    clrtoeol();
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            nodelay(stdscr, FALSE);
        } else {
            // Stream progressively
            progressive_stream_youtube(selection.id, selection.title, cache_dir);
        }
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
            if (total_frames > 0 && progress_y < max_y - 4 && g_pw_data && g_pw_data->sfinfo.samplerate > 0) {
                float progress = static_cast<float>(current_frame) / total_frames;
                int bar_width = std::min(THUMBNAIL_WIDTH, max_x / 2 - 4);
                int progress_bar_fill = static_cast<int>(bar_width * progress);
                
                mvwprintw(info_win, progress_y, 1, "[");
                for (int i = 0; i < bar_width; ++i) {
                    waddch(info_win, i < progress_bar_fill ? '#' : '-');
                }
                wprintw(info_win, "] %d%%", static_cast<int>(progress * 100));

                long total_seconds = static_cast<long>(static_cast<float>(total_frames) / g_pw_data->sfinfo.samplerate);
                long current_seconds = static_cast<long>(static_cast<float>(current_frame) / g_pw_data->sfinfo.samplerate);

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
                    is_playing = false;
                    is_paused = false;
                }
                
                PlayAudio(files[selected_item].string());
                is_playing = true;
                break;
            case ' ': // Space key to pause/resume
                if (is_playing) {
                    is_paused = !is_paused;
                }
                break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    nodelay(stdscr, FALSE);

    // Clean up the audio stream before exiting
    if (g_pw_data) {
        g_pw_data->should_stop = true;
        pw_main_loop_quit(g_pw_data->loop);
        
        if (g_pw_data->loop_thread.joinable()) {
            g_pw_data->loop_thread.join();
        }
        
        if (g_pw_data->stream) {
            pw_stream_destroy(g_pw_data->stream);
        }
        if (g_pw_data->loop) {
            pw_main_loop_destroy(g_pw_data->loop);
        }
        if (g_pw_data->sf) {
            sf_close(g_pw_data->sf);
        }
        delete g_pw_data;
        g_pw_data = nullptr;
    }
    
    pw_deinit();
}

// A new function to ask the user for the mode
int prompt_mode_selection() {
    int highlight = 0;
    const char* choices[] = {"Offline Library", "Online (YouTube)"};
    
    while(true) {
        clear();
        mvprintw(LINES / 2 - 2, (COLS - 20) / 2, "Select a mode:");
        for(int i = 0; i < 2; ++i) {
            if (i == highlight) attron(A_REVERSE);
            mvprintw(LINES / 2 + i, (COLS - strlen(choices[i])) / 2, "%s", choices[i]);
            if (i == highlight) attroff(A_REVERSE);
        }
        refresh();

        int ch = getch();
        switch(ch) {
            case KEY_UP:
            case KEY_DOWN:
                highlight = !highlight;
                break;
            case 10: // Enter
                return highlight;
        }
    }
}

int main() {
    initscr();
    start_color();
    init_pair(1, COLOR_YELLOW, COLOR_BLACK);
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0); // Hide the cursor

    int mode = prompt_mode_selection();
    
    clear();
    refresh();

    if (mode == 0) { // Offline
        std::string music_dir = run_file_browser("/home");
        if (!music_dir.empty()) {
            run_playback_tui(music_dir);
        }
    } else { // Online
        run_online_mode();
    }
    
    endwin();
    
    // Final cleanup of audio resources
    if (g_pw_data) {
        pw_main_loop_quit(g_pw_data->loop);
        if (g_pw_data->loop_thread.joinable()) {
            g_pw_data->loop_thread.join();
        }
        pw_stream_destroy(g_pw_data->stream);
        pw_main_loop_destroy(g_pw_data->loop);
        sf_close(g_pw_data->sf);
        delete g_pw_data;
        pw_deinit();
    }

    return 0;
}
