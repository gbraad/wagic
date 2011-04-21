#include "PrecompiledHeader.h"

#include "GameObserver.h"
#include "CardGui.h"
#include "Damage.h"
#include "Rules.h"
#include "ExtraCost.h"
#include "Subtypes.h"
#include <JLogger.h>
#include <JRenderer.h>

GameObserver * GameObserver::mInstance = NULL;

GameObserver* GameObserver::GetInstance()
{
    return mInstance;
}

void GameObserver::EndInstance()
{

    SAFE_DELETE(mInstance);
}

void GameObserver::Init(Player * _players[], int _nbplayers)
{
    mInstance = NEW GameObserver(_players, _nbplayers);
}

GameObserver::GameObserver(Player * _players[], int _nb_players)
{
    for (int i = 0; i < _nb_players; i++)
    {
        players[i] = _players[i];
    }
    currentPlayer = NULL;
    currentActionPlayer = NULL;
    isInterrupting = NULL;
    currentPlayerId = 0;
    nbPlayers = _nb_players;
    currentGamePhase = -1;
    targetChooser = NULL;
    cardWaitingForTargets = NULL;
    mExtraPayment = NULL;
    gameOver = NULL;
    phaseRing = NULL;
    replacementEffects = NEW ReplacementEffects();
    combatStep = BLOCKERS;
    mRules = NULL;
}

int GameObserver::getCurrentGamePhase()
{
    return currentGamePhase;
}

Player * GameObserver::opponent()
{
    int index = (currentPlayerId + 1) % nbPlayers;
    return players[index];
}

void GameObserver::nextPlayer()
{
    turn++;
    currentPlayerId = (currentPlayerId + 1) % nbPlayers;
    currentPlayer = players[currentPlayerId];
    currentActionPlayer = currentPlayer;
    combatStep = BLOCKERS;
}
void GameObserver::nextGamePhase()
{
    Phase * cPhaseOld = phaseRing->getCurrentPhase();
    if (cPhaseOld->id == Constants::MTG_PHASE_COMBATDAMAGE) 
    	if ((FIRST_STRIKE == combatStep) || (END_FIRST_STRIKE == combatStep) || (DAMAGE == combatStep))
    	{
        	nextCombatStep();
        	return;
    	}

    if (cPhaseOld->id == Constants::MTG_PHASE_COMBATBLOCKERS) 
    	if (BLOCKERS == combatStep || TRIGGERS == combatStep)
    	{
        	nextCombatStep();
        	return;
    	}

    phaseRing->forward();

    //Go directly to end of combat if no attackers
    if (cPhaseOld->id == Constants::MTG_PHASE_COMBATATTACKERS && !(currentPlayer->game->inPlay->getNextAttacker(NULL)))
    {
        phaseRing->forward();
        phaseRing->forward();
    }

    Phase * cPhase = phaseRing->getCurrentPhase();
    currentGamePhase = cPhase->id;

    if (Constants::MTG_PHASE_COMBATDAMAGE == currentGamePhase)
    	nextCombatStep();

    if (currentPlayer != cPhase->player)
    	nextPlayer();

    //init begin of turn
    if (currentGamePhase == Constants::MTG_PHASE_BEFORE_BEGIN)
    {
        cleanupPhase();
        currentPlayer->damageCount = 0;
        currentPlayer->preventable = 0;
        currentPlayer->isPoisoned = false;
        mLayers->actionLayer()->cleanGarbage(); //clean abilities history for this turn;
        mLayers->stackLayer()->garbageCollect(); //clean stack history for this turn;
        mLayers->actionLayer()->Update(0);
        for (int i = 0; i < 2; i++)
        {
            //Cleanup of each player's gamezones
            players[i]->game->beforeBeginPhase();
        }
        combatStep = BLOCKERS;
        return nextGamePhase();
    }

    for (int i = 0; i < 2; ++i)
        players[i]->getManaPool()->init();

    if (currentGamePhase == Constants::MTG_PHASE_AFTER_EOT)
    {
        //Auto Hand cleaning, in case the player didn't do it himself
        while (currentPlayer->game->hand->nb_cards > 7 && currentPlayer->nomaxhandsize == false)
        {
            WEvent * e = NEW WEventCardDiscard(currentPlayer->game->hand->cards[0]);
            GameObserver * game = GameObserver::GetInstance();
            game->receiveEvent(e);
            currentPlayer->game->putInGraveyard(currentPlayer->game->hand->cards[0]);
        }
        mLayers->actionLayer()->Update(0);
        currentPlayer->lifeLostThisTurn = 0;
        currentPlayer->opponent()->lifeLostThisTurn = 0;
        return nextGamePhase();
    }

    //Phase Specific actions
    switch (currentGamePhase)
    {
    case Constants::MTG_PHASE_UNTAP:
        DebugTrace("Untap Phase -------------   Turn " << turn );
        untapPhase();
        break;
    case Constants::MTG_PHASE_DRAW:
        //mLayers->stackLayer()->addDraw(currentPlayer,1);
        break;
    case Constants::MTG_PHASE_COMBATBLOCKERS:
        receiveEvent(NEW WEventAttackersChosen());
        break;
    default:
        break;
    }
}

