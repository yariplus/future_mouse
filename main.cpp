#define OLC_PGE_APPLICATION
#define OLC_PGEX_GRAPHICS2D

#include "olcPixelGameEngine.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define TILE_WIDTH 16
#define TILE_HEIGHT 16
#define GRAVITY 20.0f

bool playJump = false,
     playShoot = false,
     playExplode = false,
     playAttack = false,
     playHurt = false,
     playPickup = false;

int bulletCount = 0;
int cheeseGoal = 50;

bool won = false;
bool gameover = false;

#define SAMPLE_FORMAT   ma_format_f32
#define CHANNEL_COUNT   6
#define SAMPLE_RATE     48000

ma_uint32   g_decoderCount;
ma_decoder* g_pDecoders;
ma_bool32*  g_pDecodersAtEnd;

ma_uint32 read_and_mix_pcm_frames_f32(ma_decoder* pDecoder, float* pOutputF32, ma_uint32 frameCount)
{
    float temp[4096];
    ma_uint32 tempCapInFrames = ma_countof(temp) / CHANNEL_COUNT;
    ma_uint32 totalFramesRead = 0;

    while (totalFramesRead < frameCount) {
        ma_uint32 iSample;
        ma_uint32 framesReadThisIteration;
        ma_uint32 totalFramesRemaining = frameCount - totalFramesRead;
        ma_uint32 framesToReadThisIteration = tempCapInFrames;
        if (framesToReadThisIteration > totalFramesRemaining) {
            framesToReadThisIteration = totalFramesRemaining;
        }

        framesReadThisIteration = (ma_uint32)ma_decoder_read_pcm_frames(pDecoder, temp, framesToReadThisIteration);
        if (framesReadThisIteration == 0) {
            break;
        }

        /* Mix the frames together. */
        for (iSample = 0; iSample < framesReadThisIteration*CHANNEL_COUNT; ++iSample) {
            pOutputF32[totalFramesRead*CHANNEL_COUNT + iSample] += temp[iSample];
        }

        totalFramesRead += framesReadThisIteration;

        if (framesReadThisIteration < framesToReadThisIteration) {
            break;  /* Reached EOF. */
        }
    }
    
    return totalFramesRead;
}

void data_mixed_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    float* pOutputF32 = (float*)pOutput;
    ma_uint32 iDecoder;

    MA_ASSERT(pDevice->playback.format == SAMPLE_FORMAT);

    for (iDecoder = 0; iDecoder < g_decoderCount; ++iDecoder) {
            if ((iDecoder == 0 && playJump) || 
                (iDecoder == 1 && playShoot) || 
                (iDecoder == 2 && playExplode) || 
                (iDecoder == 3 && playAttack) || 
                (iDecoder == 4 && playHurt) || 
                (iDecoder == 5 && playPickup))
            {
                ma_uint32 framesRead = read_and_mix_pcm_frames_f32(&g_pDecoders[iDecoder], pOutputF32, frameCount);
                if (framesRead < frameCount) {
                    g_pDecodersAtEnd[iDecoder] = MA_TRUE;
                }
            }
    }

    (void)pInput;
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    ma_decoder** pDecoder = (ma_decoder**)pDevice->pUserData;
    if (pDecoder == NULL)
    {
        return;
    }

    if (playJump)
    {
        ma_decoder_read_pcm_frames(pDecoder[0], pOutput, frameCount);
    }

    if (playShoot)
    {
        ma_decoder_read_pcm_frames(pDecoder[1], pOutput, frameCount);
    }

    (void)pInput;
}

struct Game : public olc::PixelGameEngine
{
    int nCheeseCount = 0;
    std::string tCheese;

    olc::Sprite * spritesheet = nullptr;
    olc::Decal * decalsheet = nullptr;

    std::string text = "nothing";

    ma_result result;
    ma_decoder_config decoderConfig;
    ma_device_config deviceConfig;
    ma_device device;
    ma_uint32 iDecoder;

