#pragma once
#include "MovieSprite.h"
#include "oxygine/core/file.h"
#include "pthread.h"
#include "oxygine/core/ThreadMessages.h"

namespace oxygine
{
    class OggDecoder;
    class OggDecoder2;

    class MovieSpriteTheora: public MovieSprite
    {
    public:
        MovieSpriteTheora();
        ~MovieSpriteTheora();

    protected:
        static void* _thread(void* This);
        void _threadThis();

        void _initPlayer() OVERRIDE;
        void _play() OVERRIDE;
        void _pause() OVERRIDE;
        void _resume() OVERRIDE;
        void _setVolume(float v)  OVERRIDE;
        float _getVolume() const OVERRIDE;
        void _stop() OVERRIDE;
        bool _isPlaying() const  OVERRIDE;
        bool _isLoaded() const OVERRIDE;
        float _getMovieLength() const OVERRIDE;
        float _getCurrentTime() const OVERRIDE;
        void _setCurrentTime(const float s) OVERRIDE;
        void _clear() OVERRIDE;
        void _update(const UpdateState&) OVERRIDE;

        OggDecoder* _decoder;
        file::handle _file;
        pthread_t _threadID;

        ThreadMessages _msg;
    };
}