int GameObserver::cancelCurrentAction()
{
    SAFE_DELETE(targetChooser);
    return mLayers->actionLayer()->cancelCurrentAction();
}

void GameObserver::nextCombatStep()
{
    switch (combatStep)
    {
    case BLOCKERS:
        receiveEvent(NEW WEventBlockersChosen());
        receiveEvent(NEW WEventCombatStepChange(combatStep = TRIGGERS));
        return;

    case TRIGGERS:
        receiveEvent(NEW WEventCombatStepChange(combatStep = ORDER));
        return;
    case ORDER:
        receiveEvent(NEW WEventCombatStepChange(combatStep = FIRST_STRIKE));
        return;
    case FIRST_STRIKE:
        receiveEvent(NEW WEventCombatStepChange(combatStep = END_FIRST_STRIKE));
        return;
    case END_FIRST_STRIKE:
        receiveEvent(NEW WEventCombatStepChange(combatStep = DAMAGE));
        return;
    case DAMAGE:
        receiveEvent(NEW WEventCombatStepChange(combatStep = END_DAMAGE));
        return;
    case END_DAMAGE:
        ; // Nothing : go to next phase
    }
}

void GameObserver::userRequestNextGamePhase()
{
    if (mLayers->stackLayer()->getNext(NULL, 0, NOT_RESOLVED))
    	return;
    if (getCurrentTargetChooser())
    	return;
    //if (mLayers->actionLayer()->isWaitingForAnswer())
    //	return;
    // Wil 12/5/10: additional check, not quite understanding why TargetChooser doesn't seem active at this point.
    // If we deem that an extra cost payment needs to be made, don't allow the next game phase to proceed.
    // Here's what I find weird - if the extra cost is something like a sacrifice, doesn't that imply a TargetChooser?
    if (WaitForExtraPayment(NULL)) 
    	return;

    bool executeNextPhaseImmediately = true;

    Phase * cPhaseOld = phaseRing->getCurrentPhase();
    if ((cPhaseOld->id == Constants::MTG_PHASE_COMBATBLOCKERS && combatStep == ORDER) 
    	|| (cPhaseOld->id == Constants::MTG_PHASE_COMBATBLOCKERS && combatStep == TRIGGERS) 
    	|| (cPhaseOld->id == Constants::MTG_PHASE_COMBATDAMAGE)
        || opponent()->isAI() 
        || options[Options::optionInterrupt(currentGamePhase)].number
    )
    {
        executeNextPhaseImmediately = false;
    }

    if (executeNextPhaseImmediately)
    {
        nextGamePhase();
    }
    else
    {
        mLayers->stackLayer()->AddNextGamePhase();
    }

}

int GameObserver::forceShuffleLibraries()
{
    int result = 0;
    if (players[0]->game->library->needShuffle)
    {
        players[0]->game->library->shuffle();
        players[0]->game->library->needShuffle = false;
        ++result;
    }
    if (players[1]->game->library->needShuffle)
    {
        players[1]->game->library->shuffle();
        players[1]->game->library->needShuffle = false;
        ++result;
    }
    return result;
}