    struct Object
    {
        Game * game;

        float fPosX = 0.0f, fPosY = 0.0f;
        float fVelX = 0.0f, fVelY = 0.0f;
        float fAccX = 0.03125f * 6, fAccY = 0.0f;

        float life = 0.0f;
        float ttl = 0.0f;
        bool isDead = false;
        bool isHurtable = false;
        bool isVisible = true;

        bool isMovingLeft = false, isMovingRight = false;

        virtual void update(float fElapsedTime) = 0;
        virtual void draw() = 0;

        int nSpritePosX, nSpritePosY;

        float fFrameCounter;
        bool bNextFrame;
        int frame = 0;
        bool bFlipH = false;

        void frameAdvance(float fElapsedTime)
        {
            fFrameCounter += abs(fVelX) * fElapsedTime;
            if (fFrameCounter > 0.8f) bNextFrame = true;
        }
    };

    struct TextBox : public Object
    {
        std::string text;

        TextBox(Game * game, std::string text, float posx, float posy)
        {
            this->game = game;
            this->fPosX = posx;
            this->fPosY = posy;
            this->text = text;
        }

        void update(float fElapsedTime) override
        {

        }

        void draw() override
        {
            if (abs(game->player->fPosX - fPosX) < 0.8f && abs(game->player->fPosY - fPosY) < 0.8f)
            {
                olc::vf2d pos = game->UnitToReal(fPosX - 1.6f, fPosY - 1.6f);

                if (gameover)
                {
                    pos.x = game->ScreenWidth() / 2 - 60;
                    pos.y = game->ScreenHeight() / 2 - 34;
                }

                game->DrawStringPropDecal({pos.x - 1, pos.y + 1}, text, olc::BLACK);
                game->DrawStringPropDecal({pos.x, pos.y}, text, gameover && !won ? olc::RED : olc::YELLOW);

                if (game->player->life > 0 && game->nCheeseCount == cheeseGoal)
                {
                    gameover = true;
                    won = true;

                    game->aTexts[0]->fPosX = fPosX;
                    game->aTexts[0]->fPosY = fPosY;
                }
            }

            
        }
    };

    struct Player : public Object
    {
        Player(Game * game, int nSpritePosX, int nSpritePosY)
        {
            this->game = game;
            this->nSpritePosX = nSpritePosX;
            this->nSpritePosY = nSpritePosY;

            this->life = 5;
        }

        bool bJumping = false;
        bool bGrounded = true;
        bool isShooting = false;

