#include "../include/TextScroller.h"
#include "../include/utils.h"
#include <JLBFont.h>

TextScroller::TextScroller(JLBFont * font, float x, float y, float width, float speed):JGuiObject(0){
  mFont = font;
  mWidth = width;
  mSpeed = speed;
  mX = x;
  mY = y;
  start = -width;
  timer = 0;
  currentId = 0;
  mRandom = 0;
}

void TextScroller::setRandom(int mode){
  mRandom = mode;
  if (mRandom && strings.size()){
    currentId = (rand() % strings.size());
    mText = strings[currentId];
  }
}

void TextScroller::Add(string text){
  if (!strings.size()) mText = text;
  strings.push_back(text);
}

void TextScroller::Reset(){
  strings.clear();
}

void TextScroller::Update(float dt){
  if(!strings.size())
    return;
  start+=mSpeed*dt;
  if (start > mFont->GetStringWidth(mText.c_str())){
    start = -mWidth;
    if (mRandom){
      currentId = (rand() % strings.size());
    }else{
      currentId++;
      if (currentId > strings.size()-1)currentId = 0;
    }
    mText = strings[currentId];
  }

}

void TextScroller::Render(){
  mFont->DrawString(mText.c_str(),mX,mY,JGETEXT_LEFT,start,mWidth);
}

ostream& TextScroller::toString(ostream& out) const
{
  return out << "TextScroller ::: mText : " << mText
	     << " ; tempText : " << tempText
	     << " ; mFont : " << mFont
	     << " ; mWidth : " << mWidth
	     << " ; mSpeed : " << mSpeed
	     << " ; mX,mY : " << mX << "," << mY
	     << " ; start : " << start
	     << " ; timer : " << timer
	     << " ; strings : ?" // << strings
	     << " ; currentId : " << currentId
	     << " ; mRandom : " << mRandom;
}
