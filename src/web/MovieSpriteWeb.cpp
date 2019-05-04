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
#include "oxygine/RenderState.h"
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
//#define DEBUG_MOVIESPRITEWEB

extern "C"
{

EMSCRIPTEN_KEEPALIVE
static int movieSpriteWeb_onFrameLoaded(void* self, int width, int height)
{
    oxygine::MovieSpriteWeb* pMovie = static_cast<oxygine::MovieSpriteWeb*>(self);
    
    #if defined(DEBUG_MOVIESPRITEWEB)
    printf("%s:%d:%s onFrameLoaded invoked with width: %d height: %d\n", __FILE__, __LINE__, __func__, width, height);
    #endif
    
    pMovie->setBufferSize(width, height);
    pMovie->setSize(width, height);
     pMovie->setFrameLoaded(true);
    // TODO: correct texture format
    pMovie->createVideoTexture(width, height);

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

                "lowp vec4 color = vec4(1.0, 1.0, 0.0, 1.0);\n";
        else
            base +=
                "lowp float  r = y + 1.13983*v;\n"
                "lowp float  g = y - 0.39465*u - 0.58060*v;\n"
                "lowp float  b = y + 2.03211*u;\n"
                "lowp vec4 color = vec4(1.0, 1.0, 0.0, 1.0);\n";

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

    void MovieSpriteWeb::createVideoTexture(const int width, const int height)
    {
        if(_videoTexture)
        {
            #if defined(DEBUG_MOVIESPRITEWEB)
            printf("Not creating video texture because on already exists.\n");
            #endif
            return;
        }

        if(_videoTextureID > 0)
        {
            #if defined(DEBUG_MOVIESPRITEWEB)
            printf("Not creating video texture because _videoTextureID is nonzero.\n");
            #endif
            return;
        }

        // create an opengl texture we can upload video to
        glGenTextures(1, &_videoTextureID);
        glBindTexture(GL_TEXTURE_2D, _videoTextureID);

        #if defined(DEBUG_MOVIESPRITEWEB)
        // DEBUG:
        // Manually fill new video texture with some known color
        unsigned int* data = new unsigned int[720*400];
        for(int row = 0; row < 400; ++row)
            for(int col = 0; col < 720; ++col)
            {
                data[row * 720 + col] = 0xff00ffff;
            }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 720, 400, 0, GL_RGBA, GL_UNSIGNED_BYTE, (void*)data);
        //glGenerateMipmap(GL_TEXTURE_2D);
        delete [] data;
        #endif

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        _videoTexture = IVideoDriver::instance->createTexture();
        _videoTexture->init((void*)_videoTextureID, 720, 400, TF_R8G8B8A8);

        #if defined(DEBUG_MOVIESPRITEWEB)
        printf("%s:%d:%s created video texture with id: %d\n", __FILE__, __LINE__, __func__, _videoTextureID);
        #endif
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

            //console.log("MovieSpriteWeb playing video ", file);
            
            var video = document.createElement("VIDEO");

            video.setAttribute("src",file);
            video.setAttribute("width", "320");
            video.setAttribute("height", "240");
            video.setAttribute("controls", "false");

            // when the video loads, pull out the dimensions so we can create texturs
            video.addEventListener('loadeddata', function(event) {
                console.log('Yay! The readyState just increased to  ' + 
                    'HAVE_CURRENT_DATA or greater for the first time.');

                // pass video size back to C++
                var height = video.videoHeight; // returns the intrinsic height of the video
                var width = video.videoWidth; // returns the intrinsic width of the video
                console.log("Size of video is ", width, "x", height);

                var movieSpriteWeb_onFrameLoaded = Module.cwrap('movieSpriteWeb_onFrameLoaded',
                    'number', // return type
                    ['number', 'number', 'number']); // argument types
                movieSpriteWeb_onFrameLoaded(self, width, height);

                return 0;

            });

            // DEBUG:
            // Normally we just use the element to provide us a video texture
            // but if we want to see the actual element on the page, uncomment this
            //document.body.appendChild(video);

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
            if(Module['videos'] && Module.videos[self])
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
            if(Module['videos'] && Module.videos[self])
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

    float MovieSpriteWeb::_getVolume()const
    {
        return 1.0f;
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
        _pause();

        if(_videoTexture)
            _videoTexture->release();
        _videoTexture = 0;
        _videoTextureID = 0;
        _frameLoaded = false;
    }

    void MovieSpriteWeb::_update(const UpdateState&)
    {
        // we only have textures and image dimensions after the first frame has loaded
        if(!_frameLoaded || _videoTextureID < 1)
            return;

        int failure = EM_ASM_INT({

            var self = ($0|0);
            var id = ($1|0);
            var texture =  GL.textures[id];

            if(!Module['videos'] && !Module.videos[self])
            {
                console.log("Bailing on _update js because no Module['videos'] yet...");
                return 1;
            }
            var video = Module.videos[self];

            const level = 0;
            const internalFormat = GLctx.RGBA;
            const srcFormat = GLctx.RGBA;
            const srcType = GLctx.UNSIGNED_BYTE;
            GLctx.bindTexture(GLctx.TEXTURE_2D, texture);
            GLctx.texImage2D(GLctx.TEXTURE_2D, level, internalFormat, srcFormat, srcType, video);
        }
        ,this
        ,_videoTextureID);

        _dirty = failure == 0;
    }

    void MovieSpriteWeb::doRender(const RenderState& rs)
    {
        // This call updates _textureUV and _textureYA from
        // the current movie sprite.
        // Probably not necesary as we maintain a separate video texture.
        #if 0
        convert();
        #else
        _dirty = false;
        _ready = true;
        #endif

        if (!_ready)
        {
            #if defined(DEBUG_MOVIESPRITEWEB)
            printf("%s:%d:%s BAILING ON RENDER BECAUSE NOT READY\n", __FILE__, __LINE__, __func__);
            #endif
            return;
        }

        if (!_videoTexture)
        {
            #if defined(DEBUG_MOVIESPRITEWEB)
            printf("%s:%d:%s BAILING ON RENDER BECAUSE no video texture\n", __FILE__, __LINE__, __func__);
            #endif
            return;
        }

        Material::null->apply();

        STDRenderer* renderer = STDRenderer::getCurrent();

        renderer->setUberShaderProgram(_shader);
        //renderer->setShaderFlags(UberShaderProgram::ALPHA_PREMULTIPLY);
        renderer->setShaderFlags(0);

        renderer->setTransform(rs.transform);
        IVideoDriver::instance->setUniform("yaScale", &_yaScale, 1);

        Color color = rs.getFinalColor(getColor());


        if(_videoTexture)
        {
            rsCache().setTexture(0, _videoTexture);
            rsCache().setTexture(1, _textureUV);
        }
        #if 0
        else{
            rsCache().setTexture(0, _textureYA);
            rsCache().setTexture(1, _textureUV);
        }
        #endif
        rsCache().setBlendMode(blend_alpha);
        renderer->addQuad(color, getAnimFrame().getSrcRect(), getDestRect());

        renderer->flush();
    }
}