        void update(float fElapsedTime) override
        {
            if (!gameover)
            {
                for (auto o : game->aObjects)
                {
                    if (o->isHurtable)
                    {
                        if (abs(o->fPosX - this->fPosX) < 0.65f && abs(o->fPosY - this->fPosY) < 0.65f)
                        {
                            o->isDead = true;
                            life--;
                            if (life < 1)
                            {
                                life = 0;
                                gameover = true;

                                game->aTexts[0]->fPosX = fPosX;
                                game->aTexts[0]->fPosY = fPosY;
                            }

                            playHurt = true;
                            ma_decoder_seek_to_pcm_frame(&g_pDecoders[4], 0);
                        }
                    }
                }

                if (isShooting && bulletCount < 2)
                {
                    bulletCount++;
                    playShoot = true;
                    ma_result result = ma_decoder_seek_to_pcm_frame(&g_pDecoders[1], 0);
                    if (result != MA_SUCCESS) {
                        game->text = "error";
                    }
                    

                    Bullet * bullet = new Bullet(game, 16 * 3, 16);

                    bullet->fPosY = this->fPosY;

                    if (bFlipH)
                    {
                        bullet->fVelX = -16.0f;
                        bullet->fPosX = this->fPosX - 0.6f;
                    }
                    else
                    {
                        bullet->fVelX = 16.0f;
                        bullet->fPosX = this->fPosX + 0.6f;
                    }

                    game->aObjects.push_back(bullet);
                }
                isShooting = false;

                if (!(isMovingLeft && isMovingRight))
                {
                    if (isMovingLeft)
                    {
                        this->fVelX += (bGrounded ? -25.0f : -15.0f) * fElapsedTime;
                    }
                    if (isMovingRight)
                    {
                        this->fVelX += (bGrounded ? 25.0f : 15.0f) * fElapsedTime;
                    }
                }

                // Drag
                if (bGrounded)
                {
                    fVelX += -3.2f * fVelX * fElapsedTime;
                    if (fabs(fVelX) < 0.02f)
                        fVelX = 0.0f;
                }

                // Gravity
                fVelY += GRAVITY * fElapsedTime;

                // Jump
                if (bJumping && bGrounded)
                {
                    fVelY = -12.0f;
                    bGrounded = false;

                    playJump = true;
                    ma_decoder_seek_to_pcm_frame(&g_pDecoders[0], 0);
                }
                bJumping = false;

                // Clamp velocities
                if (fVelX > 10.0f)
                    fVelX = 10.0f;

                if (fVelX < -10.0f)
                    fVelX = -10.0f;

                if (fVelY > 100.0f)
                    fVelY = 100.0f;

                if (fVelY < -100.0f)
                    fVelY = -100.0f;

                float fNewPosX = fPosX + fVelX * fElapsedTime;
                float fNewPosY = fPosY + fVelY * fElapsedTime;

                if (fVelX < 0)
                {
                    nSpritePosX = 16;
                    bFlipH = true;

                    if (fNewPosX < 0)
                    {
                        fNewPosX = 0;
                        fVelX = 0;

                        nSpritePosX = 0;
                    }
                    else if (game->GetTile(fNewPosX + 0.0f, fPosY + 0.3f) != L'.' || game->GetTile(fNewPosX + 0.0f, fPosY + 0.9f) != L'.')
                    {
                        fNewPosX = (int)fNewPosX + 1;
                        fVelX = 0;

                        nSpritePosX = 0;
                    }
                }
                else if (fVelX > 0)
                {
                    nSpritePosX = 16;
                    bFlipH = false;

                    if (game->GetTile(fNewPosX + 1.0f, fPosY + 0.3f) != L'.' || game->GetTile(fNewPosX + 1.0f, fPosY + 0.9f) != L'.')
                    {
                        fNewPosX = (int)fNewPosX;
                        fVelX = 0;

                        nSpritePosX = 0;
                    }
                }
                else
                {
                    nSpritePosX = 0;
                }

                frameAdvance(fElapsedTime);

                if (bNextFrame)
                {
                    if (frame == 0)
                        frame = 1;
                    else
                        frame = 0;

                    bNextFrame = false;
                    fFrameCounter = 0;
                }

                if (frame == 1 && nSpritePosX != 0) nSpritePosX += 16;

                if (fVelY <= 0)
                {
                    if (fNewPosY < 0)
                    {
                        fNewPosY = 0;
                        fVelY = 0;
                    }
                    else if (game->GetTile(fNewPosX + 0.0f, fNewPosY + 0.3f) != L'.' || game->GetTile(fNewPosX + 0.9f, fNewPosY + 0.3f) != L'.' || fNewPosY < 0)
                    {
                        fNewPosY = (int)fNewPosY + 1;
                        fVelY = 0;
                    }
                }
                else
                {
                    if (game->GetTile(fNewPosX + 0.0f, fNewPosY + 1.0f) != L'.' || game->GetTile(fNewPosX + 0.9f, fNewPosY + 1.0f) != L'.')
                    {
                        fNewPosY = (int)fNewPosY;
                        bGrounded = true;
                        fVelY = 0;
                    }
                    else
                    {
                        bGrounded = false;
                    }
                }

                if (bFlipH)
                    nSpritePosY = 32;
                else
                    nSpritePosY = 16;

                fPosX = fNewPosX;
                fPosY = fNewPosY;

                if (game->GetTileObject(fPosX, fPosY) == L'c')
                {
                    game->getCheese();
                    game->SetTileObject(fPosX, fPosY, L'.');
                }
                if (game->GetTileObject(fPosX + 1, fPosY) == L'c')
                {
                    game->getCheese();
                    game->SetTileObject(fPosX + 1, fPosY, L'.');
                }
                if (game->GetTileObject(fPosX, fPosY + 1) == L'c')
                {
                    game->getCheese();
                    game->SetTileObject(fPosX, fPosY + 1, L'.');
                }
                if (game->GetTileObject(fPosX + 1, fPosY + 1) == L'c')
                {
                    game->getCheese();
                    game->SetTileObject(fPosX + 1, fPosY + 1, L'.');
                }
            }
        }

