#ifndef _WEVENT_H_
#define _WEVENT_H_

class MTGCardInstance;
class MTGGameZone;
class Damage;
class Phase;

class WEvent{
public:
  enum{
    CHANGE_ZONE = 1,
    DAMAGE = 2,
    CHANGE_PHASE = 3,
  };
  int type;
  WEvent(int _type);
  virtual ~WEvent() {};
};

class WEventZoneChange: public WEvent{
public:
  MTGCardInstance * card;
  MTGGameZone * from;
  MTGGameZone * to;
  WEventZoneChange(MTGCardInstance * _card, MTGGameZone * _from, MTGGameZone *_to);
  virtual ~WEventZoneChange() {};
};


class WEventDamage: public WEvent{
public:
  Damage * damage;
  WEventDamage(Damage * _damage);
};

class WEventPhaseChange: public WEvent{
public:
  Phase * from;
  Phase * to;
  WEventPhaseChange(Phase * _from, Phase * _to);
};

#endif
