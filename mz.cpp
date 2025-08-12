
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <ncurses.h>

// For Audio Playback (using PortAudio)
#include <portaudio.h>

// For Audio File Decoding (using libsndfile)
#include <sndfile.h>

// For MP3 Metadata and Tagging (using TagLib)
#include <taglib/fileref.h>
#include <taglib/tag.h>

// For ASCII Art (using Libcaca)
#include <caca.h>

namespace fs = std::filesystem;

// The callback function for PortAudio
// This function feeds audio data from libsndfile to the PortAudio stream
static int paCallback(const void *inputBuffer, void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData)
{
    SNDFILE* sf = (SNDFILE*)userData;
    sf_count_t framesRead = sf_readf_short(sf, (short*)outputBuffer, framesPerBuffer);

    if (framesRead > 0) {
        return paContinue;
    } else {
        return paComplete; // End playback
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
        std::cerr << "Error: Could not open audio file." << std::endl;
        return;
    }

    pa_err = Pa_Initialize();
    if (pa_err != paNoError) {
        std::cerr << "PortAudio Error: " << Pa_GetErrorText(pa_err) << std::endl;
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
        std::cerr << "PortAudio Error: " << Pa_GetErrorText(pa_err) << std::endl;
        Pa_Terminate();
        sf_close(sf);
        return;
    }

    // Start playback
    pa_err = Pa_StartStream(stream);
    if (pa_err != paNoError) {
        std::cerr << "PortAudio Error: " << Pa_GetErrorText(pa_err) << std::endl;
        Pa_CloseStream(stream);
        Pa_Terminate();
        sf_close(sf);
        return;
    }

    std::cout << "Playing: " << file_path << std::endl;
    std::cout << "Press Enter to stop playback..." << std::endl;
    std::cin.ignore();

    // Stop playback
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    sf_close(sf);
}

// The main TUI function
std::string runTui() {
    initscr();
    clear();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);

    std::string path_str;
    printw("Please enter the path to your music directory and press Enter:\n");
    int ch;
    int input_y = 1;
    int input_x = 0;

    while ((ch = getch()) != '\n') {
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (!path_str.empty()) {
                path_str.pop_back();
                move(input_y, --input_x);
                delch();
            }
        } else {
            path_str += ch;
            mvaddch(input_y, input_x++, ch);
        }
    }

    fs::path music_directory(path_str);
    std::vector<fs::path> files;

    if (fs::exists(music_directory) && fs::is_directory(music_directory)) {
        for (const auto& entry : fs::directory_iterator(music_directory)) {
            // Check if the file is a WAV file for this example
            if (entry.path().extension() == ".mp3") {
                files.push_back(entry.path());
            }
        }
    } else {
        endwin();
        std::cerr << "Error: Not a valid directory." << std::endl;
        return "";
    }

    clear();
    
    if (files.empty()) {
        endwin();
        std::cerr << "No .mp3 files found in the directory." << std::endl;
        return "";
    }
    
    int selected_item = 0;
    ch = 0;

    while (ch != '\n') {
        clear();
        for (size_t i = 0; i < files.size(); ++i) {
            if (i == selected_item) {
                attron(A_REVERSE);
                mvprintw(i, 0, "%s", files[i].filename().string().c_str());
                attroff(A_REVERSE);
            } else {
                mvprintw(i, 0, "%s", files[i].filename().string().c_str());
            }
        }
        
        refresh();
        
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
        }
    }
    
    endwin();
    return files[selected_item].string();
}

int main() {
    std::string selected_file_path = runTui();

    if (!selected_file_path.empty()) {
        PlayAudio(selected_file_path);
    } else {
        std::cout << "No file selected." << std::endl;
    }

    return 0;
}