        void draw() override
        {
            if (gameover && !won)
                game->DrawPartialFlippedDecal(game->UnitToReal(this->fPosX, this->fPosY + 0.5f), olc::vi2d(TILE_WIDTH, TILE_HEIGHT), game->decalsheet, {(float)this->nSpritePosX, (float)this->nSpritePosY}, {16, 16}, olc::Sprite::Flip::HORIZ | olc::Sprite::Flip::VERT, olc::GREEN);
            else
                game->DrawPartialFlippedDecal(game->UnitToReal(this->fPosX, this->fPosY), olc::vi2d(TILE_WIDTH, TILE_HEIGHT), game->decalsheet, {(float)this->nSpritePosX, (float)this->nSpritePosY}, {16, 16});
        }
    };

    void getCheese()
    {
        if (nCheeseCount < cheeseGoal) nCheeseCount++;

        playPickup = true;
        ma_decoder_seek_to_pcm_frame(&g_pDecoders[5], 0);
    }

    struct Monster : public Object
    {
        Monster(Game * game, int fPosX, int fPosY)
        {
            this->game = game;
            this->fPosX = fPosX;
            this->fPosY = fPosY;

            this->isHurtable = true;
        }

        void update(float fElapsedTime) override
        {
        }

        void draw() override
        {
            game->DrawPartialFlippedDecal(game->UnitToReal(this->fPosX, this->fPosY), olc::vi2d(TILE_WIDTH, TILE_HEIGHT), game->decalsheet, {(float)16, (float)48}, {16, 16});
        }
    };

    struct Bullet : public Object
    {
        Bullet(Game * game, int nSpritePosX, int nSpritePosY)
        {
            this->game = game;
            this->nSpritePosX = nSpritePosX;
            this->nSpritePosY = nSpritePosY;

            this->ttl = 0.3f;
        }

        void update(float fElapsedTime) override
        {
            this->fPosX += fVelX * fElapsedTime;

            life += fElapsedTime;
            if (life > ttl)
            {
                this->isDead = true;
                bulletCount--;
            }

            for (auto o : game->aObjects)
            {
                if (o->isHurtable)
                {
                    if (abs(o->fPosX - this->fPosX) < 0.8f && abs(o->fPosY - this->fPosY) < 0.8f)
                    {
                        o->isDead = true;
                        this->isDead = true;
                        bulletCount--;

                        playExplode = true;
                        ma_decoder_seek_to_pcm_frame(&g_pDecoders[2], 0);
                    }
                }
            }
        }

        void draw() override
        {
            game->DrawPartialDecal(game->UnitToReal(this->fPosX, this->fPosY), olc::vi2d(TILE_WIDTH, TILE_HEIGHT), game->decalsheet, {(float)this->nSpritePosX, (float)this->nSpritePosY}, {16, 16});
        }
    };

    Player * player = new Player(this, 0, 16);

    std::vector<Object *> aObjects;
    std::vector<TextBox *> aTexts;

    int nVisibleTilesX;
    int nVisibleTilesY;

    float fOffsetX, fOffsetY;
    float fTileOffsetX, fTileOffsetY;
    float fCameraPosX, fCameraPosY;