void GameObserver::startGame(Rules * rules)
{
    turn = 0;
    mRules = rules;
    if (rules) 
    	rules->initPlayers();

    options.automaticStyle(players[0], players[1]);

    mLayers = NEW DuelLayers();
    mLayers->init();

    currentPlayerId = 0;
    currentPlayer = players[0];
    currentActionPlayer = currentPlayer;
    phaseRing = NEW PhaseRing(players, nbPlayers);
    if (rules) 
    	rules->initGame();

    //Preload images from hand
    if (!players[0]->isAI())
    {
        for (int i = 0; i < players[0]->game->hand->nb_cards; i++)
        {
            WResourceManager::Instance()->RetrieveCard(players[0]->game->hand->cards[i], CACHE_THUMB);
            WResourceManager::Instance()->RetrieveCard(players[0]->game->hand->cards[i]);
        }
    }

    startedAt = time(0);

    //Difficult mode special stuff
    if (!players[0]->isAI() && players[1]->isAI())
    {
        int difficulty = options[Options::DIFFICULTY].number;
        if (options[Options::DIFFICULTY_MODE_UNLOCKED].number && difficulty)
        {
            Player * p = players[1];
            for (int level = 0; level < difficulty; level++)
            {
                MTGCardInstance * card = NULL;
                MTGGameZone * z = p->game->library;
                for (int j = 0; j < z->nb_cards; j++)
                {
                    MTGCardInstance * _card = z->cards[j];
                    if (_card->isLand())
                    {
                        card = _card;
                        j = z->nb_cards;
                    }
                }
                if (card)
                {
                    MTGCardInstance * copy = p->game->putInZone(card, p->game->library, p->game->stack);
                    Spell * spell = NEW Spell(copy);
                    spell->resolve();
                    delete spell;
                }
            }
        }
    }
}

void GameObserver::addObserver(MTGAbility * observer)
{
    mLayers->actionLayer()->Add(observer);
}

void GameObserver::removeObserver(ActionElement * observer)
{
    if (observer)
        mLayers->actionLayer()->moveToGarbage(observer);

    else
    {
    } //TODO log error
}

GameObserver::~GameObserver()
{
    LOG("==Destroying GameObserver==");
    SAFE_DELETE(targetChooser);
    SAFE_DELETE(mLayers);
    SAFE_DELETE(phaseRing);
    SAFE_DELETE(replacementEffects);
    for (int i = 0; i < nbPlayers; ++i)
    {
        SAFE_DELETE(players[i]);
    }
    LOG("==GameObserver Destroyed==");
}

void GameObserver::Update(float dt)
{
    Player * player = currentPlayer;

    if (Constants::MTG_PHASE_COMBATBLOCKERS == currentGamePhase && BLOCKERS == combatStep) 
    	player = player->opponent();

    currentActionPlayer = player;
    if (isInterrupting) 
    	player = isInterrupting;
    mLayers->Update(dt, player);

    while (mLayers->actionLayer()->stuffHappened)
    {
        mLayers->actionLayer()->Update(0);
    }

    gameStateBasedEffects();
    oldGamePhase = currentGamePhase;
}

