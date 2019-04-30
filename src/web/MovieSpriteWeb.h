#pragma once
#include "MovieSprite.h"
#include "oxygine/core/file.h"

namespace oxygine
{
    class MovieSpriteWeb: public MovieSprite
    {
    public:
        MovieSpriteWeb();
        ~MovieSpriteWeb();

    public:
    void setBufferSize(int width, int height)
    {
        _bufferSize = Point(width, height);
    }
    bool getFrameLoaded() const { return _frameLoaded;};
    void setFrameLoaded(bool v) {_frameLoaded = v;};

    protected:
        void _initPlayer() OVERRIDE;
        void _play() OVERRIDE;
        void _pause() OVERRIDE;
        void _resume() OVERRIDE;
        void _setVolume(float v)  OVERRIDE;
        void _stop() OVERRIDE;
        bool _isPlaying() const  OVERRIDE;
        void _clear() OVERRIDE;
        void _update(const UpdateState&) OVERRIDE;

        bool _frameLoaded = false;

    };
}