    int nLevelWidth = 64 * 3;
    int nLevelHeight = 16;

    std::wstring sLevel;
    std::wstring sObjects;

    olc::vi2d UnitToReal(float x, float y)
    {
        return olc::vi2d((x - fOffsetX) * TILE_WIDTH, (y - fOffsetY) * TILE_HEIGHT);
    }

    olc::vi2d RealToUnit(float x, float y)
    {
        return olc::vi2d(x/(float)TILE_WIDTH + fOffsetX, y/(float)TILE_HEIGHT + fOffsetY);
    }

    bool OnUserCreate() override
    {
        spritesheet = new olc::Sprite("assets/sprites.png");
        decalsheet = new olc::Decal(spritesheet);

        aTexts.push_back(new TextBox(this, "", 185, 14));

        player->fPosX = 1;
        player->fPosY = 13;

        nVisibleTilesX = ScreenWidth() / TILE_WIDTH;
        nVisibleTilesY = ScreenHeight() / TILE_HEIGHT;

        sLevel += L"#..............................................................................................................................................................................................#";
        sLevel += L"#.############......#....................................................................................###########...........................................................................#";
        sLevel += L"#...................#.........#........#.......#...............................................................#....##......................................................#..#..#..###########";
        sLevel += L"#...................#..........................................................................................#.....................................................#.........................#";
        sLevel += L"##..................#................................##...................................................####.##.........#########..........#.#.....#.......###..........#....................#";
        sLevel += L"#...................###############################################.................#......#.................#.###...#......................#...#....#......#..................................#";
        sLevel += L"#...................................................................................########.................#.#...........................#.....#...#......#.......#..........................#";
        sLevel += L"#.############...........................................................................................#####.#...........#########........#...#....#......#.....................###########..#";
        sLevel += L"#.....................................................................#......#...............................#.#..#..........................#.#.....#####...###..........#.......#............#";
        sLevel += L"#..............................#........#.............................########..............#................#.####...............................................................#............#";
        sLevel += L"##.##.##.##.................................................................................###.###..........#.#............#########......#.##.....####...####....#..............#..###########";
        sLevel += L"#.......................................................#.......#............................................#.#...........................#...#...#.......#.................#....#............#";
        sLevel += L"#............................##############.............#########............................................#.............................####...#...##...###.........#..........##...........#";
        sLevel += L"#............#......#.......################..................................#######........................#####.#####...................#.......#....#..#......................###..........#";
        sLevel += L"#............#......#......##################...##.................##..##.................##..##.......##....#.........#...................#........####...####...................####.........#";
        sLevel += L"################################################################################################################################################################################################";

        sObjects += L"#....m.....c.c.................................................................................................................................................................................#";
        sObjects += L"#.############......#.........c........c.......c........................................................############.....................................................................c.c.cm#";
        sObjects += L"#...................#c.c......#........#.......#...............................................................#c...##......................................................#..#..#..###########";
        sObjects += L"#...................#................................m.....................................................m...#c...........m.m......................c.........m.....#.........................#";
        sObjects += L"##..................#c.c..m......m..........m........##..................................................#####.##.........#########..........#.#.....#.......###...............................#";
        sObjects += L"#...................###############################################.................#..m.m.#.................#.###..........................#c.c#m...#......#..................................#";
        sObjects += L"#....m.....c.c................c.c......c.c..........................................########...............cm#.#...............m.m.........##...##...#......#.........................m...m....#";
        sObjects += L"#.############...........................................................................................#####.#.........##########.........#c.c#....#cm....#c.c..........c.......###########..#";
        sObjects += L"#.......................................m.............................#..m...#...............................#.#.m#..........................#.#.....#####...###..........#.......#............#";
        sObjects += L"#..............................#........#.............................########...............c...c...........#.####........m.m..............................m.....................#.....m...m..#";
        sObjects += L"##.##.##.##....c.c..........................................................................###.###........c.#.#..........###########......#.##.....####...####...................#..###########";
        sObjects += L"#.............................m....mm....m..............#...m...#............................................#.#...........................#..c#...#..m....#...........c..........#........c.c.#";
        sObjects += L"#............................##############.............#########..............m..m........................c.#........m....................####...#...##...###.........#..........##...........#";
        sObjects += L"#............#......#.......################..............c.c.c...............#######..........m.............#####.#####...................#.......#...c#..#c.....................###......c.c.#";
        sObjects += L"#............#..mm..#......##################.m.##.........m.m.........##..m...c.c.c....m.##.......m...##.m..#.c.c.c.c.#........m........m.#c..m.m..####...####......c..m..c....m.####..mym....#";
        sObjects += L"################################################################################################################################################################################################";

        for (int x = 0; x < nLevelWidth; x++)
        {
            for (int y = 0; y < nLevelHeight; y++)
            {
                wchar_t sObjectID = GetTileObject(x, y);

                if (sObjectID == 'm')
                {
                    aObjects.push_back(new Monster(this, x, y));
                }
            }
        }

        g_decoderCount = 6;
        g_pDecoders      = (ma_decoder*)malloc(sizeof(*g_pDecoders)      * g_decoderCount);
        g_pDecodersAtEnd = (ma_bool32*) malloc(sizeof(*g_pDecodersAtEnd) * g_decoderCount);

        decoderConfig = ma_decoder_config_init(SAMPLE_FORMAT, CHANNEL_COUNT, SAMPLE_RATE);
        for (iDecoder = 0; iDecoder < g_decoderCount; ++iDecoder) {
            switch (iDecoder)
            {
                case 0:
                    result = ma_decoder_init_file("assets/Jump9.wav", &decoderConfig, &g_pDecoders[iDecoder]);
                    break;
                case 1:
                    result = ma_decoder_init_file("assets/Laser_Shoot19.wav", &decoderConfig, &g_pDecoders[iDecoder]);
                    break;
                case 2:
                    result = ma_decoder_init_file("assets/Explosion17.wav", &decoderConfig, &g_pDecoders[iDecoder]);
                    break;
                case 3:
                    result = ma_decoder_init_file("assets/Hit19.wav", &decoderConfig, &g_pDecoders[iDecoder]);
                    break;
                case 4:
                    result = ma_decoder_init_file("assets/Hit_Hurt24.wav", &decoderConfig, &g_pDecoders[iDecoder]);
                    break;
                case 5:
                    result = ma_decoder_init_file("assets/Pickup_Coin9.wav", &decoderConfig, &g_pDecoders[iDecoder]);
                    break;
            }
            
            if (result != MA_SUCCESS) {
                ma_uint32 iDecoder2;
                for (iDecoder2 = 0; iDecoder2 < iDecoder; ++iDecoder2) {
                    ma_decoder_uninit(&g_pDecoders[iDecoder2]);
                }
                free(g_pDecoders);
                free(g_pDecodersAtEnd);
                text = "failed at " + std::to_string(iDecoder);
                return false;
            }
            g_pDecodersAtEnd[iDecoder] = MA_FALSE;
        }

        /* Create only a single device. The decoders will be mixed together in the callback. In this example the data format needs to be the same as the decoders. */
        deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.format   = SAMPLE_FORMAT;
        deviceConfig.playback.channels = CHANNEL_COUNT;
        deviceConfig.sampleRate        = SAMPLE_RATE;
        deviceConfig.dataCallback      = data_mixed_callback;
        deviceConfig.pUserData         = NULL;

        if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
            for (iDecoder = 0; iDecoder < g_decoderCount; ++iDecoder) {
                ma_decoder_uninit(&g_pDecoders[iDecoder]);
            }
            free(g_pDecoders);
            free(g_pDecodersAtEnd);

            text = "Failed to open playback device.";
            return false;
        }

