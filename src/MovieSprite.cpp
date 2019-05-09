#include "MovieSprite.h"

#include "oxygine/Material.h"
#include "oxygine/STDRenderDelegate.h"
#include "oxygine/core/oxygine.h"
#include "oxygine/core/Mutex.h"
#include "oxygine/core/ImageDataOperations.h"
#include "oxygine/core/UberShaderProgram.h"
#include "oxygine/RenderState.h"
#include "oxygine/STDRenderer.h"
#include "oxygine/actor/Stage.h"
#include "oxygine/core/ThreadMessages.h"

namespace oxygine
{
    UberShaderProgram* MovieSprite::_shader = 0;

    MovieSprite::MovieSprite() : _dirty(false),
        _completeDispatched(false),
        _bufferSize(0, 0),
        _movieRect(0, 0, 0, 0),
        _initialized(false), _paused(false), _playing(false),
        _looped(false),
        _skipFrames(true),
        _ready(false),
        _hasAlphaChannel(false),
        _detachWhenDone(false)
    {
    }

    MovieSprite::~MovieSprite()
    {
        if (_textureUV)
            _textureUV->release();
        if (_textureYA)
            _textureYA->release();
        //clear();
    }

    void MovieSprite::convert()
    {
        if (_dirty && _textureUV)
        {
            MutexAutoLock l(_mutex);
            _textureUV->updateRegion(0, 0, _mtUV.lock());
            _textureYA->updateRegion(0, 0, _mtYA.lock());
            _dirty = false;
            _ready = true;
        }
    }

    void MovieSprite::doRender(const RenderState& rs)
    {
        convert();

        if (!_ready)
            return;

        Material::null->apply();

        STDRenderer* renderer = STDRenderer::getCurrent();

        renderer->setUberShaderProgram(_shader);
        //renderer->setShaderFlags(UberShaderProgram::ALPHA_PREMULTIPLY);
        renderer->setShaderFlags(0);

        renderer->setTransform(rs.transform);
        IVideoDriver::instance->setUniform("yaScale", &_yaScale, 1);

        Color color = rs.getFinalColor(getColor());

        rsCache().setTexture(0, _textureYA);
        rsCache().setTexture(1, _textureUV);
        rsCache().setBlendMode(blend_alpha);
        renderer->addQuad(color, getAnimFrame().getSrcRect(), getDestRect());

        renderer->flush();
    }

    /*
    void MovieSprite::setBufferSize(const Point& size)
    {
        _bufferSize = size;
    }
    */

    void MovieSprite::initPlayer()
    {
        if (_initialized)
            return;

        _initialized = true;

        _initPlayer();

        Point sz = _bufferSize;
        //sz = Point(nextPOT(sz.x), nextPOT(sz.y));

        Point uvSize = _bufferSize / 2;
        //uvSize = Point(nextPOT(uvSize.x), nextPOT(uvSize.y));
        //uvSize = sz;

        _mtUV.init(uvSize.x, uvSize.y, TF_A8L8);
        _mtUV.fillZero();
        _textureUV = IVideoDriver::instance->createTexture();
        _textureUV->init(uvSize.x, uvSize.y, _mtUV.getFormat(), false);

        _mtYA.init(sz.x, sz.y, _hasAlphaChannel ? TF_A8L8 : TF_L8);
        _mtYA.fillZero();
        _textureYA = IVideoDriver::instance->createTexture();
        _textureYA->init(sz.x, sz.y, _mtYA.getFormat(), false);

        if (_hasAlphaChannel)
            setBlendMode(blend_premultiplied_alpha);
        else
            setBlendMode(blend_disabled);

        Diffuse d;
        d.base = _textureYA;
        d.alpha = _textureUV;

        //OX_ASSERT(0);
        //d.premultiplied = true;

        AnimationFrame frame;
        RectF mr = _movieRect.cast<RectF>();

        Vector2 szf = sz.cast<Vector2>();
        RectF tcYA = RectF(mr.pos.div(szf), mr.size.div(szf));
        frame.init(0, d, tcYA, mr, mr.size);

        _yaScale = Vector2(uvSize.x / szf.x, uvSize.y / szf.y);
        _yaScale = Vector2(0.5f, 0.5f);
        _yaScale = Vector2(1, 1);
        Vector2 s = getSize();
        setAnimFrame(frame);
        setSize(s);
    }

