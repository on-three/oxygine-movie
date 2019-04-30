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

extern "C"
{
EMSCRIPTEN_KEEPALIVE
static int movieSpriteWeb_SetBufferSize(void* self, int width, int height)
{
    oxygine::MovieSpriteWeb* pMovie = static_cast<oxygine::MovieSpriteWeb*>(self);
    #if 1
    printf("%s:%d:%s setting buffer size to %d,%d\n", __FILE__, __LINE__, __func__, width, height);
    #endif
    pMovie->setBufferSize(width, height);
    return 0;
}
EMSCRIPTEN_KEEPALIVE

static int movieSpriteWeb_SetSize(void* self, int width, int height)
{
    oxygine::MovieSpriteWeb* pMovie = static_cast<oxygine::MovieSpriteWeb*>(self);
    #if 1
    printf("%s:%d:%s setting sprite size to %d,%d\n", __FILE__, __LINE__, __func__, width, height);
    #endif
    pMovie->setSize(width, height);
    return 0;
}

EMSCRIPTEN_KEEPALIVE
static int movieSpriteWeb_SetFrameLoaded(void* self, bool ready)
{
    oxygine::MovieSpriteWeb* pMovie = static_cast<oxygine::MovieSpriteWeb*>(self);
    #if 1
    printf("%s:%d:%s setting movie frameloaded state to %s\n", __FILE__, __LINE__, __func__, ready? "TRUE":"FALSE");
    #endif
    pMovie->setFrameLoaded(ready);
    return 0;
}
}
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
        #if 1
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
            #if 0
            "#define REPLACED_GET_BASE\n"
            "lowp vec4 replaced_get_base();",
            #else
            "",
            #endif
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
        // To satisfy the MovieSprite base class
        // we MUST define _bufferSize in this method, which can be pretty much
        // the movie size (width x height) as an oxygine::Point

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

            // when the video loads, pull out the dimensions so we can create texturs
            video.addEventListener('loadeddata', (event) => {
                console.log('Yay! The readyState just increased to  ' + 
                    'HAVE_CURRENT_DATA or greater for the first time.');

                // pass video size back to C++
                var height = video.videoHeight; // returns the intrinsic height of the video
                var width = video.videoWidth; // returns the intrinsic width of the video
                console.log("Size of video is ", width, "x", height);

                var movieSpriteWeb_SetBufferSize = Module.cwrap('movieSpriteWeb_SetBufferSize',
                    'number', // return type
                    ['number', 'number', 'number']); // argument types

                var movieSpriteWeb_SetSize = Module.cwrap('movieSpriteWeb_SetSize',
                    'number', // return type
                    ['number', 'number', 'number']); // argument types

                var movieSpriteWeb_SetFrameLoaded = Module.cwrap('movieSpriteWeb_SetFrameLoaded',
                    'number', // return type
                    ['number', 'boolean']); // argument types

                movieSpriteWeb_SetBufferSize(self, width, height);
                movieSpriteWeb_SetSize(self, width, height);
                movieSpriteWeb_SetFrameLoaded(self, true);

                return 0;

            });

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
        ,this
        ,_fname.c_str());

        // HACK: force a size to debug basic texture handling
        setBufferSize(720, 400);
        setSize(720, 400);
        _movieRect = Rect(0, 0, 720, 400);
    
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
        ,this);
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
        ,this);
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
        // we only have textures and image dimensions after the first frame has loaded
        #if 0
        if(!_frameLoaded)
            return;
        #endif

        #if 1
        Image& _surfaceUV = _mtUV;
        Image& _surfaceYA = _mtYA;
        int w = _movieRect.getWidth();
        int h = _movieRect.getHeight();
        int stride = w; // TODO: correct?
        
        Image memUV;
        #if 0
        memUV.init(ti.frame_width / 2, ti.frame_height / 4, TF_A8L8);
        #else
        memUV.init(w / 2, h / 4, TF_A8L8);
        #endif
        memUV.fillZero();

        ImageData dstUV = memUV.lock();
        unsigned char* destUV = dstUV.data;

        Image memYA;
        #if 0
        memYA.init(ti.frame_width, ti.frame_height / 2, TF_A8L8);
        #else
        memYA.init(w, h / 2, TF_A8L8);
        #endif

        ImageData dstYA = memYA.lock();
        unsigned char* destYA = dstYA.data;

        #if 0
        if (_hasAlpha)
        {
            h /= 2;
            const unsigned char* srcA = srcY + ti.pic_height / 2 * buffer[0].stride;

            for (int y = 0; y != h; y++)
            {
                const unsigned char* srcLineA = srcA;
                const unsigned char* srcLineY = srcY;
                unsigned char* destLineYA = destYA;

                for (int x = 0; x != w; x++)
                {
                    *destLineYA++ = *srcLineY++;
                    unsigned char a = *srcLineA++;
                    int v = (a - 16) * 255 / 219;
                    *destLineYA++ = Clamp2Byte(v);
                }
                srcY += stride;
                srcA += stride;

                destYA += dstYA.pitch;
            }
        }
        else
        #endif
        {
            for (int y = 0; y != h; y++)
            {
                //const unsigned char* srcLineY = srcY;
                unsigned char* destLineYA = destYA;

                for (int x = 0; x != w; x++)
                {
                    #if 0
                    *destLineYA++ = *srcLineY++;
                    #else
                    *destLineYA++ = 255;
                    #endif
                }
                //srcY += stride;
                destYA += dstYA.pitch;
            }
        }

        #if 0
        const unsigned char* srcU = buffer[2].data + ti.pic_y / 2 * buffer[2].stride;
        const unsigned char* srcV = buffer[1].data + ti.pic_y / 2 * buffer[1].stride;
        stride = buffer[1].stride;
        w = buffer[1].width;
        h = buffer[1].height;
        #endif

        #if 0
        if (_hasAlpha)
            h /= 2;
        #endif

        for (int y = 0; y != h; y++)
        {
            //const unsigned char* srcLineU = srcU;
            //const unsigned char* srcLineV = srcV;
            unsigned char* destLineUV = destUV;

            for (int x = 0; x != w; x++)
            {
                #if 0
                *destLineUV++ = *srcLineU++;
                *destLineUV++ = *srcLineV++;
                #else
                *destLineUV++ = 255;
                *destLineUV++ = 255;
                #endif
            }
            //srcU += stride;
            //srcV += stride;
            destUV += dstUV.pitch;
        }
        #endif
        _dirty = true;
    }
}
