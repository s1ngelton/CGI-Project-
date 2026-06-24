#include "AudioManager.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <random>

#ifdef HAS_OPENAL
#include <AL/al.h>
#include <AL/alc.h>
#endif

AudioManager::AudioManager() {
    initOpenAL();
}

AudioManager::~AudioManager() {
    cleanupOpenAL();
}

void AudioManager::initOpenAL() {
#ifdef HAS_OPENAL
    std::cout << "[Audio] Initializing OpenAL...\n";
    ALCdevice* device = alcOpenDevice(nullptr);
    if (!device) {
        std::cerr << "[Audio] Failed to open default OpenAL device\n";
        return;
    }
    m_device = device;

    ALCcontext* context = alcCreateContext(device, nullptr);
    if (!context) {
        std::cerr << "[Audio] Failed to create OpenAL context\n";
        alcCloseDevice(device);
        m_device = nullptr;
        return;
    }
    m_context = context;

    if (!alcMakeContextCurrent(context)) {
        std::cerr << "[Audio] Failed to make OpenAL context current\n";
        alcDestroyContext(context);
        alcCloseDevice(device);
        m_device = nullptr;
        m_context = nullptr;
        return;
    }

    m_openALActive = true;
    std::cout << "[Audio] OpenAL successfully initialized. Generating procedural audio...\n";
    generateProceduralSounds();
#else
    std::cout << "[Audio] OpenAL not compiled into project\n";
#endif
}

void AudioManager::generateProceduralSounds() {
#ifdef HAS_OPENAL
    if (!m_openALActive) return;

    alGenBuffers(6, m_buffers);
    alGenSources(6, m_sources);

    const int sampleRate = 22050;
    std::default_random_engine generator;
    std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);

    // --- 0. TV Static (White Noise, Loopable) ---
    {
        int durationSamples = sampleRate * 2; // 2 seconds
        std::vector<short> pcm(durationSamples);
        for (int i = 0; i < durationSamples; ++i) {
            float val = distribution(generator) * 0.2f; // Keep volume moderate
            pcm[i] = static_cast<short>(val * 32767.0f);
        }
        alBufferData(m_buffers[0], AL_FORMAT_MONO16, pcm.data(), pcm.size() * sizeof(short), sampleRate);
        alSourcei(m_sources[0], AL_BUFFER, m_buffers[0]);
        alSourcei(m_sources[0], AL_LOOPING, AL_TRUE);
        alSourcef(m_sources[0], AL_GAIN, 0.25f); // Lower static volume
        alSourcef(m_sources[1], AL_GAIN, 1.5f);  // Thunder volume
        alSourcef(m_sources[2], AL_GAIN, 1.0f);  // Door opening volume
        alSourcef(m_sources[3], AL_GAIN, 1.5f);  // Fuse blow volume
        alSourcef(m_sources[4], AL_GAIN, 3.5f);  // Massive kick volume boost!
        alSourcef(m_sources[5], AL_GAIN, 0.45f); // Footstep volume
    }

    // --- 1. Thunder (Low-frequency Brown Noise Rumble) ---
    {
        int durationSamples = sampleRate * 4; // 4 seconds
        std::vector<short> pcm(durationSamples);
        float lastVal = 0.0f;
        for (int i = 0; i < durationSamples; ++i) {
            float white = distribution(generator);
            // Brown noise filter (integration / lowpass)
            float brown = (lastVal + (0.05f * white)) / 1.05f;
            lastVal = brown;
            
            // Decaying envelope
            float t = (float)i / durationSamples;
            float env = std::exp(-t * 3.0f); // Fast decay
            
            float val = brown * env * 0.8f;
            pcm[i] = static_cast<short>(val * 32767.0f);
        }
        alBufferData(m_buffers[1], AL_FORMAT_MONO16, pcm.data(), pcm.size() * sizeof(short), sampleRate);
        alSourcei(m_sources[1], AL_BUFFER, m_buffers[1]);
    }

    // --- 2. Door/Fridge Open (Latch Click + Creak + Suction Pop) ---
    {
        int durationSamples = sampleRate * 1.5f;
        std::vector<short> pcm(durationSamples);
        for (int i = 0; i < durationSamples; ++i) {
            float t = (float)i / sampleRate;
            // A sharp metallic/plastic latch click at the very start (t < 0.05s)
            float click = std::sin(2.0f * M_PI * 1200.0f * t) * std::exp(-80.0f * t) * 0.4f;
            // High frequency door creak sweeping down
            float creak = std::sin(2.0f * M_PI * (700.0f - t * 300.0f) * t) * std::exp(-t * 3.5f) * 0.25f;
            // Deep suction pop/vacuum release (t < 0.15s)
            float pop = std::sin(2.0f * M_PI * 55.0f * t) * std::exp(-25.0f * t) * 0.35f;
            
            float val = click + creak + pop;
            pcm[i] = static_cast<short>(val * 32767.0f);
        }
        alBufferData(m_buffers[2], AL_FORMAT_MONO16, pcm.data(), pcm.size() * sizeof(short), sampleRate);
        alSourcei(m_sources[2], AL_BUFFER, m_buffers[2]);
    }

    // --- 3. Trip Bang (Low-frequency thud) ---
    {
        int durationSamples = sampleRate * 1.0f;
        std::vector<short> pcm(durationSamples);
        for (int i = 0; i < durationSamples; ++i) {
            float t = (float)i / sampleRate;
            // Low boom
            float boom = std::sin(2.0f * M_PI * 50.0f * t) * std::exp(-t * 8.0f) * 0.9f;
            pcm[i] = static_cast<short>(boom * 32767.0f);
        }
        alBufferData(m_buffers[3], AL_FORMAT_MONO16, pcm.data(), pcm.size() * sizeof(short), sampleRate);
        alSourcei(m_sources[3], AL_BUFFER, m_buffers[3]);
    }

    // --- 4. Kick / Foot Impact (Cinematic "BUUMM" explosion/thud) ---
    {
        int durationSamples = sampleRate * 0.8f; // Longer duration for the "Bumm" decay
        std::vector<short> pcm(durationSamples);
        for (int i = 0; i < durationSamples; ++i) {
            float t = (float)i / sampleRate;
            
            // Initial impact crack (metal/plastic pop)
            float snap = std::sin(2.0f * M_PI * 800.0f * t) * std::exp(-60.0f * t) * 0.4f;
            
            // Main booming sweep (130Hz down to 30Hz)
            float sweepFreq = 30.0f + 100.0f * std::exp(-12.0f * t);
            float boom = std::sin(2.0f * M_PI * sweepFreq * t) * std::exp(-4.0f * t) * 0.85f;
            
            // Deep sub-bass rumble
            float rumble = std::sin(2.0f * M_PI * 45.0f * t) * std::exp(-2.5f * t) * 0.25f;
            
            float val = snap + boom + rumble;
            pcm[i] = static_cast<short>(val * 32767.0f);
        }
        alBufferData(m_buffers[4], AL_FORMAT_MONO16, pcm.data(), pcm.size() * sizeof(short), sampleRate);
        alSourcei(m_sources[4], AL_BUFFER, m_buffers[4]);
    }
    
    // --- 5. Footstep (Short wood floor creak/impact) ---
    {
        int durationSamples = sampleRate * 0.35f; // 0.35 seconds
        std::vector<short> pcm(durationSamples);
        for (int i = 0; i < durationSamples; ++i) {
            float t = (float)i / sampleRate;
            
            // Impact thud (low frequency shoe heel)
            float thud = std::sin(2.0f * M_PI * 110.0f * t) * std::exp(-22.0f * t) * 0.4f;
            
            // Wood creak noise (low pass filtered noise)
            float woodNoise = distribution(generator) * 0.08f;
            float creak = woodNoise * std::exp(-12.0f * t);
            
            float val = thud + creak;
            pcm[i] = static_cast<short>(val * 32767.0f);
        }
        alBufferData(m_buffers[5], AL_FORMAT_MONO16, pcm.data(), pcm.size() * sizeof(short), sampleRate);
        alSourcei(m_sources[5], AL_BUFFER, m_buffers[5]);
    }