        if (ma_device_start(&device) != MA_SUCCESS) {
            ma_device_uninit(&device);
            for (iDecoder = 0; iDecoder < g_decoderCount; ++iDecoder) {
                ma_decoder_uninit(&g_pDecoders[iDecoder]);
            }
            free(g_pDecoders);
            free(g_pDecodersAtEnd);

            text = "Failed to start playback device.";
            return false;
        }

        ma_device_set_master_volume(&device, 0.2f);

        return true;
    };

    bool OnUserUpdate(float fElapsedTime) override
    {
        if (nCheeseCount < cheeseGoal)
            aTexts[0]->text = "Not Enough\n Cheese!";
        else
            aTexts[0]->text = "        The Future of  \nMouse Land is Saved!";

        if (gameover && !won)
            aTexts[0]->text = "        The Future of  \nMouse Land is Doomed!";

        Clear(olc::CYAN);

        if (IsFocused()) ProcessInput(fElapsedTime);

        UpdateCamera();

        DrawWorld();
        UpdateObjects(fElapsedTime);

        std::string spacer = nCheeseCount < 10 ? " " : "";
        tCheese = "Cheese: " + spacer + std::to_string(nCheeseCount) + "/" + std::to_string(cheeseGoal);

        DrawObjects();

        DrawString(142, 9, tCheese, olc::BLACK, 1);
        DrawString(143, 8, tCheese, olc::YELLOW, 1);

        return true;
    };

    bool ProcessInput(float fElapsedTime)
    {
        if (GetKey(olc::Key::LEFT).bPressed) {
            player->isMovingLeft = true;
        }
        if (GetKey(olc::Key::RIGHT).bPressed) {
            player->isMovingRight = true;
        }
        if (GetKey(olc::Key::Z).bPressed) {
            if (!player->isShooting) player->isShooting = true;
        }

        if (GetKey(olc::Key::LEFT).bReleased) {
            player->isMovingLeft = false;
        }
        if (GetKey(olc::Key::RIGHT).bReleased) {
            player->isMovingRight = false;
        }

        if (GetKey(olc::Key::SPACE).bPressed) {
            player->bJumping = true;
        }

        if (GetKey(olc::Key::SPACE).bReleased) {
            
        }

        return true;
    }

    void UpdateCamera()
    {
        fCameraPosX = player->fPosX;
        fCameraPosY = player->fPosY;

        // top left coordinate
        fOffsetX = fCameraPosX - (float)nVisibleTilesX / 2.0f;
        fOffsetY = fCameraPosY - (float)nVisibleTilesY / 2.0f;

        // Clamp
        if (fOffsetX < 0) fOffsetX = 0;
        if (fOffsetY < 0) fOffsetY = 0;
        if (fOffsetX > nLevelWidth - nVisibleTilesX) fOffsetX = nLevelWidth - nVisibleTilesX;
        if (fOffsetY > nLevelHeight - nVisibleTilesY) fOffsetY = nLevelHeight - nVisibleTilesY;

        // Tile offsets
        fTileOffsetX = (fOffsetX - (int)fOffsetX) * TILE_WIDTH;
        fTileOffsetY = (fOffsetY - (int)fOffsetY) * TILE_HEIGHT;
    }

    void DrawWorld()
    {
        for (int x = -1; x < nVisibleTilesX + 1; x++)
        {
            for (int y = -1; y < nVisibleTilesY + 1; y++)
            {
                wchar_t sTileID = GetTile(x + fOffsetX, y + fOffsetY);
                wchar_t sObjectID = GetTileObject(x + fOffsetX, y + fOffsetY);

                switch (sTileID)
                {
                    case L'#':
                        DrawPartialDecal({x * TILE_WIDTH - fTileOffsetX, y * TILE_HEIGHT - fTileOffsetY}, {16, 16}, decalsheet, {0, 0}, {16, 16});
                        break;
                    case L'.':
                    default:
                        FillRect(x * TILE_WIDTH - fTileOffsetX, y * TILE_HEIGHT - fTileOffsetY, TILE_WIDTH, TILE_HEIGHT, olc::CYAN);
                        break;
                }

                switch (sObjectID)
                {
                    case L'c':
                        DrawPartialDecal({x * TILE_WIDTH - fTileOffsetX, y * TILE_HEIGHT - fTileOffsetY}, {16, 16}, decalsheet, {32, 0}, {16, 16});
                        break;
                    case L'y':
                        DrawPartialDecal({x * TILE_WIDTH - fTileOffsetX, y * TILE_HEIGHT - fTileOffsetY}, {16, 16}, decalsheet, {48, 0}, {16, 16});
                        break;
                    default:
                        break;
                }
            }
        }
    }

    wchar_t GetTile(int x, int y)
    {
        if (x >= 0 && x < nLevelWidth && y >= 0 && y < nLevelHeight)
            return sLevel[y * nLevelWidth + x];
        else
            return L' ';
    };

    void SetTile(int x, int y, wchar_t c)
    {
        if (x >= 0 && x < nLevelWidth && y >= 0 && y < nLevelHeight)
            sLevel[y * nLevelWidth + x] = c;
    };

    wchar_t GetTileObject(int x, int y)
    {
        if (x >= 0 && x < nLevelWidth && y >= 0 && y < nLevelHeight)
            return sObjects[y * nLevelWidth + x];
        else
            return L' ';
    };

    void SetTileObject(int x, int y, wchar_t c)
    {
        if (x >= 0 && x < nLevelWidth && y >= 0 && y < nLevelHeight)
            sObjects[y * nLevelWidth + x] = c;
    };

    void UpdateObjects(float fElapsedTime)
    {
        player->update(fElapsedTime);

        for (auto o : aObjects)
            o->update(fElapsedTime);

        aObjects.erase(
            std::remove_if(
                aObjects.begin(),
                aObjects.end(),
                [](auto o)
                {
                    return o->isDead;
                }
            ), aObjects.end());
    }

    void DrawObjects()
    {
        player->draw();

        for (auto o : aObjects)
            o->draw();

        for (auto o : aTexts)
            o->draw();

        for (int i = 0; i < player->life; i++)
            DrawPartialDecal({(float)(4 + 18 * i), 4}, {16, 16}, decalsheet, {32, 48}, {16, 16});
    }

    bool OnUserDestroy() override
    {
        ma_device_uninit(&device);
        
        for (iDecoder = 0; iDecoder < g_decoderCount; ++iDecoder) {
            ma_decoder_uninit(&g_pDecoders[iDecoder]);
        }
        free(g_pDecoders);
        free(g_pDecodersAtEnd);

        return true; 
    }

    void DrawPartialFlippedDecal(const olc::vf2d& pos, const olc::vf2d& size, olc::Decal* decal, const olc::vf2d& source_pos, const olc::vf2d& source_size, uint8_t flip = 0, const olc::Pixel& tint = olc::WHITE)
    {
        if (decal == nullptr)
			return;

        int32_t x = (int)pos.x, y = (int)pos.y;
		int32_t fh = 0, w = (int)size.x;
		int32_t fv = 0, h = (int)size.y;

		if (flip & olc::Sprite::Flip::HORIZ) { fh = 1; }
		if (flip & olc::Sprite::Flip::VERT) { fv = 1; }

        DrawPartialWarpedDecal(decalsheet,{
            {(float)(x + (0 * w) +  1 * (fh * w)), (float)(y + (0 * h) +  1 * (fv * h))},
            {(float)(x + (0 * w) +  1 * (fh * w)), (float)(y + (1 * h) + -1 * (fv * h))},
            {(float)(x + (1 * w) + -1 * (fh * w)), (float)(y + (1 * h) + -1 * (fv * h))},
            {(float)(x + (1 * w) + -1 * (fh * w)), (float)(y + (0 * h) +  1 * (fv * h))}
        }, source_pos, source_size, tint);
    }
};

int main()
{
    Game game;

    if (game.Construct(256, 192, 5, 5)) game.Start();

    return 0;
}