//applies damage to creatures after updates
//Players life test
//Handles game state based effects
void GameObserver::gameStateBasedEffects()
{
    //check land playability at start; as we want this effect to happen reguardless of unresolved
    //effects or menus actions
    for (int i = 0; i < 2; i++)
    {
        if(players[i]->poisonCount > 0)
        {
        players[i]->isPoisoned = true;
        }
        else
        {
        players[i]->isPoisoned = false;
        }
    }
    if (mLayers->stackLayer()->count(0, NOT_RESOLVED) != 0)
    	return;
    if (mLayers->actionLayer()->menuObject) 
    	return;
    if (targetChooser || mLayers->actionLayer()->isWaitingForAnswer()) 
    	return;

    ////////////////////////
    //---apply damage-----//
    //after combat effects//
    ////////////////////////
    for (int i = 0; i < 2; i++)
    {
        MTGGameZone * zone = players[i]->game->inPlay;
        for (int j = zone->nb_cards - 1; j >= 0; j--)
        {
            MTGCardInstance * card = zone->cards[j];
            card->afterDamage();
            ///////////////////////////////////////////////////////
            //Remove auras that don't have a valid target anymore//
            ///////////////////////////////////////////////////////
            if (card->target && !isInPlay(card->target) && !card->hasType(Subtypes::TYPE_EQUIPMENT))
            {
                players[i]->game->putInGraveyard(card);
            }
            card->enchanted = false;
            if (card->target && isInPlay(card->target) && !card->hasType(Subtypes::TYPE_EQUIPMENT) && card->hasSubtype(Subtypes::TYPE_AURA))
            {
                card->target->enchanted = true;
            }
            ///////////////////////////
            //reset extracost shadows//
            ///////////////////////////
            card->isExtraCostTarget = false;
            if(mExtraPayment != NULL)
            {
                for(unsigned int ec = 0;ec < mExtraPayment->costs.size();ec++)
                {
                    if( mExtraPayment->costs[ec]->target)
                        mExtraPayment->costs[ec]->target->isExtraCostTarget = true;
                }
            }
            //////////////////////
            //reset morph hiding//
            //////////////////////
            if((card->previous && card->previous->morphed && !card->turningOver) || (card->morphed && !card->turningOver))
            {
                card->morphed = true;
                card->isMorphed = true;
            }
            else
            {
                card->isMorphed = false;
                card->morphed = false;
            }
            //////////////////////////
            //handles phasing events//
            //////////////////////////
            if(card->has(Constants::PHASING)&& currentGamePhase == Constants::MTG_PHASE_UNTAP && currentPlayer == card->controller() && card->phasedTurn != turn && !card->isPhased)
            {
                card->isPhased = true;
                card->phasedTurn = turn;
                if(card->view)
                card->view->alpha = 50;
                card->initAttackersDefensers();
            }
            else if((card->has(Constants::PHASING) || card->isPhased)&& currentGamePhase == Constants::MTG_PHASE_UNTAP && currentPlayer == card->controller() && card->phasedTurn != turn)
            {
                card->isPhased = false;
                card->phasedTurn = turn;
                if(card->view)
                card->view->alpha = 255;
            }
            if (card->target && isInPlay(card->target) && (card->hasSubtype(Subtypes::TYPE_EQUIPMENT) || card->hasSubtype(Subtypes::TYPE_AURA)))
            {
                card->isPhased = card->target->isPhased;
                card->phasedTurn = card->target->phasedTurn;
                if(card->view && card->target->view)
                card->view->alpha = card->target->view->alpha;
            }
            //////////////////////////  
            //forceDestroy over ride//
            //////////////////////////
            if(card->isInPlay())
            {
                card->graveEffects = false;
                card->exileEffects = false;
            }
        }
    }
    //-------------------------------------

    for (int i = 0; i < 2; i++)
    {
        ///////////////////////////////////////////////////////////
        //life checks/poison checks also checks cant win or lose.//
        ///////////////////////////////////////////////////////////
        if (players[i]->life <= 0 || players[i]->poisonCount >= 10)
        {
            int cantlosers = 0;
            MTGGameZone * z = players[i]->game->inPlay;
            int nbcards = z->nb_cards;
            for (int j = 0; j < nbcards; ++j)
            {
                MTGCardInstance * c = z->cards[j];
                if (c->has(Constants::CANTLOSE) || (c->has(Constants::CANTLIFELOSE) && players[i]->poisonCount < 10))
                {
                    cantlosers++;
                }
            }
            MTGGameZone * k = players[i]->opponent()->game->inPlay;
            int onbcards = k->nb_cards;
            for (int m = 0; m < onbcards; ++m)
            {
                MTGCardInstance * e = k->cards[m];
                if (e->has(Constants::CANTWIN))
                {
                    cantlosers++;
                }
            }
            if (cantlosers < 1)
            {
                gameOver = players[i];
            }
        }
    }
    //////////////////////////////////////////////////////
    //-------------card based states effects------------//
    //////////////////////////////////////////////////////
    //ie:cantcast; extra land; extra turn;no max hand;--//
    //////////////////////////////////////////////////////

    for (int i = 0; i < 2; i++)
    {
        //checks if a player has a card which has the stated ability in play.
        Player * p = players[i];
        MTGGameZone * z = players[i]->game->inPlay;
        int nbcards = z->nb_cards;
        //------------------------------
        if (z->hasAbility(Constants::NOMAXHAND))
        {
            p->nomaxhandsize = true;
        }
        else
        {
            p->nomaxhandsize = false;
        }

        /////////////////////////////////////////////////
        //handle end of turn effects while we're at it.//
        /////////////////////////////////////////////////
        if (currentGamePhase == Constants::MTG_PHASE_ENDOFTURN+1)
        {
            for (int j = 0; j < nbcards; ++j)
            {
                MTGCardInstance * c = z->cards[j];

                if(!c)break;
                while (c->flanked)
                {
                    /////////////////////////////////
                    //undoes the flanking on a card//
                    /////////////////////////////////
                    c->power += 1;
                    c->addToToughness(1);
                    c->flanked -= 1;
                }
                if (c->fresh) c->fresh = 0;
                if(c->wasDealtDamage && c->isInPlay())
                c->wasDealtDamage = false;
                c->damageToController = false;
                c->damageToOpponent = false;

            }
            for (int t = 0; t < nbcards; t++)
            {
                MTGCardInstance * c = z->cards[t];

                if (c->has(Constants::TREASON))
                {
                    WEvent * e = NEW WEventCardSacrifice(c);
                    GameObserver * game = GameObserver::GetInstance();
                    game->receiveEvent(e);

                    p->game->putInGraveyard(c);
                }
                if (c->has(Constants::UNEARTH))
                {
                    p->game->putInExile(c);

                }
                if(nbcards > z->nb_cards)
                {
                    t = 0;
                    nbcards = z->nb_cards;
                }
            }

            MTGGameZone * f = p->game->graveyard;
            for (int k = 0; k < f->nb_cards; k++)
            {
                MTGCardInstance * card = f->cards[k];
                card->fresh = 0;
            }
        }
        if (z->nb_cards == 0)
        {
            p->nomaxhandsize = false;
        }
        //////////////////////////
        // Check auras on a card//
        //////////////////////////
        enchantmentStatus();
        /////////////////////////////
        // Check affinity on a card//
        /////////////////////////////
        Affinity();
        /////////////////////////////////////
        // Check colored statuses on cards //
        /////////////////////////////////////
        for(int w = 0;w < z->nb_cards;w++)
        {  
            int colored = 0;
            for (int colorIdx = Constants::MTG_COLOR_GREEN; colorIdx <= Constants::MTG_COLOR_WHITE; ++colorIdx)
            {
                if (z->cards[w]->hasColor(colorIdx))
                    ++colored;
            }
            if(colored > 1)
            {
                z->cards[w]->isMultiColored = 1;
            }
            else
            {
                z->cards[w]->isMultiColored = 0;
            }
            if(z->cards[w]->hasColor(Constants::MTG_COLOR_WHITE) && z->cards[w]->hasColor(Constants::MTG_COLOR_BLACK))
                z->cards[w]->isBlackAndWhite = 1;
            else
                z->cards[w]->isBlackAndWhite = 0;
            if(z->cards[w]->hasColor(Constants::MTG_COLOR_RED) && z->cards[w]->hasColor(Constants::MTG_COLOR_BLUE))
                z->cards[w]->isRedAndBlue = 1;
            else
                z->cards[w]->isRedAndBlue = 0;
            if(z->cards[w]->hasColor(Constants::MTG_COLOR_GREEN) && z->cards[w]->hasColor(Constants::MTG_COLOR_BLACK))
                z->cards[w]->isBlackAndGreen = 1;
            else
                z->cards[w]->isBlackAndGreen = 0;
            if(z->cards[w]->hasColor(Constants::MTG_COLOR_BLUE) && z->cards[w]->hasColor(Constants::MTG_COLOR_GREEN))
                z->cards[w]->isBlueAndGreen = 1;
            else
                z->cards[w]->isBlueAndGreen = 0;
            if(z->cards[w]->hasColor(Constants::MTG_COLOR_RED) && z->cards[w]->hasColor(Constants::MTG_COLOR_WHITE))
                z->cards[w]->isRedAndWhite = 1;
            else
                z->cards[w]->isRedAndWhite = 0;
        }
    }
    ///////////////////////////////////
    //phase based state effects------//
    ///////////////////////////////////
    if (combatStep == TRIGGERS)
    {
        if (!mLayers->stackLayer()->getNext(NULL, 0, NOT_RESOLVED) && !targetChooser
            && !mLayers->actionLayer()->isWaitingForAnswer()) 
            mLayers->stackLayer()->AddNextCombatStep();
    }

    //Auto skip Phases
    GameObserver * game = game->GetInstance();
    int skipLevel = (game->currentPlayer->playMode == Player::MODE_TEST_SUITE) ? Constants::ASKIP_NONE
        : options[Options::ASPHASES].number;
    int nrCreatures = currentPlayer->game->inPlay->countByType("Creature");

    if (skipLevel == Constants::ASKIP_SAFE || skipLevel == Constants::ASKIP_FULL)
    {
        if ((opponent()->isAI() && !(isInterrupting)) && ((currentGamePhase == Constants::MTG_PHASE_UNTAP)
            || (currentGamePhase == Constants::MTG_PHASE_DRAW) || (currentGamePhase == Constants::MTG_PHASE_COMBATBEGIN) 
            || ((currentGamePhase == Constants::MTG_PHASE_COMBATATTACKERS) && (nrCreatures == 0)) 
            || currentGamePhase == Constants::MTG_PHASE_COMBATEND || currentGamePhase == Constants::MTG_PHASE_ENDOFTURN 
            || ((currentGamePhase == Constants::MTG_PHASE_CLEANUP) && (currentPlayer->game->hand->nb_cards < 8)))) 
            userRequestNextGamePhase();
    }
    if (skipLevel == Constants::ASKIP_FULL)
    {
        if ((opponent()->isAI() && !(isInterrupting)) && (currentGamePhase == Constants::MTG_PHASE_UPKEEP 
            || currentGamePhase == Constants::MTG_PHASE_COMBATDAMAGE)) 
            userRequestNextGamePhase();
    }
}

