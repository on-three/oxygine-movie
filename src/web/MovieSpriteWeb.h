#pragma once
#include "MovieSprite.h"
#include "oxygine/core/file.h"
#include "oxygine/core/gl/oxgl.h" // for GLuint

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
    void setFrameLoaded(bool v) {
        _frameLoaded = v;
        // we've got a first frame so consider the video 'loaded'
        oxygine::MovieSprite::asyncLoaded();
    };

    void createVideoTexture(const int width, const int height);

    protected:
        void _initPlayer() OVERRIDE;
        void _play() OVERRIDE;
        void _pause() OVERRIDE;
        void _resume() OVERRIDE;
        void _setVolume(float v)  OVERRIDE;
        float _getVolume() const OVERRIDE;
        void _stop() OVERRIDE;
        bool _isPlaying() const  OVERRIDE;
        void _clear() OVERRIDE;
        void _update(const UpdateState&) OVERRIDE;

        // Override base impl to use video texture right from video player
        void doRender(const RenderState& rs) OVERRIDE;

        bool _frameLoaded = false;

        // Native video texture allocated by javascript
        GLuint _videoTextureID = 0;
        spNativeTexture _videoTexture;
    };
}