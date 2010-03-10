#include "util/bitmap.h"

#include "gui/context-box.h"
#include "util/font.h"

static const double FONT_SPACER = 1.3;
static const int GradientMax = 50;

static int selectedGradientStart(){
    static int color = Bitmap::makeColor(19, 167, 168);
    return color;
}

static int selectedGradientEnd(){
    static int color = Bitmap::makeColor(27, 237, 239);
    return color;
}

using namespace std;
using namespace Gui;

ContextItem::ContextItem(){
}
ContextItem::~ContextItem(){
}
const bool ContextItem::isAdjustable(){
    return false;
}
const int ContextItem::getLeftColor(){
    return 0;
}
const int ContextItem::getRightColor(){
    return 0;
}

ContextBox::ContextBox():
current(0),
fadeState(NotActive),
fontWidth(0),
fontHeight(0),
fadeSpeed(12),
fadeAlpha(0),
cursorCenter(0),
cursorLocation(0),
scrollWait(4),
selectedGradient(GradientMax, selectedGradientStart(), selectedGradientEnd()){
}
ContextBox::ContextBox( const ContextBox & copy ):
current(0),
fadeState(NotActive),
selectedGradient(GradientMax, selectedGradientStart(), selectedGradientEnd()){
    this->context = copy.context;
    this->font = copy.font;
    this->fontWidth = copy.fontWidth;
    this->fontHeight = copy.fontHeight;
    this->fadeSpeed = copy.fadeSpeed;
    this->fadeAlpha = copy.fadeAlpha;
    this->cursorCenter = copy.cursorCenter;
    this->cursorLocation = copy.cursorLocation;
    this->scrollWait = copy.scrollWait;
}
ContextBox::~ContextBox(){
}
ContextBox & ContextBox::operator=( const ContextBox & copy){
    this->current = 0;
    this->fadeState = NotActive;
    this->context = copy.context;
    this->font = copy.font;
    this->fontWidth = copy.fontWidth;
    this->fontHeight = copy.fontHeight;
    this->fadeSpeed = copy.fadeSpeed;
    this->fadeAlpha = copy.fadeAlpha;
    this->cursorCenter = copy.cursorCenter;
    this->cursorLocation = copy.cursorLocation;
    this->scrollWait = copy.scrollWait;
    return *this;
}

void ContextBox::act(){
    // do fade
    doFade();

    // Calculate text info
    calculateText();
    
    // Update gradient
    selectedGradient.update();
}

void ContextBox::render(const Bitmap & work){
    board.render(work);
    drawText(work);
}

bool ContextBox::next(){
    if (fadeState == FadeOutText || fadeState == FadeOutBox){
	return false;
    }
    const Font & vFont = Font::getFont(font, fontWidth, fontHeight);
    cursorLocation += (int)(vFont.getHeight()/FONT_SPACER);
    if (current < context.size()-1){
        current++;
    } else {
        current = 0;
    }
    return true;
}
bool ContextBox::previous(){
    if (fadeState == FadeOutText || fadeState == FadeOutBox){
	return false;
    }
    const Font & vFont = Font::getFont(font, fontWidth, fontHeight);
    cursorLocation -= (int)(vFont.getHeight()/FONT_SPACER);
    if (current > 0){
        current--;
    } else {
        current = context.size()-1;
    }
    return true;
}
void ContextBox::adjustLeft(){
}
void ContextBox::adjustRight(){
}
void ContextBox::open(){
    // Set the fade stuff
    fadeState = FadeInBox;
    board.position = position;
    board.position.width = board.position.height = 0;
    board.position.x = position.x+(position.width/2);
    board.position.y = position.y+(position.height/2);
    board.position.borderAlpha = board.position.bodyAlpha = 0;
    fadeAlpha = 0;
    cursorLocation = 0;
}

void ContextBox::close(){
    fadeState = FadeOutText;
    fadeAlpha = 255;
    cursorLocation = 480;
}