void GameObserver::enchantmentStatus()
{
    for (int i = 0; i < 2; i++)
    {
        MTGGameZone * zone = players[i]->game->inPlay;
        for (int k = zone->nb_cards - 1; k >= 0; k--)
        {
            MTGCardInstance * card = zone->cards[k];
            if (card && !card->hasType(Subtypes::TYPE_EQUIPMENT) && !card->hasSubtype(Subtypes::TYPE_AURA))
            {
                card->enchanted = false;
                card->auras = 0;
            }
        }
        for (int j = zone->nb_cards - 1; j >= 0; j--)
        {
            MTGCardInstance * card = zone->cards[j];
            if (card->target && isInPlay(card->target) && !card->hasType(Subtypes::TYPE_EQUIPMENT) && card->hasSubtype(Subtypes::TYPE_AURA))
            {
                card->target->enchanted = true;
                card->target->auras += 1;
            }
        }
    }
}

void GameObserver::Affinity()
{
    for (int i = 0; i < 2; i++)
    {
        MTGGameZone * zone = players[i]->game->hand;
        for (int k = zone->nb_cards - 1; k >= 0; k--)
        {
            MTGCardInstance * card = zone->cards[k];
            int color = 0;
            string type = "";
            //only do any of the following if a card with the stated ability is in your hand.
           ManaCost * original = NEW ManaCost();
           original->copy(card->model->data->getManaCost());
           //have to run alter cost before affinity or the 2 cancel each other out.
           if(card && (card->getIncreasedManaCost()->getConvertedCost()||card->getReducedManaCost()->getConvertedCost()))
           {
            if(card->getIncreasedManaCost()->getConvertedCost())
            original->add(card->getIncreasedManaCost());
            if(card->getReducedManaCost()->getConvertedCost())
            original->remove(card->getReducedManaCost());
            card->getManaCost()->copy(original);
            }
            if(card && 
                (card->has(Constants::AFFINITYARTIFACTS)||
                card->has(Constants::AFFINITYFOREST)||
                card->has(Constants::AFFINITYGREENCREATURES)||
                card->has(Constants::AFFINITYISLAND)||
                card->has(Constants::AFFINITYMOUNTAIN)||
                card->has(Constants::AFFINITYPLAINS)||
                card->has(Constants::AFFINITYSWAMP))){
                    if (card && card->has(Constants::AFFINITYARTIFACTS))
                    {
                        type = "artifact";
                    }
                    else if (card && card->has(Constants::AFFINITYSWAMP))
                    {
                        type = "swamp";
                    }
                    else if (card && card->has(Constants::AFFINITYMOUNTAIN))
                    {
                        type = "mountain";
                    }
                    else if (card && card->has(Constants::AFFINITYPLAINS))
                    {
                        type = "plains";
                    }
                    else if (card && card->has(Constants::AFFINITYISLAND))
                    {
                        type = "island";
                    }
                    else if (card && card->has(Constants::AFFINITYFOREST))
                    {
                        type = "forest";
                    }
                    else if (card && card->has(Constants::AFFINITYGREENCREATURES))
                    {
                        color = 1;
                        type = "creature";
                    }
                    card->getManaCost()->copy(original);
                    int reduce = 0;
                    if(card->has(Constants::AFFINITYGREENCREATURES))
                    {
                        TargetChooserFactory tf;
                        TargetChooser * tc = tf.createTargetChooser("creature[green]",NULL);
                        reduce = card->controller()->game->battlefield->countByCanTarget(tc);
                        SAFE_DELETE(tc);
                    }
                    else
                    {
                        reduce = card->controller()->game->battlefield->countByType(type.c_str());
                    }

                    for(int reductionIdx = 0; reductionIdx < reduce; reductionIdx++)
                    {
                        if(card->getManaCost()->getCost(color) > 0)
                            card->getManaCost()->remove(color,1);
                    }
            }
            if(original)
            delete original;
        }
    }
}
void GameObserver::Render()
{
    mLayers->Render();
    if (targetChooser || mLayers->actionLayer()->isWaitingForAnswer()) 
	    JRenderer::GetInstance()->DrawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ARGB(255,255,0,0));
    if (mExtraPayment) 
    	mExtraPayment->Render();
    
    for (int i = 0; i < nbPlayers; ++i)
    {
        players[i]->Render();
    }
}

