#include "MovieSpriteWeb.h"
//#include "ogg/ogg.h"
/* #include "theora/theora.h"
#include "theora/theoradec.h" */
#include <map>
/* #include "oxygine/core/Mutex.h" */
#include "oxygine/core/oxygine.h"
#include <fstream>
/* #include "oxygine/core/ThreadMessages.h"
#include "pthread.h" */
#include "oxygine/core/ImageDataOperations.h"
#include "oxygine/core/UberShaderProgram.h"
#include "oxygine/STDRenderer.h"
//#include "ivorbiscodec.h"
#include <time.h>
#include "oxygine/utils/AtlasBuilder.h"
#include "oxygine/utils/ImageUtils.h"
#include "oxygine/utils/stringUtils.h"
#include "oxygine/res/CreateResourceContext.h"
#include "oxygine/json/json.h"
#include "oxygine/utils/cdecode.h"
//#include "core/gl/oxgl.h"

#include "emscripten.h"

#define PREMULT_MOVIE 0

namespace oxygine
{
    const int MSG_PAUSE = 1;
    const int MSG_STOP = 2;
    const int MSG_RESUME = 3;
    const int MSG_WAIT_FIRST_FRAME = 4;


    inline unsigned char Clamp2Byte(int n)
    {
        n &= -(n >= 0);
        return n | ((255 - n) >> 31);
    }

    using namespace std;

    ResAnim* createResAnimFromMovie(const string& name, const Point& atlasSize, TextureFormat tf)
    {
        ResAnim* rs = new ResAnim;
        #if 0
        ResAnimTheoraPacker p(atlasSize, tf);
        p.prepare(name, 0);
        p.decode(rs);
        #endif
        return rs;
    }

    void MovieSprite::init(bool highQualityShader)
    {
        #if 0
        _shader = new UberShaderProgram();

        string base =
            "#ifdef PS\n"
            "uniform lowp vec2 yaScale;\n"
            "lowp vec4 replaced_get_base()\n"
            "{\n"
            "lowp vec4 ya = texture2D(base_texture,  result_uv);\n"
            "lowp vec4 uv = texture2D(alpha_texture, result_uv * yaScale);\n"
            "lowp float  y = ya.r;\n"
            "lowp float  u = uv.a - 0.5;\n"
            "lowp float  v = uv.r - 0.5;\n";


        if (highQualityShader)
            base +=
                "lowp float  q = 1.164383561643836 * (y - 0.0625);\n"

                "lowp float  r = q                         + 1.596026785714286 * v;\n"
                "lowp float  g = q - 0.391762290094916 * u - 0.812967647237770 * v;\n"
                "lowp float  b = q + 2.017232142857143 * u;\n"

                "r=clamp(r, 0.0, 1.0);"
                "g=clamp(g, 0.0, 1.0);"
                "b=clamp(b, 0.0, 1.0);"

                "lowp vec4 color = vec4(r, g, b, 1.0);\n";
        else
            base +=
                "lowp float  r = y + 1.13983*v;\n"
                "lowp float  g = y - 0.39465*u - 0.58060*v;\n"
                "lowp float  b = y + 2.03211*u;\n"
                "lowp vec4 color = vec4(r, g, b, 1.0);\n";

        base +=
#if PREMULT_MOVIE
            "return color * ya.a;\n"
#else
            "return vec4(color.rgb, ya.a);\n"
#endif
            "}\n"
            "#endif\n";


        _shader->init(
            STDRenderer::uberShaderBody,
            "#define REPLACED_GET_BASE\n"
            "lowp vec4 replaced_get_base();",
            base.c_str());
        #endif
    }

    void MovieSprite::free()
    {
        delete _shader;
        _shader = 0;
    }


    spMovieSprite MovieSprite::create()
    {
        return new MovieSpriteWeb;
    }

    MovieSpriteWeb::MovieSpriteWeb()
    {

    }

    MovieSpriteWeb::~MovieSpriteWeb()
    {
        _clear();
    }

    void MovieSpriteWeb::_initPlayer()
    {
        // just start playing the video at '_fname' relative url
        unsigned int self = *reinterpret_cast<unsigned int*>(this);
        EM_ASM_INT({

            var self = $0|0;
            var file = Pointer_stringify($1|0);

            // DEBUG: ensure we can play ogv video
            console.log("MovieSpriteWeb playing video ", file);
            //if (x.canPlayType('video/ogg').length > 0) {
            //    console.log("This browser supports ogg video");
            //}else{
            //    console.log("This browser DOES NOT SUPPORT ogg video");
            //}
            
            var video = document.createElement("VIDEO");

            // TODO: support filetypes?
            //if (video.canPlayType("video/mp4")) {
            //    video.setAttribute("src","movie.mp4");
            //} else {

            video.setAttribute("src",file);
            video.setAttribute("width", "320");
            video.setAttribute("height", "240");
            video.setAttribute("controls", "false");

            // DEBUG:
            // Normally we just use the element to provide us a video texture
            // but if we want to see the actual element on the page, uncomment this
            document.body.appendChild(video);

            // let emscripten keep track of these video elements
            // so we can interact with them
            //if(!Module.videos) Module.videos = {};
            Module['videos'] = Module['videos'] || {};
            Module.videos[self] = video;

            return 0;
        }
        ,self
        ,_fname.c_str());

        setSize(320,240);    
    }

    void MovieSpriteWeb::_play()
    {
        unsigned int self = *reinterpret_cast<unsigned int*>(this);
        EM_ASM_INT({
            var self = $0|0;
            if(Module.videos[self])
                Module.videos[self].play();
            return 0;
        }
        ,self);
    }

    void MovieSpriteWeb::_pause()
    {
        unsigned int self = *reinterpret_cast<unsigned int*>(this);
        EM_ASM_INT({
            var self = $0|0;
            if(Module.videos[self])
                Module.videos[self].pause();
            return 0;
        }
        ,self);
        _paused = true;
    }

    void MovieSpriteWeb::_resume()
    {
        if (!_paused)
            return;

        _play();
    }

    void MovieSpriteWeb::_setVolume(float v)
    {

    }

    void MovieSpriteWeb::_stop()
    {
        _clear();
    }

    bool MovieSpriteWeb::_isPlaying() const
    {
        #if 0
        return  !pthread_equal(_threadID, pthread_self());
        #else
        return false;
        #endif
    }

    void MovieSpriteWeb::_clear()
    {

    }

    void MovieSpriteWeb::_update(const UpdateState&)
    {
        #if 0
        if (_decoder)
        {
            _decoder->_mutex.lock();
            if (_decoder->_updated)
            {
                _dirty = true;
            }
            _decoder->_updated = false;
            _decoder->_mutex.unlock();
        }
        #endif
    }
}
