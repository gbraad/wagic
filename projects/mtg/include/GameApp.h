/*
 *  Wagic, The Homebrew ?! is licensed under the BSD license
 *  See LICENSE in the Folder's root
 *  http://wololo.net/wagic/
 */

#ifndef _GAMEAPP_H_
#define _GAMEAPP_H_

#include <JApp.h>
#include <JGE.h>
#include <JSprite.h>
#include <JLBFont.h>
#include <hge/hgeparticle.h>
#include "WResourceManager.h"

#include "GameState.h"
#include "GameOptions.h"

#include "MTGDeck.h"
#include "MTGCard.h"
#include "MTGGameZones.h"

#include "CardEffect.h"

enum
{
    PLAYER_TYPE_CPU = 0,
    PLAYER_TYPE_HUMAN=1,
    PLAYER_TYPE_TESTSUITE=2
};

enum
{
    GAME_TYPE_CLASSIC,
    GAME_TYPE_MOMIR,
    GAME_TYPE_RANDOM1,
    GAME_TYPE_RANDOM2,
    GAME_TYPE_STORY,
    GAME_TYPE_DEMO
};

class MTGAllCards;
class TransitionBase;

class GameApp: public JApp
{

private:
#ifdef DEBUG
    int nbUpdates;
    float totalFPS;
#endif
    bool mShowDebugInfo;
    int mScreenShotCount;

    GameState* mCurrentState;
    GameState* mNextState;
    GameState* mGameStates[GAME_STATE_MAX];
public:

    int gameType;
    CardEffect *effect;

    GameApp();
    virtual ~GameApp();

    virtual void Create();
    virtual void Destroy();
    virtual void Update();
    virtual void Render();
    virtual void Pause();
    virtual void Resume();

    void LoadGameStates();
    void SetNextState(int state);
    void DoTransition(int trans, int tostate, float dur = -1, bool animonly = false);
    void DoAnimation(int trans, float dur = -1);
    static hgeParticleSystem * Particles[6];
    static int HasMusic;
    static string systemError;
    static JMusic* music;
    static string currentMusicFile;
    static void playMusic(string filename, bool loop = true);
    static int players[2];

};

extern JQuadPtr manaIcons[7];

#endif