void GameObserver::ButtonPressed(PlayGuiObject * target)
{
    DebugTrace("GAMEOBSERVER Click");
    if (CardView* cardview = dynamic_cast<CardView*>(target))
    {
        MTGCardInstance * card = cardview->getCard();
        cardClick(card, card);
    }
    else if (GuiLibrary* library = dynamic_cast<GuiLibrary*>(target))
    {
        if (library->showCards)
        {
            library->toggleDisplay();
            forceShuffleLibraries();
        }
        else
        {
            TargetChooser * _tc = this->getCurrentTargetChooser();
            if (_tc && _tc->targetsZone(library->zone))
            {
                library->toggleDisplay();
                library->zone->needShuffle = true;
            }
        }
    }
    else if (GuiGraveyard* graveyard = dynamic_cast<GuiGraveyard*>(target))
        graveyard->toggleDisplay();
    //opponenthand
    else if (GuiOpponentHand* opponentHand = dynamic_cast<GuiOpponentHand*>(target))
        if (opponentHand->showCards)
        {
            opponentHand->toggleDisplay();
        }
        else
        {
            TargetChooser * _tc = this->getCurrentTargetChooser();
            if (_tc && _tc->targetsZone(opponentHand->zone))
            {
                opponentHand->toggleDisplay();
            }
        }
    //end opponenthand
    else if (GuiAvatar* avatar = dynamic_cast<GuiAvatar*>(target))
    {
        cardClick(NULL, avatar->player);
    }
}