#endif
}

void AudioManager::cleanupOpenAL() {
#ifdef HAS_OPENAL
    if (m_openALActive) {
        for (int i = 0; i < 6; ++i) {
            alSourceStop(m_sources[i]);
        }
        alDeleteSources(6, m_sources);
        alDeleteBuffers(6, m_buffers);

        alcMakeContextCurrent(nullptr);
        alcDestroyContext(static_cast<ALCcontext*>(m_context));
        alcCloseDevice(static_cast<ALCdevice*>(m_device));
    }
#endif
}

void AudioManager::stop(const std::string& soundName) {
#ifdef HAS_OPENAL
    if (!m_openALActive) return;

    if (soundName == "tv_static") {
        alSourceStop(m_sources[0]);
    } else if (soundName == "thunder") {
        alSourceStop(m_sources[1]);
    } else if (soundName == "fridge_open") {
        alSourceStop(m_sources[2]);
    } else if (soundName == "trip_bang") {
        alSourceStop(m_sources[3]);
    } else if (soundName == "kick") {
        alSourceStop(m_sources[4]);
    } else if (soundName == "footstep") {
        alSourceStop(m_sources[5]);
    }
#endif
}

void AudioManager::load(const std::string& soundName) {
    std::cout << "[AUDIO CUE] Triggered sound: " << soundName << std::endl;

#ifdef HAS_OPENAL
    if (!m_openALActive) return;

    if (soundName == "tv_static") {
        alSourcePlay(m_sources[0]);
    } else if (soundName == "thunder") {
        alSourcePlay(m_sources[1]);
    } else if (soundName == "fridge_open") {
        alSourcePlay(m_sources[2]);
    } else if (soundName == "trip_bang") {
        // Stop TV static on trip bang (going black)
        alSourceStop(m_sources[0]);
        alSourcePlay(m_sources[3]);
    } else if (soundName == "kick") {
        alSourcePlay(m_sources[4]);
    } else if (soundName == "footstep") {
        alSourcePlay(m_sources[5]);
    }
#endif
}
