#pragma once
#include <string>

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    void load(const std::string& soundName);
    void stop(const std::string& soundName);

private:
    void initOpenAL();
    void cleanupOpenAL();
    void generateProceduralSounds();

    bool m_openALActive = false;
    void* m_device = nullptr;  // ALCdevice*
    void* m_context = nullptr; // ALCcontext*

    unsigned int m_buffers[6] = {0};
    unsigned int m_sources[6] = {0};
};