void GameObserver::stackObjectClicked(Interruptible * action)
{
    if (targetChooser != NULL)
    {
        int result = targetChooser->toggleTarget(action);
        if (result == TARGET_OK_FULL)
        {
            cardClick(cardWaitingForTargets);
        }
        else
        {
            return;
        }
    }
    else
    {
        int reaction = mLayers->actionLayer()->isReactingToTargetClick(action);
        if (reaction == -1) 
        	mLayers->actionLayer()->reactToTargetClick(action);
    }
}

bool GameObserver::WaitForExtraPayment(MTGCardInstance * card)
{
    bool result = false;
    if (mExtraPayment)
    {
        if (card)
        {
            mExtraPayment->tryToSetPayment(card);
        }
        if (mExtraPayment->isPaymentSet())
        {
            mLayers->actionLayer()->reactToClick(mExtraPayment->action, mExtraPayment->source);
            mExtraPayment = NULL;
        }
        result = true;
    }

    return result;
}

int GameObserver::cardClick(MTGCardInstance * card, Targetable * object)
{
    Player * clickedPlayer = NULL;
    if (!card) 
    	clickedPlayer = ((Player *) object);
    if (targetChooser)
    {
        int result;
        if (card)
        {
            if (card == cardWaitingForTargets)
            {
                int _result = targetChooser->ForceTargetListReady();
                if (_result)
                {
                    result = TARGET_OK_FULL;
                }
                else
                {
                    result = targetChooser->targetsReadyCheck();
                }
            }
            else
            {
                result = targetChooser->toggleTarget(card);
                WEvent * e = NEW WEventTarget(card,cardWaitingForTargets);
                receiveEvent(e);
            }
        }
        else
        {
            result = targetChooser->toggleTarget(clickedPlayer);
        }
        if (result == TARGET_OK_FULL)
            card = cardWaitingForTargets;
        else
            return 1;
    }

    if (WaitForExtraPayment(card)) 
    	return 1;

    int reaction = 0;

    if (ORDER == combatStep)
    {
        card->defenser->raiseBlockerRankOrder(card);
        return 1;
    }

    if (card)
    {
        //card played as normal, alternative cost, buyback, flashback, retrace.

        //the varible "paymenttype = int" only serves one purpose, to tell this bug fix what menu item you clicked on...
        // all alternative cost or play methods suffered from the fix because if the card contained "target=" 
        // it would automatically force the play method to putinplayrule...even charge you the original mana cost.

        /* Fix for Issue http://code.google.com/p/wagic/issues/detail?id=270
         put into play is hopefully the only ability causing that kind of trouble
         If the same kind of issue occurs with other abilities, let's think of a cleaner solution
         */
        if (targetChooser)
        {
            MTGAbility * a = mLayers->actionLayer()->getAbility(card->paymenttype);
            return a->reactToClick(card);
        }

        reaction = mLayers->actionLayer()->isReactingToClick(card);
        if (reaction == -1) 
        	return mLayers->actionLayer()->reactToClick(card);
    }
    else
    {//this handles abilities on a menu...not just when card is being played
        reaction = mLayers->actionLayer()->isReactingToTargetClick(object);
        if (reaction == -1) 
        	return mLayers->actionLayer()->reactToTargetClick(object);
    }

    if (!card) 
    	return 0;

    //Current player's hand
    if (currentPlayer->game->hand->hasCard(card) && currentGamePhase == Constants::MTG_PHASE_CLEANUP
            && currentPlayer->game->hand->nb_cards > 7 && currentPlayer->nomaxhandsize == false)
    {
        WEvent * e = NEW WEventCardDiscard(currentPlayer->game->hand->cards[0]);
        GameObserver * game = GameObserver::GetInstance();
        game->receiveEvent(e);
        currentPlayer->game->putInGraveyard(card);
    }
    else if (reaction)
    {
        if (reaction == 1)
        {
            return mLayers->actionLayer()->reactToClick(card);
        }
        else
        {
            mLayers->actionLayer()->setMenuObject(object);
            return 1;
        }
    }
    else if (card->isTapped() && card->controller() == currentPlayer)
    {
        return untap(card);
    }

    return 0;

}

