#include "MovieSpriteTheora.h"
#include "ogg/ogg.h"
#include "theora/theora.h"
#include "theora/theoradec.h"
#include <map>
#include "oxygine/core/Mutex.h"
#include "oxygine/core/oxygine.h"
#include <fstream>
#include "oxygine/core/ThreadMessages.h"
#include "pthread.h"
#include "oxygine/core/ImageDataOperations.h"
#include "oxygine/core/UberShaderProgram.h"
#include "oxygine/STDRenderer.h"
#include "ivorbiscodec.h"
#include <time.h>
#include "oxygine/utils/AtlasBuilder.h"
#include "oxygine/utils/ImageUtils.h"
#include "oxygine/utils/stringUtils.h"
#include "oxygine/res/CreateResourceContext.h"
#include "oxygine/json/json.h"
#include "oxygine/utils/cdecode.h"
//#include "core/gl/oxgl.h"

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

    enum StreamType
    {
        TYPE_VORBIS,
        TYPE_THEORA,
        TYPE_UNKNOWN
    };

    class TheoraOggStream;

    class TheoraDecode
    {
    public:
        th_info mInfo;
        th_comment mComment;
        th_setup_info* mSetup;
        th_dec_ctx* mCtx;

    public:
        TheoraDecode() :
            mSetup(0),
            mCtx(0)
        {
            th_info_init(&mInfo);
            th_comment_init(&mComment);
        }

        void initForData(TheoraOggStream* stream);

        ~TheoraDecode()
        {
            th_setup_free(mSetup);
            th_decode_free(mCtx);
        }
    };


    class VorbisDecode
    {
    public:
        vorbis_info mInfo;
        vorbis_comment mComment;
        vorbis_dsp_state mDsp;
        vorbis_block mBlock;

    public:
        VorbisDecode()
        {
            vorbis_info_init(&mInfo);
            vorbis_comment_init(&mComment);
        }

        void initForData(TheoraOggStream* stream);
    };


    class TheoraOggStream
    {
    public:
        int mSerial;
        ogg_stream_state mState;
        StreamType mType;
        bool mActive;
        TheoraDecode mTheora;
        VorbisDecode mVorbis;
    public:
        TheoraOggStream(int serial = -1) :
            mSerial(serial),
            mType(TYPE_UNKNOWN),
            mActive(true)
        {
        }

        ~TheoraOggStream()
        {
            int ret = ogg_stream_clear(&mState);
            OX_ASSERT(ret == 0);
        }
    };

    void TheoraDecode::initForData(TheoraOggStream* stream)
    {
        stream->mTheora.mCtx =
            th_decode_alloc(&stream->mTheora.mInfo,
                            stream->mTheora.mSetup);
        OX_ASSERT(stream->mTheora.mCtx != NULL);
        int ppmax = 0;
        int ret = th_decode_ctl(stream->mTheora.mCtx,
                                TH_DECCTL_GET_PPLEVEL_MAX,
                                &ppmax,
                                sizeof(ppmax));
        OX_ASSERT(ret == 0);

        // Set to a value between 0 and ppmax inclusive to experiment with
        // this parameter.
        ppmax = 0;
        ret = th_decode_ctl(stream->mTheora.mCtx,
                            TH_DECCTL_SET_PPLEVEL,
                            &ppmax,
                            sizeof(ppmax));
        OX_ASSERT(ret == 0);
    }


    void VorbisDecode::initForData(TheoraOggStream* stream)
    {
        int ret = vorbis_synthesis_init(&stream->mVorbis.mDsp, &stream->mVorbis.mInfo);
        OX_ASSERT(ret == 0);
        ret = vorbis_block_init(&stream->mVorbis.mDsp, &stream->mVorbis.mBlock);
        OX_ASSERT(ret == 0);
    }


    typedef map<int, TheoraOggStream*> StreamMap;

    class OggDecoderBase
    {
    public:
        OggDecoderBase(): mGranulepos(0),
            _videoStream(0),
            _audioStream(0) {}

        StreamMap mStreams;
        TheoraOggStream* _audioStream;
        TheoraOggStream* _videoStream;

        ogg_sync_state _syncState;

        ogg_int64_t  mGranulepos;


        void initStreams(file::handle h);

        bool handle_theora_header(TheoraOggStream* stream, ogg_packet* packet);
        bool handle_vorbis_header(TheoraOggStream* stream, ogg_packet* packet);
        void read_headers(file::handle, ogg_sync_state* state);

        bool read_page(file::handle, ogg_sync_state* state, ogg_page* page);
        bool read_packet(file::handle, ogg_sync_state* state, TheoraOggStream* stream, ogg_packet* packet);
    };



    bool OggDecoderBase::read_page(file::handle stream, ogg_sync_state* state, ogg_page* page)
    {
        int ret = 0;

        // If we've hit end of file we still need to continue processing
        // any remaining pages that we've got buffered.
        //if (!stream.good())
        //  return ogg_sync_pageout(state, page) == 1;

        while ((ret = ogg_sync_pageout(state, page)) != 1)
        {
            // Returns a buffer that can be written too
            // with the given size. This buffer is stored
            // in the ogg synchronisation structure.
            char* buffer = ogg_sync_buffer(state, 4096);
            OX_ASSERT(buffer);

            // Read from the file into the buffer
            int bytes = file::read(stream, buffer, 4096);
            //stream.read(buffer, 4096);
            //int bytes = stream.gcount();
            if (bytes == 0)
            {
                // End of file.
                return false;
            }

            // Update the synchronisation layer with the number
            // of bytes written to the buffer
            ret = ogg_sync_wrote(state, bytes);
            OX_ASSERT(ret == 0);
        }
        return true;
    }

    bool OggDecoderBase::read_packet(file::handle is, ogg_sync_state* state, TheoraOggStream* stream, ogg_packet* packet)
    {
        if (!is)
            return false;

        int ret = 0;

        while ((ret = ogg_stream_packetout(&stream->mState, packet)) != 1)
        {
            ogg_page page;
            if (!read_page(is, state, &page))
                return false;

            int serial = ogg_page_serialno(&page);
            OX_ASSERT(mStreams.find(serial) != mStreams.end());
            TheoraOggStream* pageStream = mStreams[serial];

            // Drop data for streams we're not interested in.
            if (stream->mActive)
            {
                ret = ogg_stream_pagein(&pageStream->mState, &page);
                OX_ASSERT(ret == 0);
            }
        }
        return true;
    }

    void OggDecoderBase::read_headers(file::handle stream, ogg_sync_state* state)
    {
        ogg_page page;

        bool headersDone = false;
        while (!headersDone && read_page(stream, state, &page))
        {
            int ret = 0;
            int serial = ogg_page_serialno(&page);
            TheoraOggStream* stream = 0;

            if (ogg_page_bos(&page))
            {
                // At the beginning of the stream, read headers
                // Initialize the stream, giving it the serial
                // number of the stream for this page.
                stream = new TheoraOggStream(serial);
                ret = ogg_stream_init(&stream->mState, serial);
                OX_ASSERT(ret == 0);
                mStreams[serial] = stream;
            }

            OX_ASSERT(mStreams.find(serial) != mStreams.end());
            stream = mStreams[serial];

            // Add a complete page to the bitstream
            ret = ogg_stream_pagein(&stream->mState, &page);
            OX_ASSERT(ret == 0);

            // Process all available header packets in the stream. When we hit
            // the first data stream we don't decode it, instead we
            // return. The caller can then choose to process whatever data
            // streams it wants to deal with.
            ogg_packet packet;
            while (!headersDone &&
                    (ret = ogg_stream_packetpeek(&stream->mState, &packet)) != 0)
            {
                OX_ASSERT(ret == 1);

                // A packet is available. If it is not a header packet we exit.
                // If it is a header packet, process it as normal.
                headersDone = headersDone || handle_theora_header(stream, &packet);
                headersDone = headersDone || handle_vorbis_header(stream, &packet);
                if (!headersDone)
                {
                    // Consume the packet
                    ret = ogg_stream_packetout(&stream->mState, &packet);
                    OX_ASSERT(ret == 1);
                }
            }
        }
    }


    void OggDecoderBase::initStreams(file::handle h)
    {
        int ret = ogg_sync_init(&_syncState);
        OX_ASSERT(ret == 0);

        // Read headers for all streams
        read_headers(h, &_syncState);



        // Find and initialize the first theora and vorbis
        // streams. According to the Theora spec these can be considered the
        // 'primary' streams for playback.
        TheoraOggStream* video = 0;
        TheoraOggStream* audio = 0;
        for (StreamMap::iterator it = mStreams.begin(); it != mStreams.end(); ++it)
        {
            TheoraOggStream* stream = (*it).second;
            if (!video && stream->mType == TYPE_THEORA)
            {
                video = stream;
                video->mTheora.initForData(video);
            }
            else if (!audio && stream->mType == TYPE_VORBIS)
            {
                audio = stream;
                audio->mVorbis.initForData(audio);
            }
            else
                stream->mActive = false;
        }

        if (video)
            _videoStream = video;
        if (audio)
            _audioStream = audio;
    }

    bool OggDecoderBase::handle_theora_header(TheoraOggStream* stream, ogg_packet* packet)
    {
        int ret = th_decode_headerin(&stream->mTheora.mInfo,
                                     &stream->mTheora.mComment,
                                     &stream->mTheora.mSetup,
                                     packet);
        if (ret == TH_ENOTFORMAT)
            return false; // Not a theora header

        if (ret > 0)
        {
            // This is a theora header packet
            stream->mType = TYPE_THEORA;

            return false;
        }

        // Any other return value is treated as a fatal error
        OX_ASSERT(ret == 0);

        // This is not a header packet. It is the first
        // video data packet.
        return true;
    }



    const int A_OFFSET = 1;



    inline void makePixel(Pixel& px, const unsigned char* ya, const unsigned char* srcLineUV)
    {
        float y = (*ya) / 255.0f;
        float v = (*srcLineUV) / 255.0f - 0.5f;
        float u = (*(srcLineUV + 1)) / 255.0f - 0.5f;

        float q = 1.164383561643836f * (y - 0.0625f);

        float  r = q + 1.596026785714286f * v;
        float  g = q - 0.391762290094916f * u - 0.812967647237770f * v;
        float  b = q + 2.017232142857143f * u;


        px.r = Clamp2Byte(int(r * 255));
        px.g = Clamp2Byte(int(g * 255));
        px.b = Clamp2Byte(int(b * 255));

        px.a = *(ya + A_OFFSET);
    }


    ResAnimTheoraPacker::ResAnimTheoraPacker(const Point& atlasSize, TextureFormat tf, bool optBounds): _atlasSize(atlasSize), _tf(tf), _optBounds(optBounds), _dec(0), _h(0)
    {
    }

    ResAnimTheoraPacker::~ResAnimTheoraPacker()
    {
        sync();
    }

    void ResAnimTheoraPacker::sync()
    {
        if (_mt)
        {
            CreateTextureTask t;
            t.src = _mt;
            t.dest = _native;
            LoadResourcesContext::get()->createTexture(t);

#if 0
            char nme[255];
            static int i = 0;
            safe_sprintf(nme, "im%05d.png", i);
            ++i;
            saveImage(_mt->lock(), nme);
            int q = 0;
#endif
        }
    }



    void ResAnimTheoraPacker::next_atlas()
    {
        sync();

        _mt = new Image;


        OggDecoderBase& dec = *_dec;
        TheoraOggStream* video = dec._videoStream;

        int picW = video->mTheora.mInfo.pic_width + 4;
        int picH = video->mTheora.mInfo.pic_height / 2 + 4;

        int cols = 2048 / picW;
        _atlasSize.x = cols * picW;
        int left = _framesLeft;
        int rows = left / cols;
        if (_frames > cols && (left % cols))
            rows += 1;
        if (rows == 0)
            rows = 1;

        _atlasSize.y = rows * picH;
        while (_atlasSize.y > 1024)
        {
            rows /= 2;
            _atlasSize.y = rows * picH;
        }


        _mt->init(_atlasSize.x, _atlasSize.y, _tf);
        _atlas.init(_mt->getWidth(), _mt->getHeight());

        _native = IVideoDriver::instance->createTexture();
    }

    void ResAnimTheoraPacker::prepare(const std::string& name, details* det)
    {
        _h = file::open(name.c_str(), "srb");

        _dec = new OggDecoderBase;
        _dec->initStreams(_h);

        Json::Value js(Json::objectValue);
        {
            char ORG[] = "ORGANIZATION";
            const char* userData = th_comment_query(&_dec->_videoStream->mTheora.mComment, ORG, 0);
            Json::Reader reader;
            int ln = strlen(userData);

            base64_decodestate state;
            base64_init_decodestate(&state);
            string str;
            str.resize(ln * 3 / 4);
            if (ln)
                base64_decode_block(userData, ln, (char*)&str.front(), &state);

            bool s = reader.parse(str, js);
        }
        _scale = 1.0f;

        if (!js.isNull())
        {
            _scale = js["scale"].asFloat();
            _frames = js["frames"].asInt();
        }

        TheoraOggStream* video = _dec->_videoStream;

        const th_info& ti = video->mTheora.mInfo;
        if (det)
        {

            det->size = Vector2(ti.pic_width / _scale, ti.pic_height / 2 / _scale).cast<Point>();
            det->frames = _frames;
            det->framerate = int(float(video->mTheora.mInfo.fps_numerator) / float(video->mTheora.mInfo.fps_denominator));
        }
    }

    void ResAnimTheoraPacker::decode(ResAnim* rs)
    {
        OggDecoderBase& dec = *_dec;

        animationFrames frames;




        TheoraOggStream* video = dec._videoStream;

        TheoraOggStream* baseStream = video;

        th_decode_ctl(video->mTheora.mCtx, TH_DECCTL_SET_GRANPOS, &dec.mGranulepos, sizeof(dec.mGranulepos));
        int ret = 0;

        unsigned int time = getTimeMS();
        unsigned int timeOffset = 0;



        //unsigned int ps = file::tell(_h);

        // Read audio packets, sending audio data to the sound hardware.
        // When it's time to display a frame, decode the frame and display it.
        ogg_packet packet;

        float framerate = float(video->mTheora.mInfo.fps_numerator) / float(video->mTheora.mInfo.fps_denominator);
        rs->setFrameRate((int)framerate);

        _framesLeft = _frames;



        const th_info& ti = video->mTheora.mInfo;

        bool end = false;
        while (!end)
        {
            bool ok = dec.read_packet(_h, &dec._syncState, baseStream, &packet);
            if (!ok)
                break;

            if (video)
            {
                timeMS video_time = timeMS(th_granule_time(video->mTheora.mCtx, dec.mGranulepos) * 1000);

                // The granulepos for a packet gives the time of the end of the
                // display interval of the frame in the packet.  We keep the
                // granulepos of the frame we've decoded and use this to know the
                // time when to display the next frame.
                int ret = th_decode_packetin(baseStream->mTheora.mCtx,
                                             &packet,
                                             &dec.mGranulepos);
                if (ret >= 0)
                {
                    if (ret == TH_DUPFRAME)
                    {
                        AnimationFrame prev = frames.back();
                        frames.push_back(prev);
                        continue;
                    }



                    th_ycbcr_buffer buffer;
                    ret = th_decode_ycbcr_out(baseStream->mTheora.mCtx, buffer);
                    OX_ASSERT(ret == 0);


                    Image memYA;
                    memYA.init(ti.frame_width, ti.frame_height / 2, TF_A8L8);

                    ImageData dstYA = memYA.lock();
                    unsigned char* destYA = dstYA.data;

                    const unsigned char* srcY = buffer[0].data + ti.pic_y * buffer[0].stride;

                    int stride = buffer[0].stride;
                    int w = ti.pic_width;
                    int h = ti.pic_height;

                    int fw = ti.frame_width;


                    Rect bounds = Rect::invalidated();

                    if (true)
                    {
                        h /= 2;
                        const unsigned char* srcA = srcY + ti.pic_height / 2 * buffer[0].stride;

                        for (int y = 0; y != h; y++)
                        {
                            const unsigned char* srcLineA = srcA;
                            const unsigned char* srcLineY = srcY;
                            unsigned char* destLineYA = destYA;

                            for (int x = 0; x !=  fw; x++)
                            {
                                *destLineYA++ = *srcLineY++;
                                unsigned char a = *srcLineA++;
                                int v = (a - 16) * 255 / 219;
                                *destLineYA++ = Clamp2Byte(v);

                                if (v)
                                {
                                    bounds.unite(Rect(x, y, 1, 1));
                                }
                            }
                            srcY += stride;
                            srcA += stride;

                            destYA += dstYA.pitch;
                        }
                    }


                    Image memUV;
                    memUV.init(ti.frame_width / 2, ti.frame_height / 4, TF_A8L8);
                    memUV.fillZero();

                    ImageData dstUV = memUV.lock();
                    unsigned char* destUV = dstUV.data;

                    const unsigned char* srcU = buffer[2].data + ti.pic_y / 2 * buffer[2].stride;
                    const unsigned char* srcV = buffer[1].data + ti.pic_y / 2 * buffer[1].stride;

                    stride = buffer[1].stride;
                    w = buffer[1].width;
                    h = buffer[1].height;

                    if (true)
                        h /= 2;

                    for (int y = 0; y != h; y++)
                    {
                        const unsigned char* srcLineU = srcU;
                        const unsigned char* srcLineV = srcV;
                        unsigned char* destLineUV = destUV;

                        for (int x = 0; x != w; x++)
                        {
                            *destLineUV++ = *srcLineU++;
                            *destLineUV++ = *srcLineV++;
                        }
                        srcU += stride;
                        srcV += stride;
                        destUV += dstUV.pitch;
                    }




                    Image res;
                    res.init(ti.frame_width, ti.frame_height / 2, TF_R8G8B8A8);

                    ImageData dest = res.lock();

                    unsigned char* destRGBA = dest.data;

                    const unsigned char* srcYA = dstYA.data;
                    const unsigned char* srcUV = dstUV.data;



                    PixelR8G8B8A8 p;
                    for (int Y = 0; Y != h; Y++)
                    {
                        const unsigned char* srcLineYA = srcYA;
                        const unsigned char* srcLineUV = srcUV;
                        unsigned char*       destLineRGBA = destRGBA;

                        for (int x = 0; x != w; x++)
                        {
                            Pixel px;

                            const unsigned char* ya = srcLineYA;
                            unsigned char* rgba = destLineRGBA;


                            makePixel(px, ya, srcLineUV);
                            p.setPixel(rgba, px);

                            ya += 2;
                            rgba += 4;


                            makePixel(px, ya, srcLineUV);
                            p.setPixel(rgba, px);

                            ya += dstYA.pitch - 2;
                            rgba += dest.pitch - 4;


                            makePixel(px, ya, srcLineUV);
                            p.setPixel(rgba, px);

                            ya += 2;
                            rgba += 4;

                            makePixel(px, ya, srcLineUV);
                            p.setPixel(rgba, px);

                            destLineRGBA += 4 * 2;
                            srcLineUV += dstUV.bytespp;
                            srcLineYA += dstYA.bytespp * 2;
                        }


                        destRGBA += dest.pitch;
                        destRGBA += dest.pitch;

                        srcYA += dstYA.pitch;
                        srcYA += dstYA.pitch;

                        srcUV += dstUV.pitch;
                    }



                    Rect rc;
                    //dest = dest.getRect(Rect(ti.pic_x, 0, ti.pic_width, ti.pic_height / 2));



                    /*
                    {
                        char nme[255];
                        static int i = 0;
                        safe_sprintf(nme, "im/%s%05d.png", path::extractFileName(name).c_str(), i);
                        ++i;
                        saveImage(dest, nme);
                        int q = 0;
                    }

                    {
                        char nme[255];
                        static int i = 0;
                        safe_sprintf(nme, "im/im%05d.png", i);
                        ++i;
                        saveImage(dest, nme);
                        int q = 0;
                    }

                    {
                        char nme[255];
                        static int i = 0;
                        safe_sprintf(nme, "im/ya%05d.png", i);
                        ++i;
                        saveImage(memYA.lock(), nme);
                        int q = 0;
                    }


                    {
                        char nme[255];
                        static int i = 0;
                        safe_sprintf(nme, "im/uv%05d.png", i);
                        ++i;
                        saveImage(memUV.lock(), nme);
                        int q = 0;
                    }
                    */


                    Point offset(2, 2);
                    if (bounds == Rect::invalidated())
                    {
                        bounds = Rect(0, 0, 0, 0);
                    }

                    Rect clip(ti.pic_x, 0, ti.pic_width, ti.pic_height / 2);
                    if (_optBounds)
                        bounds.clip(clip);
                    else
                        bounds = clip;
                    dest = dest.getRect(bounds);
                    if (!_atlas.add(_mt.get(), dest, rc, offset))
                    {
                        next_atlas();
                        _atlas.add(_mt.get(), dest, rc, offset);
                    }


                    AnimationFrame frame;
                    Diffuse df;
                    df.base = _native;
                    OX_ASSERT(0);
                    //df.premultiplied = true;

                    RectF srcRectF = rc.cast<RectF>() / _mt->getSize().cast<Vector2>();
                    RectF destRectF = RectF(0, 0, (float)ti.pic_width, ti.pic_height / 2.0f);

                    bounds.pos.x -= ti.pic_x;
                    destRectF = bounds.cast<RectF>() / _scale;

                    frame.init(0, df, srcRectF, destRectF, Vector2((float)ti.pic_width, ti.pic_height / 2.0f) / _scale);

                    frames.push_back(frame);
                    _framesLeft--;
                }
            }
        }

        //



        rs->init(frames, frames.size());

        ret = ogg_sync_clear(&dec._syncState);

        file::close(_h);
        delete _dec;
        _dec = 0;
    }


    ResAnim* createResAnimFromMovie(const string& name, const Point& atlasSize, TextureFormat tf)
    {
        ResAnim* rs = new ResAnim;
        ResAnimTheoraPacker p(atlasSize, tf);
        p.prepare(name, 0);
        p.decode(rs);
        return rs;
    }

    class OggDecoder: public OggDecoderBase
    {
    public:


        Mutex& _mutex;
        Image& _surfaceUV;
        Image& _surfaceYA;

        ThreadMessages& _msg;
        bool _updated;


        Point _frameSize;
        //Point _picSize;
        Rect _pictureRect;
        timeMS _video_time = 0; // current video time

        bool& _looped;
        bool _hasAlpha;
        bool _skipNextFrame;
        bool _allowSkipFrames;


    public:
        OggDecoder(Image& uv, Image& ya, Mutex& mt, ThreadMessages& msg, bool& looped, bool hasAlpha, bool skipFrames) :
            _surfaceUV(uv), _surfaceYA(ya), _mutex(mt), _looped(looped),
            _pictureRect(0, 0, 0, 0),
            _frameSize(0, 0),
            _msg(msg),
            _updated(false),
            _allowSkipFrames(skipFrames),
            _hasAlpha(hasAlpha),
            _skipNextFrame(false)
        {
        }

        ~OggDecoder()
        {
        }

        void init(file::handle);
        bool play(file::handle);

        float getVideoTime()const{return _video_time/1000.0f;};

    protected:
        void frameUpdated();
        virtual void _frameUpdated() {}

    private:
        bool handle_theora_data(TheoraOggStream* stream, ogg_packet* packet);
    };


    void OggDecoder::init(file::handle h)
    {
        initStreams(h);


        if (_videoStream)
        {
            const th_info& ti = _videoStream->mTheora.mInfo;

            _frameSize.x = ti.frame_width;
            _frameSize.y = ti.frame_height;

            #if 1
            printf("%s:%d:%s video framesize: %d x %d\n", __FILE__, __LINE__, __func__,  _frameSize.x, _frameSize.y);
            #endif

            _pictureRect = Rect(0, 0, ti.pic_width, ti.pic_height);

            if (_hasAlpha)
            {
                _pictureRect.size.y /= 2;
                _frameSize.y /= 2;
            }
        }
    }

    void OggDecoder::frameUpdated()
    {
        _updated = true;
    }

    bool OggDecoder::play(file::handle is)
    {

        TheoraOggStream* audio = _audioStream;
        TheoraOggStream* video = _videoStream;

        TheoraOggStream* baseStream = video;

        _skipNextFrame = false;
        mGranulepos = 0;
        _updated = false;
        //th_granule_time(video->mTheora.mCtx, mGranulepos);
        th_decode_ctl(video->mTheora.mCtx, TH_DECCTL_SET_GRANPOS, &mGranulepos, sizeof(mGranulepos));
        int ret = 0;

        unsigned int time = getTimeMS();
        unsigned int timeOffset = 0;



        unsigned int ps = file::tell(is);

        // Read audio packets, sending audio data to the sound hardware.
        // When it's time to display a frame, decode the frame and display it.
        ogg_packet packet;

        float framerate = float(video->mTheora.mInfo.fps_numerator) / float(video->mTheora.mInfo.fps_denominator);
        int slpTime = static_cast<int>((1.0f / framerate) * 1000) / 2;


        bool end = false;
        while (true)
        {
            ThreadMessages::peekMessage m;

            bool first_frame_reply = false;

            while (_msg.peek(m, true))
            {
                switch (m.msgid)
                {
                    case MSG_STOP:
                    {
                        end = true;
                    } break;
                    case MSG_PAUSE:
                    {
                        timeMS tm = getTimeMS();
                        ThreadMessages::peekMessage m2;
                        _msg.get(m2);
                        OX_ASSERT(m2.msgid == MSG_RESUME);
                        timeOffset += tm - getTimeMS();

                    } break;
                    case MSG_WAIT_FIRST_FRAME:
                        first_frame_reply = true;
                        break;
                    default:
                        break;
                }
            }




            if (end)
                break;


            bool ok = read_packet(is, &_syncState, baseStream, &packet);
            if (!ok)
            {
                if (_looped)
                {
                    file::seek(is, 0, SEEK_SET);
                    ok = read_packet(is, &_syncState, baseStream, &packet);
                }
                else
                    break;
            }

            if (video)
            {
                ogg_int64_t position = 0;
                timeMS video_time = timeMS(th_granule_time(video->mTheora.mCtx, mGranulepos) * 1000);
                _video_time = video_time;
                //logs::messageln("%d - %d", video_time, mGranulepos);
                int tm = 0;

                while (true)
                {
                    tm = getTimeMS() + timeOffset - time;
                    if (tm >= video_time)
                        break;

                    sleep(slpTime);
                }


                if (handle_theora_data(video, &packet))
                {
                    //logs::messageln("frame");
                    if (first_frame_reply)
                    {
                        _msg.reply(0);
                    }
                    video_time = timeMS(th_granule_time(video->mTheora.mCtx, mGranulepos) * 1000);
                    _video_time = video_time;
                    if (tm > video_time)
                        _skipNextFrame = _allowSkipFrames;
                }
            }
        }

        ret = ogg_sync_clear(&_syncState);

        return end;
    }



    bool OggDecoder::handle_theora_data(TheoraOggStream* stream, ogg_packet* packet)
    {
        const th_info& ti = stream->mTheora.mInfo;

        // The granulepos for a packet gives the time of the end of the
        // display interval of the frame in the packet.  We keep the
        // granulepos of the frame we've decoded and use this to know the
        // time when to display the next frame.
        int ret = th_decode_packetin(stream->mTheora.mCtx,
                                     packet,
                                     &mGranulepos);
        if (ret >= 0)
        {
            if (ret == TH_DUPFRAME)
                return true;

            if (_skipNextFrame)
            {
                _skipNextFrame = false;
                return true;
            }

            // We have a frame. Get the YUV data
            th_ycbcr_buffer buffer;
            ret = th_decode_ycbcr_out(stream->mTheora.mCtx, buffer);
            OX_ASSERT(ret == 0);

            // Create an SDL surface to display if we haven't
            // already got one.
            int t = getTimeMS();
            MutexAutoLock al(_mutex);
            t = getTimeMS() - t;
            //logs::messageln("lock %d", t);


            ImageData dstYA = _surfaceYA.lock();
            unsigned char* destYA = dstYA.data;

            const unsigned char* srcY = buffer[0].data + ti.pic_y * buffer[0].stride;

            int stride = buffer[0].stride;
            int w = buffer[0].width;
            int h = buffer[0].height;



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
            {
                for (int y = 0; y != h; y++)
                {
                    const unsigned char* srcLineY = srcY;
                    unsigned char* destLineYA = destYA;

                    for (int x = 0; x != w; x++)
                    {
                        *destLineYA++ = *srcLineY++;
                        //*destLineYA++ = 255;
                    }
                    srcY += stride;
                    destYA += dstYA.pitch;
                }
            }



            ImageData dstUV = _surfaceUV.lock();
            unsigned char* destUV = dstUV.data;

            const unsigned char* srcU = buffer[2].data + ti.pic_y / 2 * buffer[2].stride;
            const unsigned char* srcV = buffer[1].data + ti.pic_y / 2 * buffer[1].stride;
            stride = buffer[1].stride;
            w = buffer[1].width;
            h = buffer[1].height;

            if (_hasAlpha)
                h /= 2;

            for (int y = 0; y != h; y++)
            {
                const unsigned char* srcLineU = srcU;
                const unsigned char* srcLineV = srcV;
                unsigned char* destLineUV = destUV;

                for (int x = 0; x != w; x++)
                {
                    *destLineUV++ = *srcLineU++;
                    *destLineUV++ = *srcLineV++;
                }
                srcU += stride;
                srcV += stride;
                destUV += dstUV.pitch;
            }

            frameUpdated();

            return true;
        }
        else
        {
            //logs::messageln("skip");
        }

        return false;
    }

    bool OggDecoderBase::handle_vorbis_header(TheoraOggStream* stream, ogg_packet* packet)
    {
        int ret = vorbis_synthesis_headerin(&stream->mVorbis.mInfo,
                                            &stream->mVorbis.mComment,
                                            packet);
        // Unlike libtheora, libvorbis does not provide a return value to
        // indicate that we've finished loading the headers and got the
        // first data packet. To detect this I check if I already know the
        // stream type and if the vorbis_synthesis_headerin call failed.
        if (stream->mType == TYPE_VORBIS && ret == OV_ENOTVORBIS)
        {
            // First data packet
            return true;
        }
        else if (ret == 0)
        {
            stream->mType = TYPE_VORBIS;
        }
        return false;
    }


    void MovieSprite::init(bool highQualityShader)
    {
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

        #if defined(__ANDROID__)
        // Android vertex shader fails due to multiple prototypes defined for replaced_get_base()
        _shader->init(
            STDRenderer::uberShaderBody,
            "#define REPLACED_GET_BASE\n",
            base.c_str());
        #else
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
        return new MovieSpriteTheora;
    }

    MovieSpriteTheora::MovieSpriteTheora() : _file(0), _decoder(0)
    {
        _threadID = pthread_self();
    }

    MovieSpriteTheora::~MovieSpriteTheora()
    {
        _clear();
    }

    void MovieSpriteTheora::_initPlayer()
    {
        OX_ASSERT(_file == 0);
        OX_ASSERT(_decoder == 0);

        _file = file::open(_fname.c_str(), "srb");

        _decoder = new OggDecoder(_mtUV, _mtYA, _mutex, _msg, _looped, _hasAlphaChannel, _skipFrames);
        _decoder->init(_file);
        _movieRect = _decoder->_pictureRect;
        _bufferSize = _decoder->_frameSize;

        #if 1
        //printf("%s:%d:%s video buffersize: %f x %f\n", __FILE__, __LINE__, __func__,  _bufferSize.w, _bufferSize.h);
        printf("%s:%d:%s video _movieRect: %d x %d\n", __FILE__, __LINE__, __func__,  _movieRect.getWidth(), _movieRect.getHeight());
        #endif
        setSize(_movieRect.getSize());

        // Running natively the video is always available and loaded
        MovieSprite::asyncLoaded();
    }

    void MovieSpriteTheora::_play()
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
        pthread_create(&_threadID, &attr, &MovieSpriteTheora::_thread, this);

        _msg.send(MSG_WAIT_FIRST_FRAME, 0, 0);
    }

    void MovieSpriteTheora::_pause()
    {
        _msg.post(MSG_PAUSE, 0, 0);
    }

    void MovieSpriteTheora::_resume()
    {
        if (!_paused)
            return;

        _msg.post(MSG_RESUME, 0, 0);
    }

    void MovieSpriteTheora::_setVolume(float v)
    {

    }

    float MovieSpriteTheora::_getVolume() const
    {
        return 0.0f;
    }

    void MovieSpriteTheora::_stop()
    {
        _clear();
    }

    bool MovieSpriteTheora::_isPlaying() const
    {
        return  !pthread_equal(_threadID, pthread_self());
    }

    bool MovieSpriteTheora::_isLoaded() const
    {
        // we're always loaded if playing for native theora player
        return _playing;
    }

    float MovieSpriteTheora::_getMovieLength() const
    {
        return 0.0f;
    }

    float MovieSpriteTheora::_getCurrentTime() const
    {
        if(_decoder)
            return _decoder->getVideoTime();
        else
            return 0.0f;
    }

    void MovieSpriteTheora::_setCurrentTime(const float s)
    {

    }

    void MovieSpriteTheora::_clear()
    {
        _msg.clear();
        if (!pthread_equal(_threadID, pthread_self()))
        {
            if (_paused)
                _msg.post(MSG_RESUME, 0, 0);
            _msg.post(MSG_STOP, 0, 0);

            void* ptr = 0;
            pthread_join(_threadID, &ptr);
            _threadID = pthread_self();
            _msg.clear();
        }


        if (_decoder)
            for (StreamMap::iterator it = _decoder->mStreams.begin(); it != _decoder->mStreams.end(); ++it)
            {
                TheoraOggStream* stream = (*it).second;
                delete stream;
            }


        delete _decoder;
        _decoder = 0;

        if (_file)
            file::close(_file);
        _file = 0;
    }

    void MovieSpriteTheora::_update(const UpdateState&)
    {
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
    }

    void* MovieSpriteTheora::_thread(void* This)
    {
        MovieSpriteTheora* _this = (MovieSpriteTheora*)This;
        _this->_threadThis();
        return 0;
    }

    void MovieSpriteTheora::_threadThis()
    {
        bool stopped = _decoder->play(_file);

        if (!stopped)
        {
            file::seek(_file, 0, SEEK_SET);

            asyncDone();
        }
    }


    /*
    class MovieTween : public Tween
    {
    public:
        MovieTween(const string& name, bool alpha): _alpha(alpha)
        {
            OX_ASSERT(_file == 0);
            OX_ASSERT(_decoder == 0);

            _looped = false;

            _file = file::open(_fname.c_str(), "srb");
            _decoder = new OggDecoder(_mtUV, _mtYA, _mutex, _msg, _looped, _alpha);
            _decoder->init(_file);

            //_movieRect = _decoder->_pictureRect;
            //_bufferSize = _decoder->_frameSize;
            //setSize(_movieRect.getSize());
        }

        void _start(Actor& actor) OVERRIDE
        {
            Sprite& spr = safeCast<Sprite&>(actor);

            Point sz = _decoder->_frameSize;
            sz = Point(nextPOT(sz.x), nextPOT(sz.y));

            _mtUV.init(sz.x / 2, sz.y / 2, TF_A8L8);
            _mtUV.fill_zero();

            _textureUV = IVideoDriver::instance->createTexture();
            _textureUV->init(sz.x, sz.y, _mtUV.getFormat(), false);

            _mtYA.init(sz.x, sz.y, _alpha ? TF_A8L8 : TF_L8);
            _mtYA.fill_zero();
            _textureYA = IVideoDriver::instance->createTexture();
            _textureYA->init(sz.x, sz.y, _mtYA.getFormat(), false);

            if (_alpha)
                spr.setBlendMode(blend_premultiplied_alpha);
            else
                spr.setBlendMode(blend_disabled);

            Diffuse d;
            d.base = _textureYA;
            d.alpha = _textureUV;
            d.premultiplied = true;

            AnimationFrame frame;
            RectF mr = _movieRect.cast<RectF>();
            Vector2 szf = sz.cast<Vector2>();
            RectF uv = RectF(mr.pos.div(szf), mr.size.div(szf));
            frame.init(0, d, uv, mr, mr.size);

            Vector2 s = getSize();
            setAnimFrame(frame);
            setSize(s);

            //Sprite::doRender()
        }

        bool _looped;
        bool _alpha;

        Mutex _mutex;

        Point _bufferSize;
        Rect _movieRect;

        MemoryTexture _mtUV;
        MemoryTexture _mtYA;
        spNativeTexture _textureUV;
        spNativeTexture _textureYA;


        std::string _fname;
        OggDecoder* _decoder;
        file::handle _file;
        pthread_t _threadID;

        ThreadMessages _msg;
    };

    spMovieTween createMovieTween(const std::string& name, bool alpha)
    {
        spMovieTween t = new MovieTween(name, alpha);
        return t;
    }
    */
}