    void MovieSprite::doUpdate(const UpdateState& us)
    {
        _update(us);

        if(_pauseTime >= 0.0f && getCurrentTime() >= _pauseTime)
        {
            if(_looped)
            {
                setCurrentTime(_startTime);
            }else{
                pause();
                clearEndTime();
            }
        }
    }

    void MovieSprite::setVolume(float v)
    {
        _setVolume(v);
    }

    float MovieSprite::getVolume() const
    {
        return _getVolume();
    }

    void MovieSprite::setLooped(bool l)
    {
        _looped = l;
    }

    void MovieSprite::setSkipFrames(bool skip)
    {
        _skipFrames = skip;
    }

    void MovieSprite::setDetachWhenDone(bool detach)
    {
        _detachWhenDone = detach;
    }

    Point MovieSprite::getMovieSize() const
    {
        return _movieRect.getSize();
    }

    float MovieSprite::getMovieLength() const
    {
        return _getMovieLength();
    }

    float MovieSprite::getCurrentTime() const
    {
        return _getCurrentTime();
    }

    void MovieSprite::setCurrentTime(const float s)
    {
        _setCurrentTime(s);
    }

    void MovieSprite::setStartTime(const float t)
    {
        _startTime = t;
    }

    float MovieSprite::getStartTime() const
    {
        return _startTime;
    }

    void MovieSprite::clearStartTime()
    {
        _startTime = 0.0f;
    }

    void MovieSprite::setEndTime(const float t)
    {
        _pauseTime = t;
    }

    float MovieSprite::getEndTime() const
    {
        return _pauseTime;
    }

    void MovieSprite::clearEndTime()
    {
        _pauseTime = -1;
    }

    void MovieSprite::clear()
    {
        _clear();

        _dirty = false;
        _playing = false;
        _paused = false;
        _ready = false;

        if (_textureUV)
            _textureUV->release();
        _textureUV = 0;

        if (_textureYA)
            _textureYA->release();
        _textureYA = 0;

        _movieRect = Rect(0, 0, 0, 0);

        setAnimFrame(0);

        _initialized = false;

        {
            MutexAutoLock m(_mutex);

            _mtUV.cleanup();
            _mtYA.cleanup();
        }
    }

    void MovieSprite::setMovie(const std::string& movie, bool hasAlpha)
    {
        clear();
        _hasAlphaChannel = hasAlpha;
        _fname = movie;
        initPlayer();
    }

    void MovieSprite::play()
    {
        if (_playing)
        {
            if (_paused)
            {
                _resume();
                _paused = false;
                return;
            }

            return;
        }


        _playing = true;


        initPlayer();

        _completeDispatched = false;
        _play();
    }

    void MovieSprite::pause()
    {
        if (_paused)
            return;

        _pause();
        _paused = true;
    }

    void MovieSprite::stop()
    {
        clear();
    }

    bool MovieSprite::isPlaying() const
    {
        return _isLoaded() && _playing && !_paused;
    }

    bool MovieSprite::isLoaded() const
    {
        // defer to implementation
        return _isLoaded();
    }

    void MovieSprite::asyncDone()
    {
        addRef();
        core::getMainThreadDispatcher().postCallback([ = ]()
        {
            _playing = false;

            Event ev(MovieSprite::COMPLETE);
            dispatchEvent(&ev);

            if (_detachWhenDone)
                detach();

            releaseRef();
        });
    }
    void MovieSprite::asyncLoaded()
    {
        addRef();
        core::getMainThreadDispatcher().postCallback([ = ]()
        {
            Event ev(MovieSprite::LOADED);
            dispatchEvent(&ev);

            releaseRef();
        });
    }
}