int GameObserver::untap(MTGCardInstance * card)
{
    if (!card->isUntapping())
    {
        return 0;
    }
    if (card->has(Constants::DOESNOTUNTAP))
    	return 0;
    if (card->frozen > 0) 
    	return 0;
    card->attemptUntap();
    return 1;
}

TargetChooser * GameObserver::getCurrentTargetChooser()
{
    TargetChooser * _tc = mLayers->actionLayer()->getCurrentTargetChooser();
    if (_tc) 
    	return _tc;
    return targetChooser;
}

/* Returns true if the card is in one of the player's play zone */
int GameObserver::isInPlay(MTGCardInstance * card)
{
    for (int i = 0; i < 2; i++)
    {
        if (players[i]->game->isInPlay(card)) 
        	return 1;
    }
    return 0;
}
int GameObserver::isInGrave(MTGCardInstance * card)
{

    for (int i = 0; i < 2; i++)
    {
        MTGGameZone * graveyard = players[i]->game->graveyard;
        if (players[i]->game->isInZone(card,graveyard)) 
            return 1;
    }
    return 0;
}
int GameObserver::isInExile(MTGCardInstance * card)
{

    for (int i = 0; i < 2; i++)
    {
        MTGGameZone * exile = players[i]->game->exile;
        if (players[i]->game->isInZone(card,exile)) 
            return 1;
    }
    return 0;
}

void GameObserver::cleanupPhase()
{
    currentPlayer->cleanupPhase();
    opponent()->cleanupPhase();
}

void GameObserver::untapPhase()
{
    currentPlayer->inPlay()->untapAll();
}

int GameObserver::receiveEvent(WEvent * e)
{
    if (!e) 
    	return 0;
    eventsQueue.push(e);
    if (eventsQueue.size() > 1) 
    	return -1; //resolving events can generate more events
    int result = 0;
    while (eventsQueue.size())
    {
        WEvent * ev = eventsQueue.front();
        result += mLayers->receiveEvent(ev);
        for (int i = 0; i < 2; ++i)
        {
            result += players[i]->receiveEvent(ev);
        }
        SAFE_DELETE(ev);
        eventsQueue.pop();
    }
    return result;
}

Player * GameObserver::currentlyActing()
{
    if (isInterrupting) 
    	return isInterrupting;
    return currentActionPlayer;
}

//TODO CORRECT THIS MESS
int GameObserver::targetListIsSet(MTGCardInstance * card)
{
    if (targetChooser == NULL)
    {
        TargetChooserFactory tcf;
        targetChooser = tcf.createTargetChooser(card);
        cardWaitingForTargets = card;
        if (targetChooser == NULL)
        {
            return 1;
        }
    }
    return (targetChooser->targetListSet());
}