void ContextBox::doFade(){
    switch ( fadeState ){
	case FadeInBox: {
	    if (board.position.x > position.x){
		board.position.x -= fadeSpeed;
	    } else if ( board.position.x < position.x ){
		board.position.x = position.x;
	    }

	    if (board.position.y > position.y){
		board.position.y -= fadeSpeed;
	    } else if (board.position.y < position.y){
		board.position.y = position.y;
	    }

	    if (board.position.width < position.width){
		board.position.width += (fadeSpeed*2);
	    } else if (board.position.width > position.width){
		board.position.width = position.width;
	    }

	    if (board.position.height < position.height){
		board.position.height += (fadeSpeed*2);
	    } else if (board.position.height > position.height){
		board.position.height = position.height;
	    }
	    
	    if (board.position.borderAlpha < position.borderAlpha){
                board.position.borderAlpha += (int)(fadeSpeed/2);
                if (board.position.borderAlpha >= position.borderAlpha){
                    board.position.borderAlpha = position.borderAlpha;
                }
            }
            if (board.position.bodyAlpha < position.bodyAlpha){
                board.position.bodyAlpha += (int)(fadeSpeed/2);
                if (board.position.bodyAlpha >= position.bodyAlpha){
                    board.position.bodyAlpha = position.bodyAlpha;
                }
            }

	    if (board.position == position){
		fadeState = FadeInText;
                fadeAlpha = 0;
	    }

	    break;
	}
	case FadeInText: {
	    if (fadeAlpha < 255){
		fadeAlpha += (fadeSpeed+2);
	    }

	    if (fadeAlpha >= 255){
		fadeAlpha = 255;
		fadeState = Active;
	    }
	    if (board.position.borderAlpha < position.borderAlpha){
                board.position.borderAlpha += (int)(fadeSpeed/4);
                if (board.position.borderAlpha >= position.borderAlpha){
                    board.position.borderAlpha = position.borderAlpha;
                }
            }
            if (board.position.bodyAlpha < position.bodyAlpha){
                board.position.bodyAlpha += (int)(fadeSpeed/4);
                if (board.position.bodyAlpha >= position.bodyAlpha){
                    board.position.bodyAlpha = position.bodyAlpha;
                }
            }
	    break;
	}
	case FadeOutText: {
	    if (fadeAlpha > 0){
		fadeAlpha -= (fadeSpeed+2);
	    }

	    if (fadeAlpha <= 0){
		fadeAlpha = 0;
		fadeState = FadeOutBox;
	    }
	    if (board.position.borderAlpha > 0){
                board.position.borderAlpha -= (int)(fadeSpeed/4);
                if (board.position.borderAlpha <= 0){
                    board.position.borderAlpha = 0;
                }
            }
            if (board.position.bodyAlpha < 0){
                board.position.bodyAlpha -= (int)(fadeSpeed/4);
                if (board.position.bodyAlpha <= 0){
                    board.position.bodyAlpha = 0;
                }
            }
	    break;
	}
	case FadeOutBox: {
	    const int positionX = position.x+(position.width/2);
	    const int positionY = position.y+(position.height/2);
	    if (board.position.x < positionX){
		board.position.x += fadeSpeed;
	    } else if ( board.position.x > positionX ){
		board.position.x = positionX;
	    }

	    if (board.position.y < positionY){
		board.position.y += fadeSpeed;
	    } else if (board.position.y < positionY){
		board.position.y = positionY;
	    }

	    if (board.position.width > 0){
		board.position.width -= (fadeSpeed*2);
	    } else if (board.position.width < 0){
		board.position.width = 0;
	    }

	    if (board.position.height > 0){
		board.position.height -= (fadeSpeed*2);
	    } else if (board.position.height < 0){
		board.position.height = 0;
	    }

	    if (board.position == RectArea()){
		fadeState = NotActive;
	    }
	    
	    if (board.position.borderAlpha > 0){
                board.position.borderAlpha -= (int)(fadeSpeed/2);
                if (board.position.borderAlpha <= 0){
                    board.position.borderAlpha = 0;
                }
            }
            if (board.position.bodyAlpha > 0){
                board.position.bodyAlpha -= (int)(fadeSpeed/2);
                if (board.position.bodyAlpha <= 0){
                    board.position.bodyAlpha = 0;
                }
            }

	    break;
	}
	case Active:
	case NotActive:
	default:
	    break;
    }
}

