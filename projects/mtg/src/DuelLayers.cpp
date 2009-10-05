#include "../include/config.h"
#include "../include/MTGRules.h"
#include "../include/GuiCombat.h"
#include "../include/GuiBackground.h"
#include "../include/GuiFrame.h"
#include "../include/GuiPhaseBar.h"
#include "../include/GuiAvatars.h"
#include "../include/GuiHand.h"
#include "../include/GuiPlay.h"
#include "../include/GuiMana.h"
#include "../include/Trash.h"
#include "../include/DuelLayers.h"

void DuelLayers::init(){

  GameObserver* go = GameObserver::GetInstance();

  cs = NEW CardSelector(this);
  //1 Action Layer
  action = NEW ActionLayer();
  action->Add(NEW MTGGamePhase(action->getMaxId()));
  //Add Magic Specific Rules
  action->Add(NEW MTGPutInPlayRule(-1));
  action->Add(NEW MTGAttackRule(-1));
  action->Add(NEW MTGBlockRule(-1));
  action->Add(NEW MTGLegendRule(-1));
  action->Add(NEW MTGPersistRule(-1));
  action->Add(NEW MTGLifelinkRule(-1));
  action->Add(NEW MTGDeathtouchRule(-1));
  //Other display elements
  action->Add(NEW HUDDisplay(-1));

  Add(NEW GuiMana(20,20,go->players[1]));
  Add(NEW GuiMana(440,20,go->players[0]));
  Add(stack = NEW ActionStack(go));
  Add(combat = NEW GuiCombat(go));
  Add(action);
  Add(cs);
  Add(hand = NEW GuiHandSelf(cs, go->players[0]->game->hand));
  Add(avatars = NEW GuiAvatars(cs));
  Add(NEW GuiHandOpponent(cs, go->players[1]->game->hand));
  Add(NEW GuiPlay(go, cs));
  Add(NEW GuiPhaseBar());
  Add(NEW GuiFrame());
  Add(NEW GuiBackground());
}

void DuelLayers::CheckUserInput(int isAI){
  u32 key;
  while ((key = JGE::GetInstance()->ReadButton())){
    if ((!isAI) && (0 != key))
      {
	if (stack->CheckUserInput(key)) break;
        if (combat->CheckUserInput(key)) break;
	if (action->CheckUserInput(key)) break;
  if (avatars->CheckUserInput(key)) break;
	if (hand->CheckUserInput(key)) break;
	if (cs->CheckUserInput(key)) break;
      }
  }
}

void DuelLayers::Update(float dt, Player * currentPlayer)
{
  for (int i = 0; i < nbitems; ++i) objects[i]->Update(dt);
  int isAI = currentPlayer->isAI();
  if (isAI) currentPlayer->Act(dt);
  CheckUserInput(isAI);
}

ActionStack * DuelLayers::stackLayer(){
  return stack;
}

GuiCombat * DuelLayers::combatLayer(){
  return combat;
}

ActionLayer * DuelLayers::actionLayer(){
  return action;
}

DuelLayers::DuelLayers() : nbitems(0) {}

DuelLayers::~DuelLayers(){
  for (int i = 0; i < nbitems; ++i) delete objects[i];

  for (size_t i = 0; i < waiters.size(); ++i)
    delete(waiters[i]);
  Trash::cleanup();
}

int DuelLayers::unstoppableRenderInProgress(){
  for (int i = 0; i < nbitems; ++i)
    if (objects[i]->unstoppableRenderInProgress())
      return 1;
  return 0;
}

void DuelLayers::Add(GuiLayer * layer){
  objects.push_back(layer);
  nbitems++;
}

void DuelLayers::Remove(){
  --nbitems;
}

void DuelLayers::Render(){
  bool focusMakesItThrough = true;
  for (int i = 0; i < nbitems; ++i)
    {
      objects[i]->hasFocus = focusMakesItThrough;
      if (objects[i]->modal) focusMakesItThrough = false;
    }
  for (int i = nbitems - 1; i >= 0; --i)
    objects[i]->Render();
}

int DuelLayers::receiveEvent(WEvent * e){

#if 0
#define PRINT_IF(type) { type *foo = dynamic_cast<type*>(e); if (foo) cout << "Is a " #type " " << *foo << endl; }
  cout << "Received event " << e << " ";
  PRINT_IF(WEventZoneChange);
  PRINT_IF(WEventDamage);
  PRINT_IF(WEventPhaseChange);
  PRINT_IF(WEventCardUpdate);
  PRINT_IF(WEventCardTap);
  PRINT_IF(WEventCreatureAttacker);
  PRINT_IF(WEventCreatureBlocker);
  PRINT_IF(WEventCreatureBlockerRank);
  PRINT_IF(WEventCombatStepChange);
  PRINT_IF(WEventEngageMana);
  PRINT_IF(WEventConsumeMana);
  PRINT_IF(WEventEmptyManaPool);
#endif

  int used = 0;
  for (int i = 0; i < nbitems; ++i)
    used |= objects[i]->receiveEventPlus(e);
  if (!used)
    {
      Pos* p;
      if (WEventZoneChange *event = dynamic_cast<WEventZoneChange*>(e))
	{
          MTGCardInstance* card = event->card;
	  if (card->view)
	    waiters.push_back(p = NEW Pos(*(card->view)));
	  else
	    waiters.push_back(p = NEW Pos(0, 0, 0, 0, 255));
          const Pos* ref = card->view;
          while (card)
            {
              if (ref == card->view) card->view = p;
              card = card->next;
            }
	}
    }
  for (int i = 0; i < nbitems; ++i)
    objects[i]->receiveEventMinus(e);

  if (WEventPhaseChange *event = dynamic_cast<WEventPhaseChange*>(e))
    if (Constants::MTG_PHASE_BEFORE_BEGIN == event->to->id)
      Trash::cleanup();

  return 1;
}

float DuelLayers::RightBoundary()
{
  return MIN (hand->LeftBoundary(), avatars->LeftBoundarySelf());
}