void ContextBox::calculateText(){
    if (context.empty()){
        return;
    } 
    
    const Font & vFont = Font::getFont(font, fontWidth, fontHeight);
    
    cursorCenter = (position.y + (int)position.height/2) - vFont.getHeight()/2;
    
    if (cursorLocation == cursorCenter){
	    scrollWait = 4;
    } else {
	if (scrollWait <= 0){
	    cursorLocation = (cursorLocation + cursorCenter)/2;
	    scrollWait = 4;
	} else {
	    scrollWait--;
	}
    }
}

void ContextBox::drawText(const Bitmap & bmp){
    if (context.empty()){
        return;
    }
    const Font & vFont = Font::getFont(font, fontWidth, fontHeight);
    bmp.setClipRect(board.position.x+2, board.position.y+2, board.position.getX2()-2, board.position.getY2()-2);
    int locationY = cursorLocation;
    int currentOption = current;
    int count = 0;
    while (locationY < position.getX2() + vFont.getHeight()){
        const int startx = (position.width/2)-(vFont.textLength(context[currentOption]->getName().c_str())/2);
        if (count == 0){
            Bitmap::transBlender(0, 0, 0, fadeAlpha);
            Bitmap::drawingMode( Bitmap::MODE_TRANS );
            const int color = selectedGradient.current();
            vFont.printf(position.x + startx, locationY, color, bmp, context[currentOption]->getName(), 0 );
            if (context[currentOption]->isAdjustable()){
                const int triangleSize = 10;
                int cx = (position.x + startx) - 15;
                int cy = (int)(locationY + (vFont.getHeight()/FONT_SPACER) / 2 + 2);
                bmp.triangle( cx + triangleSize / 2, cy - triangleSize / 2, cx - triangleSize, cy, cx + triangleSize / 2, cy + triangleSize / 2, context[currentOption]->getLeftColor() );

                cx = (position.x+startx + vFont.textLength(context[currentOption]->getName().c_str()))+15;
                bmp.triangle( cx - triangleSize / 2, cy - triangleSize / 2, cx + triangleSize, cy, cx - triangleSize / 2, cy + triangleSize / 2, context[currentOption]->getRightColor() );
            }
            Bitmap::drawingMode(Bitmap::MODE_SOLID);
        } else {
            int textAlpha = fadeAlpha - (count * 35);
            if (textAlpha < 0){
                textAlpha = 0;
            }
            Bitmap::transBlender(0, 0, 0, textAlpha);
            Bitmap::drawingMode( Bitmap::MODE_TRANS );
            const int color = Bitmap::makeColor(255,255,255);
            vFont.printf(position.x + startx, locationY, color, bmp, context[currentOption]->getName(), 0 );
            Bitmap::drawingMode( Bitmap::MODE_SOLID );
        }
        if (context.size() == 1){
            bmp.setClipRect(0, 0, bmp.getWidth(), bmp.getHeight());
            return;
        }
        currentOption++;
        if (currentOption == (int)context.size()){
            currentOption = 0;
        }
        locationY += (int)(vFont.getHeight()/FONT_SPACER);
        count++;
        /*if (context.size() < 2 && count == 2){
            break;
        }*/
    }
    locationY = cursorLocation - (int)(vFont.getHeight()/FONT_SPACER);
    currentOption = current;
    currentOption--;
    count = 0;
    while (locationY > position.x - vFont.getHeight()){
        if (currentOption < 0){
            currentOption = context.size()-1;
        }
        const int startx = (position.width/2)-(vFont.textLength(context[currentOption]->getName().c_str())/2);
        int textAlpha = fadeAlpha - (count * 35);
        if (textAlpha < 0){
            textAlpha = 0;
        }
        Bitmap::transBlender(0, 0, 0, textAlpha);
        Bitmap::drawingMode( Bitmap::MODE_TRANS );
        const int color = Bitmap::makeColor(255,255,255);
        vFont.printf(position.x + startx, locationY, color, bmp, context[currentOption]->getName(), 0 );
        Bitmap::drawingMode( Bitmap::MODE_SOLID );
        currentOption--;
        locationY -= (int)(vFont.getHeight()/FONT_SPACER);
        count++;
        /*if (context.size() < 2 && count == 1){
            break;
        }*/
    }
    bmp.setClipRect(0, 0, bmp.getWidth(), bmp.getHeight());
}

