#include "util/bitmap.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <string>
#include <cstring>
#include <vector>
#include <ostream>
#include <sstream>
#include <iostream>

// To aid in filesize
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "util/funcs.h"
#include "util/font.h"
#include "util/file-system.h"
#include "factory/font_render.h"

#include "animation.h"
#include "item.h"
#include "item-content.h"
#include "section.h"
#include "character.h"
#include "sound.h"
#include "reader.h"
#include "sprite.h"
#include "util.h"
#include "stage.h"
#include "globals.h"
#include "state.h"
#include "evaluator.h"
#include "compiler.h"
#include "command.h"
#include "behavior.h"

#include "input/input-map.h"
#include "input/input-manager.h"

#include "parse-cache.h"
#include "parser/all.h"
#include "ast/all.h"

using namespace std;

static const int REGENERATE_TIME = 40;

namespace Mugen{

namespace StateType{

std::string Stand = "S";
std::string Crouch = "C";
std::string Air = "A";
std::string LyingDown = "L";

}

namespace Move{

std::string Attack = "A";
std::string Idle = "I";
std::string Hit = "H";

}

namespace AttackType{
    std::string Normal = "N";
    std::string Special = "S";
    std::string Hyper = "H";
}

namespace PhysicalAttack{
    std::string Normal = "A";
    std::string Throw = "T";
    std::string Projectile = "P";
}

namespace PaintownUtil = ::Util;

HitDefinition::~HitDefinition(){
    /*
    delete groundSlideTime;
    delete player1SpritePriority;
    delete player1Facing;
    delete player2Facing;
    delete player1State;
    delete player2State;
    */
}

HitDefinition::Damage::~Damage(){
    /*
    delete damage;
    delete guardDamage;
    */
}

HitDefinition::Fall::~Fall(){
    // delete fall;
}

StateController::CompiledController::CompiledController(){
}

StateController::CompiledController::~CompiledController(){
}

StateController::StateController(const string & name):
type(Unknown),
compiled(NULL),
name(name),
changeControl(false),
control(NULL),
x(NULL),
y(NULL),
value(NULL),
variable(NULL),
posX(NULL),
posY(NULL),
time(NULL),
animation(30),
changeMoveType(false),
changeStateType(false),
changePhysics(false),
internal(NULL),
debug(false){
}

StateController::StateController(const string & name, Ast::Section * section):
compiled(NULL),
name(name),
changeControl(false),
control(NULL),
x(NULL),
y(NULL),
value(NULL),
variable(NULL),
posX(NULL),
posY(NULL),
time(NULL),
animation(30),
changeMoveType(false),
changeStateType(false),
changePhysics(false),
internal(NULL),
debug(false){
    class Walker: public Ast::Walker {
    public:
        Walker(StateController & controller):
            controller(controller){
            }

        StateController & controller;

        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
            if (simple == "triggerall"){
                controller.addTriggerAll(Compiler::compile(simple.getValue()));
            } else if (PaintownUtil::matchRegex(PaintownUtil::lowerCaseAll(simple.idString()), "trigger[0-9]+")){
                int trigger = atoi(PaintownUtil::captureRegex(PaintownUtil::lowerCaseAll(simple.idString()), "trigger([0-9]+)", 0).c_str());
                controller.addTrigger(trigger, Compiler::compile(simple.getValue()));
            }
        }
    };
    Walker walker(*this);
    section->walk(walker);
}

StateController::~StateController(){
    for (map<int, vector<Compiler::Value*> >::iterator it = triggers.begin(); it != triggers.end(); it++){
        vector<Compiler::Value*> values = (*it).second;
        for (vector<Compiler::Value*>::iterator value_it = values.begin(); value_it != values.end(); value_it++){
            Compiler::Value * value = *value_it;
            delete value;
        }
    }
    
    for (map<int, Compiler::Value*>::iterator it = variables.begin(); it != variables.end(); it++){
        Compiler::Value * value = (*it).second;
        delete value;
    }

    for (map<int, Compiler::Value*>::iterator it = floatVariables.begin(); it != floatVariables.end(); it++){
        Compiler::Value * value = (*it).second;
        delete value;
    }

    for (map<int, Compiler::Value*>::iterator it = systemVariables.begin(); it != systemVariables.end(); it++){
        Compiler::Value * value = (*it).second;
        delete value;
    }

    delete x;
    delete y;
    delete control;
    // delete value;
    delete compiled;
    delete variable;
    delete posX;
    delete posY;
    delete time;
}

/*
void StateController::setValue1(Compiler::Value * value){
    this->value1 = value;
}

void StateController::setValue2(Compiler::Value * value){
    this->value2 = value;
}
*/

void StateController::setX(Compiler::Value * value){
    this->x = value;
}

void StateController::setY(Compiler::Value * value){
    this->y = value;
}

void StateController::setValue(const Ast::Value * value){
    this->value = value;
}

void StateController::setVariable(Compiler::Value * value){
    this->variable = value;
}

void StateController::addTriggerAll(Compiler::Value * trigger){
    triggers[-1].push_back(trigger);
}

void StateController::addTrigger(int number, Compiler::Value * trigger){
    triggers[number].push_back(trigger);
}
    
void StateController::addVariable(int number, Compiler::Value * variable){
    if (variables[number] != 0){
        delete variables[number];
    }
    variables[number] = variable;
}

void StateController::addFloatVariable(int number, Compiler::Value * variable){
    if (floatVariables[number] != 0){
        delete floatVariables[number];
    }
    floatVariables[number] = variable;
}

void StateController::addSystemVariable(int number, Compiler::Value * variable){
    if (systemVariables[number] != 0){
        delete systemVariables[number];
    }

    systemVariables[number] = variable;
}

bool StateController::canTrigger(const MugenStage & stage, const Character & character, const Compiler::Value * expression, const vector<string> & commands) const {
    /* this makes it easy to break in gdb */
    try{
        if (debug){
            int x = 2;
            x += 1;
        }
        RuntimeValue result = expression->evaluate(FullEnvironment(stage, character, commands));
        return result.toBool();
    } catch (const MugenException & e){
        ostringstream out;
        out << "Expression `" << expression << "' " << e.getReason();
        throw MugenException(out.str());
    }
}

bool StateController::canTrigger(const MugenStage & stage, const Character & character, const vector<Compiler::Value*> & expressions, const vector<string> & commands) const {
    for (vector<Compiler::Value*>::const_iterator it = expressions.begin(); it != expressions.end(); it++){
        const Compiler::Value * value = *it;
        if (!canTrigger(stage, character, value, commands)){
            // Global::debug(2*!getDebug()) << "'" << value->toString() << "' did not trigger" << endl;
            return false;
        } else {
            // Global::debug(2*!getDebug()) << "'" << value->toString() << "' did trigger" << endl;
        }
    }
    return true;
}

vector<int> StateController::sortTriggers() const {
    vector<int> out;

    for (map<int, vector<Compiler::Value*> >::const_iterator it = triggers.begin(); it != triggers.end(); it++){
        int number = it->first;
        /* ignore triggerall (-1) */
        if (number != -1){
            out.push_back(number);
        }
    }

    sort(out.begin(), out.end());

    return out;
}

bool StateController::canTrigger(const MugenStage & stage, const Character & character, const vector<string> & commands) const {
    if (triggers.find(-1) != triggers.end()){
        vector<Compiler::Value*> values = triggers.find(-1)->second;
        /* if the triggerall fails then no triggers will work */
        if (!canTrigger(stage, character, values, commands)){
            return false;
        }
    }

    vector<int> keys = sortTriggers();
    for (vector<int>::iterator it = keys.begin(); it != keys.end(); it++){
        vector<Compiler::Value*> values = triggers.find(*it)->second;
        /* if a trigger succeeds then stop processing and just return true */
        if (canTrigger(stage, character, values, commands)){
            return true;
        }
    }

    return false;
}

#if 0
StateController::CompiledController * StateController::doCompile(){
    switch (getType()){
        case ChangeAnim: {
            class ControllerChangeAnim: public CompiledController {
            public:
                ControllerChangeAnim(Compiler::Value * value):
                    value(value){
                    }

                Compiler::Value * value;

                virtual ~ControllerChangeAnim(){
                    delete value;
                }

                virtual void execute(MugenStage & stage, Character & guy, const vector<string> & commands){
                    RuntimeValue result = value->evaluate(FullEnvironment(stage, guy));
                    if (result.isDouble()){
                        int value = (int) result.getDoubleValue();
                        guy.setAnimation(value);
                    }
                }
            };

            return new ControllerChangeAnim(Compiler::compile(getValue()));
            break;
        }
        case ChangeState : {
            class ControllerChangeState: public CompiledController {
            public:
                ControllerChangeState(const Ast::Value * value){
                    this->value = Compiler::compile(value);
                }

                Compiler::Value * value;

                inline Compiler::Value * getValue(){
                    return value;
                }

                virtual ~ControllerChangeState(){
                    delete value;
                }
                
                virtual void execute(MugenStage & stage, Character & guy, const vector<string> & commands){
                    RuntimeValue result = getValue()->evaluate(FullEnvironment(stage, guy));
                    if (result.isDouble()){
                        int value = (int) result.getDoubleValue();
                        guy.changeState(stage, value, commands);
                    }
                }
            };

            return new ControllerChangeState(getValue());
            break;
        }
        case CtrlSet : {
            class ControllerCtrlSet: public CompiledController {
            public:
                ControllerCtrlSet(const Ast::Value * value){
                    this->value = Compiler::compile(value);
                }

                Compiler::Value * value;

                virtual ~ControllerCtrlSet(){
                    delete value;
                }

                Compiler::Value * getValue(){
                    return value;
                }

                virtual void execute(MugenStage & stage, Character & guy, const vector<string> & commands){
                    RuntimeValue result = getValue()->evaluate(FullEnvironment(stage, guy));
                    guy.setControl(toBool(result));
                }
            };

            return new ControllerCtrlSet(getValue());
            break;
        }
        case PlaySnd : {
            class ControllerPlaySound: public CompiledController {
            public:
                ControllerPlaySound(const Ast::Value * value):
                group(-1),
                own(false),
                item(NULL){
                    parseSound(value);
                }

                int group;
                bool own;
                Compiler::Value * item;

                virtual ~ControllerPlaySound(){
                    delete item;
                }

                virtual void parseSound(const Ast::Value * value){
                    try{
                        string group;
                        const Ast::Value * item;
                        *value >> group >> item;
                        if (PaintownUtil::matchRegex(group, "F[0-9]+")){
                            int realGroup = atoi(PaintownUtil::captureRegex(group, "F([0-9]+)", 0).c_str());
                            this->group = realGroup;
                            this->item = Compiler::compile(item);
                            own = true;
                        } else if (PaintownUtil::matchRegex(group, "[0-9]+")){
                            this->group = atoi(group.c_str());
                            this->item = Compiler::compile(item);
                            own = false;
                        }
                    } catch (const MugenException & e){
                        // Global::debug(0) << "Error with PlaySnd " << controller.name << ": " << e.getReason() << endl;
                        Global::debug(0) << "Error with PlaySnd :" << e.getReason() << endl;
                    }

                }

                virtual void execute(MugenStage & stage, Character & guy, const vector<string> & commands){
                    MugenSound * sound = NULL;
                    if (item != NULL){
                        int itemNumber = (int) item->evaluate(FullEnvironment(stage, guy)).toNumber();
                        if (own){
                            sound = guy.getCommonSound(group, itemNumber);
                        } else {
                            sound = guy.getSound(group, itemNumber);
                        }
                    }

                    if (sound != NULL){
                        sound->play();
                    }
                }
            };

            return new ControllerPlaySound(getValue());
            
            break;
        }
        case VarSet : {
            class ControllerVarSet: public CompiledController {
            public:
                ControllerVarSet(const Ast::Value * value,
                                 const Compiler::Value * variable,
                                 map<int, Compiler::Value*> variables,
                                 map<int, Compiler::Value*> floatVariables,
                                 map<int, Compiler::Value*> systemVariables):
                value(NULL),
                variable(variable),
                variables(variables),
                floatVariables(floatVariables),
                systemVariables(systemVariables){
                    if (value != NULL){
                        this->value = Compiler::compile(value);
                    }
                }

                Compiler::Value * value;
                const Compiler::Value * variable;
                map<int, Compiler::Value*> variables;
                map<int, Compiler::Value*> floatVariables;
                map<int, Compiler::Value*> systemVariables;

                virtual ~ControllerVarSet(){
                    delete value;
                }

                Compiler::Value * getValue(){
                    return value;
                }

                const Compiler::Value * getVariable(){
                    return variable;
                }

                virtual void execute(MugenStage & stage, Character & guy, const vector<string> & commands){
                    for (map<int, Compiler::Value*>::const_iterator it = variables.begin(); it != variables.end(); it++){
                        int index = (*it).first;
                        Compiler::Value * value = (*it).second;
                        guy.setVariable(index, value);
                    }

                    for (map<int, Compiler::Value*>::const_iterator it = floatVariables.begin(); it != floatVariables.end(); it++){
                        int index = (*it).first;
                        Compiler::Value * value = (*it).second;
                        guy.setFloatVariable(index, value);
                    }

                    for (map<int, Compiler::Value*>::const_iterator it = systemVariables.begin(); it != systemVariables.end(); it++){
                        int index = (*it).first;
                        Compiler::Value * value = (*it).second;
                        guy.setSystemVariable(index, value);
                    }

                    if (getValue() != NULL && getVariable() != NULL){
                        /* 'value = 23' is value1
                         * 'v = 9' is value2
                         */
                        guy.setVariable((int) getVariable()->evaluate(FullEnvironment(stage, guy, commands)).toNumber(), getValue());
                    }
                }
            };

            return new ControllerVarSet(getValue(), getVariable(), variables, floatVariables, systemVariables);
            
            break;
        }
        default : {
            class DefaultController: public CompiledController {
            public:
                DefaultController(StateController & controller):
                controller(controller){
                }

                StateController & controller;

                virtual ~DefaultController(){
                }

                virtual Compiler::Value * getX() const {
                    return controller.getX();
                }

                virtual Compiler::Value * getVariable() const {
                    return controller.getVariable();
                }

                virtual Compiler::Value * getY() const {
                    return controller.getY();
                }

                virtual void execute(MugenStage & stage, Character & guy, const vector<string> & commands){
                    switch (controller.getType()){
                        case AfterImage : {
                            break;
                        }
                        case AfterImageTime : {
                            break;
                        }
                        case AllPalFX : {
                            break;
                        }
                        case AngleAdd : {
                            break;
                        }
                        case AngleDraw : {
                            break;
                        }
                        case AngleMul : {
                            break;
                        }
                        case AngleSet : {
                            break;
                        }
                        case AppendToClipboard : {
                            break;
                        }
                        case AssertSpecial : {
                            break;
                        }
                        case AttackDist : {
                            break;
                        }
                        case AttackMulSet : {
                            break;
                        }
                        case BGPalFX : {
                            break;
                        }
                        case BindToParent : {
                            break;
                        }
                        case BindToRoot : {
                            break;
                        }
                        case BindToTarget : {
                            break;
                        }
                        case ChangeAnim2 : {
                            break;
                        }
                        case ClearClipboard : {
                            break;
                        }
                        
                        case DefenceMulSet : {
                            break;
                        }
                        case DestroySelf : {
                            break;
                        }
                        case DisplayToClipboard : {
                            break;
                        }
                        case EnvColor : {
                            break;
                        }
                        case EnvShake : {
                            break;
                        }
                        case Explod : {
                            break;
                        }
                        case ExplodBindTime : {
                            break;
                        }
                        case ForceFeedback : {
                            break;
                        }
                        case FallEnvShake : {
                            break;
                        }
                        case GameMakeAnim : {
                            break;
                        }
                        case Gravity : {
                            break;
                        }
                        case Helper : {
                            break;
                        }
                        case HitAdd : {
                            break;
                        }
                        case HitBy : {
                            break;
                        }
                        case HitDef : {
                            /* prevent the same hitdef from being applied */
                            if (guy.getHit() != &controller.getHit()){
                                guy.setHitDef(&controller.getHit());
                                guy.nextTicket();
                            }
                            break;
                        }
                        case HitFallDamage : {
                            break;
                        }
                        case HitFallSet : {
                            break;
                        }
                        case HitFallVel : {
                            break;
                        }
                        case HitOverride : {
                            break;
                        }
                        case HitVelSet : {
                            if (getX() != NULL){
                                RuntimeValue result = getX()->evaluate(FullEnvironment(stage, guy));
                                if (result.getBoolValue()){
                                    guy.setXVelocity(guy.getHitState().xVelocity);
                                }
                            }

                            if (getY() != NULL){
                                RuntimeValue result = getY()->evaluate(FullEnvironment(stage, guy));
                                if (result.getBoolValue()){
                                    guy.setYVelocity(guy.getHitState().yVelocity);
                                }
                            }
                            break;
                        }
                        case LifeAdd : {
                            break;
                        }
                        case LifeSet : {
                            break;
                        }
                        case MakeDust : {
                            break;
                        }
                        case ModifyExplod : {
                            break;
                        }
                        case MoveHitReset : {
                            break;
                        }
                        case NotHitBy : {
                            break;
                        }
                        case Null : {
                            break;
                        }
                        case Offset : {
                            break;
                        }
                        case PalFX : {
                            break;
                        }
                        case ParentVarAdd : {
                            break;
                        }
                        case ParentVarSet : {
                            break;
                        }
                        case Pause : {
                            break;
                        }
                        case PlayerPush : {
                            break;
                        }
                        
                        case PosAdd : {
                            if (getX() != NULL){
                                RuntimeValue result = getX()->evaluate(FullEnvironment(stage, guy));
                                if (result.isDouble()){
                                    guy.moveX(result.getDoubleValue());
                                    // guy.setX(guy.getX() + result.getDoubleValue());
                                }
                            }
                            if (getY() != NULL){
                                RuntimeValue result = getY()->evaluate(FullEnvironment(stage, guy));
                                if (result.isDouble()){
                                    guy.moveYNoCheck(-result.getDoubleValue());
                                    // guy.setY(guy.getY() + result.getDoubleValue());
                                }
                            }
                            break;
                        }
                        case PosFreeze : {
                            break;
                        }
                        case PosSet : {
                            if (getX() != NULL){
                                RuntimeValue result = getX()->evaluate(FullEnvironment(stage, guy));
                                if (result.isDouble()){
                                    guy.setX(result.getDoubleValue());
                                }
                            }
                            if (getY() != NULL){
                                RuntimeValue result = getY()->evaluate(FullEnvironment(stage, guy));
                                if (result.isDouble()){
                                    guy.setY(result.getDoubleValue());
                                }
                            }
                            break;
                        }
                        case PowerAdd : {
                            break;
                        }
                        case PowerSet : {
                            break;
                        }
                        case Projectile : {
                            break;
                        }
                        case RemoveExplod : {
                            break;
                        }
                        case ReversalDef : {
                            break;
                        }
                        case ScreenBound : {
                            break;
                        }
                        case SelfState : {
                            break;
                        }
                        case SprPriority : {
                            break;
                        }
                        case StateTypeSet : {
                            if (controller.changeMoveType){
                                guy.setMoveType(controller.moveType);
                            }

                            if (controller.changeStateType){
                                guy.setStateType(controller.stateType);
                            }

                            if (controller.changePhysics){
                                guy.setCurrentPhysics(controller.physics);
                            }
                            break;
                        }
                        case SndPan : {
                            break;
                        }
                        case StopSnd : {
                            break;
                        }
                        case SuperPause : {
                            FullEnvironment env(stage, guy);
                            int x = guy.getRX() + (int) controller.posX->evaluate(env).toNumber() * (guy.getFacing() == Object::FACING_LEFT ? -1 : 1);
                            int y = guy.getRY() + (int) controller.posY->evaluate(env).toNumber();
                            /* 30 is the default I think.. */
                            int time = 30;
                            if (controller.time != NULL){
                                time = (int) controller.time->evaluate(env).toNumber();
                            }
                            stage.doSuperPause(time, controller.animation, x, y, controller.sound.group, controller.sound.item); 
                            break;
                        }
                        case TargetBind : {
                            break;
                        }
                        case TargetDrop : {
                            break;
                        }
                        case TargetFacing : {
                            break;
                        }
                        case TargetLifeAdd : {
                            break;
                        }
                        case TargetPowerAdd : {
                            break;
                        }
                        case TargetState : {
                            break;
                        }
                        case TargetVelAdd : {
                            break;
                        }
                        case TargetVelSet : {
                            break;
                        }
                        case Trans : {
                            break;
                        }
                        case Turn : {
                            break;
                        }
                        case VarAdd : {
                            break;
                        }
                        case VarRandom : {
                            break;
                        }
                        case VarRangeSet : {
                            break;
                        }
                        
                        case VelAdd : {
                            if (getX() != NULL){
                                RuntimeValue result = getX()->evaluate(FullEnvironment(stage, guy));
                                if (result.isDouble()){
                                    guy.setXVelocity(guy.getXVelocity() + result.getDoubleValue());
                                }
                            }
                            if (getY() != NULL){
                                RuntimeValue result = getY()->evaluate(FullEnvironment(stage, guy));
                                if (result.isDouble()){
                                    guy.setYVelocity(guy.getYVelocity() + result.getDoubleValue());
                                }
                            }
                            break;
                        }
                        case VelMul : {
                            if (getX() != NULL){
                                RuntimeValue result = getX()->evaluate(FullEnvironment(stage, guy));
                                if (result.isDouble()){
                                    guy.setXVelocity(guy.getXVelocity() * result.getDoubleValue());
                                }
                            }

                            if (getY() != NULL){
                                RuntimeValue result = getY()->evaluate(FullEnvironment(stage, guy));
                                if (result.isDouble()){
                                    guy.setYVelocity(guy.getYVelocity() * result.getDoubleValue());
                                }
                            }
                            break;
                        }
                        case VelSet : {
                            if (getX() != NULL){
                                RuntimeValue result = getX()->evaluate(FullEnvironment(stage, guy));
                                if (result.isDouble()){
                                    guy.setXVelocity(result.getDoubleValue());
                                }
                            }
                            if (getY() != NULL){
                                RuntimeValue result = getY()->evaluate(FullEnvironment(stage, guy));
                                if (result.isDouble()){
                                    guy.setYVelocity(result.getDoubleValue());
                                }
                            }
                            break;
                        }
                        case Width : {
                            break;
                        }
                        case Unknown : {
                            break;
                        }
                        case InternalCommand : {
                            typedef void (Character::*func)(const MugenStage & stage, const vector<string> & inputs);
                            func f = (func) controller.internal;
                            (guy.*f)(stage, commands);
                            break;
                        }
                        default : break;
                    }
                }
            };

            return new DefaultController(*this);

            break;
        }
    }

    return NULL;
}


void StateController::compile(){
    compiled = doCompile();
    if (compiled == NULL){
        ostringstream out;
        out << "Unable to compile state controller for type " << getType();
        throw MugenException(out.str());
    }
}
#endif

/*
void StateController::activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
    Global::debug(1 * !debug) << "Activate controller " << name << endl;

    if (changeControl){
        guy.setControl(control->evaluate(FullEnvironment(stage, guy, commands)).toBool());
    }

    compiled->execute(stage, guy, commands);
}
*/

State::State():
type(Unchanged),
animation(NULL),
changeControl(false),
control(NULL),
changeVelocity(false),
velocity_x(NULL),
velocity_y(NULL),
changePhysics(false),
changePower(false),
powerAdd(NULL),
moveType(Move::Idle),
juggle(0),
hitDefPersist(false){
}

void State::addController(StateController * controller){
    controllers.push_back(controller);
}

void State::addControllerFront(StateController * controller){
    controllers.insert(controllers.begin(), controller);
}

void State::setJuggle(Compiler::Value * juggle){
    this->juggle = juggle;
}

void State::setVelocity(Compiler::Value * x, Compiler::Value * y){
    changeVelocity = true;
    velocity_x = x;
    velocity_y = y;
}

void State::setPower(Compiler::Value * power){
    powerAdd = power;
    changePower = true;
}
    
void State::setMoveType(const std::string & type){
    moveType = type;
}
    
void State::setPhysics(Physics::Type p){
    changePhysics = true;
    physics = p;
}

void State::transitionTo(const MugenStage & stage, Character & who){
    if (animation != NULL){
        who.setAnimation((int) animation->evaluate(FullEnvironment(stage, who)).toNumber());
    }

    if (changeControl){
        who.setControl(control->evaluate(FullEnvironment(stage, who)).toBool());
    }

    if (juggle != NULL){
        who.setCurrentJuggle((int) juggle->evaluate(FullEnvironment(stage, who)).toNumber());
    }

    who.setMoveType(moveType);

    /* I don't think a hit definition should persist across state changes, unless
     * hitdefpersist is set to 1. Anyway this is needed because a state could try
     * to repeatedly set the same hitdef, like:
     *   state 200
     *   type = hitdef
     *   trigger1 = animelem = 3
     * But since the animelem won't change every game tick, this trigger will be
     * activated a few times (like 3-4).
     * Resetting the hitdef to NULL allows the hitdef controller to ensure only
     * unique hitdef's get set as well as repeat the same hitdef in sequence.
     *
     * Update: 7/28/2010: I don't think this is true anymore because `animelem'
     * is only true for the first tick of an animation. `animelem = 3' won't be true
     * for as long as the animation is 3, just the first tick of the game that its 3.
     */
    if (!doesHitDefPersist()){
        who.setHitDef(NULL);
    }

    if (changeVelocity){
        who.setXVelocity(velocity_x->evaluate(FullEnvironment(stage, who)).toNumber());
        who.setYVelocity(velocity_y->evaluate(FullEnvironment(stage, who)).toNumber());
        // Global::debug(0) << "Velocity set to " << velocity_x->evaluate(FullEnvironment(stage, who)).toNumber() << ", " << velocity_y->evaluate(FullEnvironment(stage, who)).toNumber() << endl;
    }

    if (changePhysics){
        who.setCurrentPhysics(physics);
    }

    switch (type){
        case Standing : {
            who.setStateType(StateType::Stand);
            break;
        }
        case Crouching : {
            who.setStateType(StateType::Crouch);
            break;
        }
        case Air : {
            who.setStateType(StateType::Air);
            break;
        }
        case LyingDown : {
            who.setStateType(StateType::LyingDown);
            break;
        }
        case Unchanged : {
            break;
        }
    }
}

State::~State(){
    for (vector<StateController*>::iterator it = controllers.begin(); it != controllers.end(); it++){
        delete (*it);
    }

    delete powerAdd;
    delete animation;
    delete control;
    delete juggle;
    delete velocity_x;
    delete velocity_y;
}

/* Called when the player was hit */
void HitState::update(MugenStage & stage, const Character & guy, bool inAir, const HitDefinition & hit){
    /* FIXME: choose the proper ground/air/guard types */

    guarded = false;
    shakeTime = hit.pause.player2;
    groundType = hit.groundType;
    airType = hit.airType;
    yAcceleration = hit.yAcceleration;

    /* FIXME: set damage */
    
    /* if in the air */
    if (inAir){
        if (fall.fall){
            if (hit.animationTypeFall == AttackType::NoAnimation){
                if (hit.animationTypeAir == AttackType::Up){
                    animationType = AttackType::Up;
                } else {
                    animationType = AttackType::Back;
                }
            } else {
                animationType = hit.animationTypeFall;
            }

            hitTime = 0;
        } else {
            if (hit.animationTypeAir != AttackType::NoAnimation){
                animationType = hit.animationTypeAir;
            } else {
                animationType = hit.animationType;
            }

            hitTime = hit.airHitTime;
        }
        
        xVelocity = hit.airVelocity.x;
        yVelocity = hit.airVelocity.y;

        if (hit.fall.fall != NULL){
            fall.fall |= hit.fall.fall->evaluate(FullEnvironment(stage, guy)).toBool();
        }
        fall.yVelocity = hit.fall.yVelocity;
        int groundSlideTime = 0;
        if (hit.groundSlideTime != NULL){
            groundSlideTime = (int) hit.groundSlideTime->evaluate(FullEnvironment(stage, guy)).toNumber();
        }
        returnControlTime = hit.airGuardControlTime == 0 ? groundSlideTime : hit.airGuardControlTime;
    } else {
        int groundSlideTime = 0;
        if (hit.groundSlideTime != NULL){
            groundSlideTime = (int) hit.groundSlideTime->evaluate(FullEnvironment(stage, guy)).toNumber();
        }
        animationType = hit.animationType;
        returnControlTime = hit.guardControlTime == 0 ? groundSlideTime : hit.guardControlTime;
        hitTime = hit.groundHitTime;
        slideTime = groundSlideTime;
        xVelocity = hit.groundVelocity.x;
        yVelocity = hit.groundVelocity.y;
        fall.fall = false;
        if (hit.fall.fall != NULL){
            fall.fall = hit.fall.fall->evaluate(FullEnvironment(stage, guy)).toBool();
        }

        fall.yVelocity = hit.fall.yVelocity;
    }

    // Global::debug(0) << "Hit definition: shake time " << shakeTime << " hit time " << hitTime << endl;
}

Character::Character(const Filesystem::AbsolutePath & s ):
ObjectAttack(0),
commonSounds(NULL),
hit(NULL){
    this->location = s;
    initialize();
}

/*
Character::Character( const char * location ):
ObjectAttack(0),
commonSounds(NULL),
hit(NULL){
    this->location = std::string(location);
    initialize();
}
*/

Character::Character( const Filesystem::AbsolutePath & s, int alliance ):
ObjectAttack(alliance),
commonSounds(NULL),
hit(NULL){
    this->location = s;
    initialize();
}

Character::Character( const Filesystem::AbsolutePath & s, const int x, const int y, int alliance ):
ObjectAttack(x,y,alliance),
commonSounds(NULL),
hit(NULL){
    this->location = s;
    initialize();
}

Character::Character( const Character & copy ):
ObjectAttack(copy),
commonSounds(NULL),
hit(NULL){
}

Character::~Character(){
     // Get rid of sprites
    for (std::map< unsigned int, std::map< unsigned int, MugenSprite * > >::iterator i = sprites.begin() ; i != sprites.end() ; ++i ){
      for( std::map< unsigned int, MugenSprite * >::iterator j = i->second.begin() ; j != i->second.end() ; ++j ){
	  if( j->second )delete j->second;
      }
    }
    
     // Get rid of bitmaps
    for( std::map< unsigned int, std::map< unsigned int, Bitmap * > >::iterator i = bitmaps.begin() ; i != bitmaps.end() ; ++i ){
      for( std::map< unsigned int, Bitmap * >::iterator j = i->second.begin() ; j != i->second.end() ; ++j ){
	  if( j->second )delete j->second;
      }
    }
    
    // Get rid of animation lists;
    for( std::map< int, MugenAnimation * >::iterator i = animations.begin() ; i != animations.end() ; ++i ){
	if( i->second )delete i->second;
    }
    
    // Get rid of sounds
    for( std::map< unsigned int, std::map< unsigned int, MugenSound * > >::iterator i = sounds.begin() ; i != sounds.end() ; ++i ){
      for( std::map< unsigned int, MugenSound * >::iterator j = i->second.begin() ; j != i->second.end() ; ++j ){
	  if( j->second )delete j->second;
      }
    }

    for (vector<Command*>::iterator it = commands.begin(); it != commands.end(); it++){
        delete (*it);
    }
        
    for (map<int, State*>::iterator it = states.begin(); it != states.end(); it++){
        State * state = (*it).second;
        delete state;
    }

    delete internalJumpNumber;
}

void Character::initialize(){
    currentState = Standing;
    moveType = Move::Idle;
    previousState = currentState;
    stateType = StateType::Stand;
    currentAnimation = Standing;
    lieDownTime = 0;
    debug = false;
    has_control = true;
    airjumpnum = 0;
    airjumpheight = 35;
    blocking = false;
    guarding = false;
    behavior = NULL;

    sparkno = 0;
    guardsparkno = 0;

    needToGuard = false;

    matchWins = 0;

    combo = 1;
    // nextCombo = 0;

    lastTicket = 0;

    internalJumpNumber = NULL;
    
    /* Load up info for the select screen */
    //loadSelectData();

    /* provide sensible defaults */
    walkfwd = 0;
    walkback = 0;
    runbackx = 0;
    runbacky = 0;
    runforwardx = 0;
    runforwardy = 0;
    power = 0;

    velocity_x = 0;
    velocity_y = 0;

    gravity = 0.1;
    standFriction = 1;

    stateTime = 0;

    /* Regeneration */
    regenerateHealth = false;
    regenerating = false;
    regenerateTime = REGENERATE_TIME;
    regenerateHealthDifference = 0;
}

void Character::loadSelectData(){
    /* Load up info for the select screen */
    try{
        Filesystem::AbsolutePath baseDir = location.getDirectory();
	Global::debug(1) << baseDir.path() << endl;
        Filesystem::RelativePath str = Filesystem::RelativePath(location.getFilename().path());
	const Filesystem::AbsolutePath ourDefFile = Util::fixFileName(baseDir, str.path() + ".def");
	
	if (ourDefFile.isEmpty()){
	    Global::debug(1) << "Cannot locate player definition file for: " << location.path() << endl;
	}
	
        Ast::AstParse parsed(Mugen::Util::parseDef(ourDefFile.path()));
	// Set name of character
	this->name = Mugen::Util::probeDef(parsed, "info", "name");
	this->displayName = Mugen::Util::probeDef(parsed, "info", "displayname");
	this->sffFile = Mugen::Util::probeDef(parsed, "files", "sprite");
	// Get necessary sprites 9000 & 9001 for select screen
        /* FIXME: replace 9000 with some readable constant */
	this->sprites[9000][0] = Mugen::Util::probeSff(Util::fixFileName(baseDir, this->sffFile), 9000,0);
	this->sprites[9000][1] = Mugen::Util::probeSff(Util::fixFileName(baseDir, this->sffFile), 9000,1);
	
    } catch (const MugenException &ex){
	Global::debug(1) << "Couldn't grab details for character!" << endl;
	Global::debug(1) << "Error was: " << ex.getReason() << endl;
    }
}
    
void Character::addCommand(Command * command){
    commands.push_back(command);
}

void Character::setAnimation(int animation){
    currentAnimation = animation;
    getCurrentAnimation()->reset();
}

void Character::loadCmdFile(const Filesystem::RelativePath & path){
    Filesystem::AbsolutePath full = baseDir.join(path);
    try{
        int defaultTime = 15;
        int defaultBufferTime = 1;

        Ast::AstParse parsed((list<Ast::Section*>*) ParseCache::parseCmd(full.path()));
        for (Ast::AstParse::section_iterator section_it = parsed.getSections()->begin(); section_it != parsed.getSections()->end(); section_it++){
            Ast::Section * section = *section_it;
            std::string head = section->getName();
            head = Util::fixCase(head);

            if (head == "command"){
                class CommandWalker: public Ast::Walker {
                public:
                    CommandWalker(Character & self, const int defaultTime, const int defaultBufferTime):
                        self(self),
                        time(defaultTime),
                        bufferTime(defaultBufferTime),
                        key(0){
                        }

                    Character & self;
                    int time;
                    int bufferTime;
                    string name;
                    Ast::Key * key;

                    virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                        if (simple == "name"){
                            simple >> name;
                        } else if (simple == "command"){
                            key = (Ast::Key*) simple.getValue()->copy();
                        } else if (simple == "time"){
                            simple >> time;
                        } else if (simple == "buffer.time"){
                            simple >> bufferTime;
                            /* Time that the command will be buffered for. If the command is done
                             * successfully, then it will be valid for this time. The simplest
                             * case is to set this to 1. That means that the command is valid
                             * only in the same tick it is performed. With a higher value, such
                             * as 3 or 4, you can get a "looser" feel to the command. The result
                             * is that combos can become easier to do because you can perform
                             * the command early.
                             */
                        }
                    }

                    virtual ~CommandWalker(){
                        if (name == ""){
                            throw MugenException("No name given for command");
                        }

                        if (key == 0){
                            throw MugenException("No key sequence given for command");
                        }

                        /* parser guarantees the key will be a KeyList */
                        self.addCommand(new Command(name, (Ast::KeyList*) key, time, bufferTime));
                    }
                };

                CommandWalker walker(*this, defaultTime, defaultBufferTime);
                section->walk(walker);
            } else if (head == "defaults"){
                class DefaultWalker: public Ast::Walker {
                public:
                    DefaultWalker(int & time, int & buffer):
                        time(time),
                        buffer(buffer){
                        }

                    int & time;
                    int & buffer;

                    virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                        if (simple == "command.time"){
                            simple >> time;
                        } else if (simple == "command.buffer.time"){
                            simple >> buffer;
                        }
                    }
                };

                DefaultWalker walker(defaultTime, defaultBufferTime);
                section->walk(walker);
            } else if (PaintownUtil::matchRegex(head, "statedef")){
                parseStateDefinition(section);
            } else if (PaintownUtil::matchRegex(head, "state ")){
                parseState(section);
            }

            /* [Defaults]
             * ; Default value for the "time" parameter of a Command. Minimum 1.
             * command.time = 15
             *
             * ; Default value for the "buffer.time" parameter of a Command. Minimum 1,
             * ; maximum 30.
             * command.buffer.time = 1
             */
        }
    } catch (const Mugen::Cmd::ParseException & e){
        /*
        Global::debug(0) << "Could not parse " << path << endl;
        Global::debug(0) << e.getReason() << endl;
        */
        ostringstream out;
        out << "Could not parse " << path.path() << ": " << e.getReason();
        throw MugenException(out.str());
    }
}

static bool isStateDefSection(string name){
    name = Util::fixCase(name);
    return PaintownUtil::matchRegex(name, "state ") ||
           PaintownUtil::matchRegex(name, "statedef ");
}

bool Character::canBeHit(Character * enemy){
    return (moveType != Move::Hit && lastTicket < enemy->getTicket()) ||
           (moveType == Move::Hit && lastTicket < enemy->getTicket() &&
            juggleRemaining >= enemy->getCurrentJuggle());
}
    
void Character::setConstant(std::string name, const vector<double> & values){
    constants[name] = Constant(values);
}

void Character::setConstant(std::string name, double value){
    constants[name] = Constant(value);
}

void Character::setFloatVariable(int index, Compiler::Value * value){
    floatVariables[index] = value;
}

void Character::setVariable(int index, Compiler::Value * value){
    variables[index] = value;
}

Compiler::Value * Character::getVariable(int index) const {
    map<int, Compiler::Value*>::const_iterator found = variables.find(index);
    if (found != variables.end()){
        return (*found).second;
    }
    return 0;
}

Compiler::Value * Character::getFloatVariable(int index) const {
    map<int, Compiler::Value*>::const_iterator found = floatVariables.find(index);
    if (found != variables.end()){
        return (*found).second;
    }
    return 0;
}
        
void Character::setSystemVariable(int index, Compiler::Value * value){
    variables[index] = value;
}

Compiler::Value * Character::getSystemVariable(int index) const {
    map<int, Compiler::Value*>::const_iterator found = variables.find(index);
    if (found != variables.end()){
        return (*found).second;
    }
    return 0;
}
        
void Character::resetStateTime(){
    stateTime = 0;
}
        
void Character::changeState(MugenStage & stage, int stateNumber, const vector<string> & inputs){
    /* reset juggle points once the player gets up */
    if (stateNumber == GetUpFromLiedown){
        juggleRemaining = getJugglePoints();
    }

    /* reset hit count */
    hitCount = 0;

    Global::debug(1) << "Change to state " << stateNumber << endl;
    previousState = currentState;
    currentState = stateNumber;
    resetStateTime();
    if (states[currentState] != 0){
        State * state = states[currentState];
        state->transitionTo(stage, *this);
        doStates(stage, inputs, currentState);
    } else {
        Global::debug(0) << "Unknown state " << currentState << endl;
    }
}

void Character::loadCnsFile(const Filesystem::RelativePath & path){
    Filesystem::AbsolutePath full = baseDir.join(path);
    try{
        /* cns can use the Cmd parser */
        Ast::AstParse parsed((list<Ast::Section*>*) ParseCache::parseCmd(full.path()));
        for (Ast::AstParse::section_iterator section_it = parsed.getSections()->begin(); section_it != parsed.getSections()->end(); section_it++){
            Ast::Section * section = *section_it;
            std::string head = section->getName();
            head = Util::fixCase(head);
            if (false && !isStateDefSection(head)){
                /* I dont think this is the right way to do it */
                class AttributeWalker: public Ast::Walker {
                public:
                    AttributeWalker(Character & who):
                    self(who){
                    }

                    Character & self;

                    virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                        string name = simple.idString();
                        if (simple.getValue() != 0 && simple.getValue()->hasMultiple()){
                            vector<double> values;
                            simple >> values;
                            self.setConstant(name, values);
                        } else {
                            double value;
                            simple >> value;
                            self.setConstant(name, value);
                        }
                    }
                };

                AttributeWalker walker(*this);
                section->walk(walker);
            } else if (head == "velocity"){
                class VelocityWalker: public Ast::Walker {
                public:
                    VelocityWalker(Character & who):
                    self(who){
                    }

                    Character & self;

                    virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                        if (simple == "walk.fwd"){
                            double speed;
                            simple >> speed;
                            self.setWalkForwardX(speed);
                        } else if (simple == "walk.back"){
                            double speed;
                            simple >> speed;
                            self.setWalkBackX(speed);
                        } else if (simple == "run.fwd"){
                            double x, y;
                            simple >> x >> y;
                            self.setRunForwardX(x);
                            self.setRunForwardY(y);
                        } else if (simple == "run.back"){
                            double x, y;
                            simple >> x >> y;
                            self.setRunBackX(x);
                            self.setRunBackY(y);
                        } else if (simple == "jump.neu"){
                            double x, y;
                            simple >> x >> y;
                            self.setNeutralJumpingX(x);
                            self.setNeutralJumpingY(y);
                        } else if (simple == "jump.back"){
                            double speed;
                            simple >> speed;
                            self.setJumpBack(speed);
                        } else if (simple == "jump.fwd"){
                            double speed;
                            simple >> speed;
                            self.setJumpForward(speed);
                        } else if (simple == "runjump.back"){
                            double speed;
                            simple >> speed;
                            self.setRunJumpBack(speed);
                        } else if (simple == "runjump.fwd"){
                            double speed;
                            simple >> speed;
                            self.setRunJumpForward(speed);
                        } else if (simple == "airjump.neu"){
                            double x, y;
                            simple >> x >> y;
                            self.setAirJumpNeutralX(x);
                            self.setAirJumpNeutralY(y);
                        } else if (simple == "airjump.back"){
                            double speed;
                            simple >> speed;
                            self.setAirJumpBack(speed);
                        } else if (simple == "airjump.fwd"){
                            double speed;
                            simple >> speed;
                            self.setAirJumpForward(speed);
                        }
                    }
                };

                VelocityWalker walker(*this);
                section->walk(walker);
            } else if (head == "data"){
                class DataWalker: public Ast::Walker {
                public:
                    DataWalker(Character & who):
                    self(who){
                    }
                    
                    Character & self;

                    virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                        if (simple == "liedown.time"){
                            int x;
                            simple >> x;
                            self.setLieDownTime(x);
                        } else if (simple == "airjuggle"){
                            int x;
                            simple >> x;
                            self.setJugglePoints(x);
                        } else if (simple == "life"){
                            int x;
                            simple >> x;
                            self.setMaxHealth(x);
                            self.setHealth(x);
                        } else if (simple == "sparkno"){
                            string spark;
                            simple >> spark;
                            spark = PaintownUtil::lowerCaseAll(spark);
                            if (PaintownUtil::matchRegex(spark, "s[0-9]+")){
                                /* FIXME: handle S */
                            } else {
                                self.setDefaultSpark(atoi(spark.c_str()));
                            }
                        } else if (simple == "guard.sparkno"){
                            string spark;
                            simple >> spark;
                            if (PaintownUtil::matchRegex(spark, "s[0-9]+")){
                                /* FIXME: handle S */
                            } else {
                                self.setDefaultGuardSpark(atoi(spark.c_str()));
                            }
                        }
                    }

                };
                
                DataWalker walker(*this);
                section->walk(walker);
            } else if (head == "size"){
                class SizeWalker: public Ast::Walker {
                public:
                    SizeWalker(Character & self):
                        self(self){
                        }

                    Character & self;

                    virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                        if (simple == "height"){
                            int x;
                            simple >> x;
                            self.setHeight(x);
                        } else if (simple == "xscale"){
			    simple >> self.xscale;
			} else if (simple == "yscale"){
			    simple >> self.yscale;
			} else if (simple == "ground.back"){
			    simple >> self.groundback;
			} else if (simple == "ground.front"){
			    simple >> self.groundfront;
			} else if (simple == "air.back"){
			    simple >> self.airback;
			} else if (simple == "air.front"){
			    simple >> self.airfront;
			} else if (simple == "attack.dist"){
			    simple >> self.attackdist;
			} else if (simple == "proj.attack.dist"){
			    simple >> self.projattackdist;
			} else if (simple == "proj.doscale"){
			    simple >> self.projdoscale;
			} else if (simple == "head.pos"){
			    int x=0,y=0;
			    try{
				simple >> self.headPosition.x >> self.headPosition.y;
			    } catch (const Ast::Exception & e){
			    }
			} else if (simple == "mid.pos"){
			    int x=0,y=0;
			    try{
				simple >> self.midPosition.x >> self.midPosition.y;
			    } catch (const Ast::Exception & e){
			    }
			} else if (simple == "shadowoffset"){
			    simple >> self.shadowoffset;
			} else if (simple == "draw.offset"){
			    int x=0,y=0;
			    try{
				simple >> self.drawOffset.x >> self.drawOffset.y;
			    } catch (const Ast::Exception & e){
			    }
			}
                    }
                };
                
                SizeWalker walker(*this);
                section->walk(walker);

            } else if (head == "movement"){
                class MovementWalker: public Ast::Walker {
                public:
                    MovementWalker(Character & who):
                    self(who){
                    }

                    Character & self;

                    virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                        if (simple == "yaccel"){
                            double n;
                            simple >> n;
                            self.setGravity(n);
                        } else if (simple == "stand.friction"){
                            double n;
                            simple >> n;
                            self.setStandingFriction(n);
                        } else if (simple == "airjump.num"){
                            int x;
                            simple >> x;
                            self.setExtraJumps(x);
                        } else if (simple == "airjump.height"){
                            double x;
                            simple >> x;
                            self.setAirJumpHeight(x);
                        }
                    }
                };

                MovementWalker walker(*this);
                section->walk(walker);
            }
        }
    } catch (const Mugen::Cmd::ParseException & e){
        ostringstream out;
        out << "Could not parse " << path.path() << ": " << e.getReason();
        throw MugenException(out.str());
    } catch (const Ast::Exception & e){
        ostringstream out;
        out << "Could not parse " << path.path() << ": " << e.getReason();
        throw MugenException(out.str());
    }
}

void Character::parseStateDefinition(Ast::Section * section){
    std::string head = section->getName();
    /* this should really be head = Mugen::Util::fixCase(head) */
    head = Util::fixCase(head);

    int state = atoi(PaintownUtil::captureRegex(head, "statedef *(-?[0-9]+)", 0).c_str());
    class StateWalker: public Ast::Walker {
        public:
            StateWalker(State * definition):
                definition(definition){
                }

            State * definition;

            virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                if (simple == "type"){
                    string type;
                    simple >> type;
                    type = PaintownUtil::lowerCaseAll(type);
                    if (type == "s"){
                        definition->setType(State::Standing);
                    } else if (type == "c"){
                        definition->setType(State::Crouching);
                    } else if (type == "a"){
                        definition->setType(State::Air);
                    } else if (type == "l"){
                        definition->setType(State::LyingDown);
                    } else if (type == "u"){
                        definition->setType(State::Unchanged);
                    } else {
                        ostringstream out;
                        out << "Unknown statedef type: '" << type << "'";
                        throw MugenException(out.str());
                    }
                } else if (simple == "movetype"){
                    string type;
                    simple >> type;
                    definition->setMoveType(type);
                } else if (simple == "physics"){
                    string type;
                    simple >> type;
                    if (type == "S"){
                        definition->setPhysics(Physics::Stand);
                    } else if (type == "N"){
                        definition->setPhysics(Physics::None);
                    } else if (type == "C"){
                        definition->setPhysics(Physics::Crouch);
                    } else if (type == "A"){
                        definition->setPhysics(Physics::Air);
                    }
                    /* if physics is U (unchanged) then dont set the state physics
                     * and then the character's physics won't change during
                     * a state transition.
                     */
                } else if (simple == "anim"){
                    definition->setAnimation(Compiler::compile(simple.getValue()));
                } else if (simple == "velset"){
                    const Ast::Value * x;
                    const Ast::Value * y;
                    simple >> x >> y;
                    definition->setVelocity(Compiler::compile(x), Compiler::compile(y));
                } else if (simple == "ctrl"){
                    definition->setControl(Compiler::compile(simple.getValue()));
                } else if (simple == "poweradd"){
                    definition->setPower(Compiler::compile(simple.getValue()));
                } else if (simple == "juggle"){
                    definition->setJuggle(Compiler::compile(simple.getValue()));
                } else if (simple == "facep2"){
                } else if (simple == "hitdefpersist"){
                    bool what;
                    simple >> what;
                    definition->setHitDefPersist(what);
                } else if (simple == "movehitpersist"){
                } else if (simple == "hitcountpersist"){
                } else if (simple == "sprpriority"){
                } else {
                    Global::debug(0) << "Unhandled statedef attribute: " << simple.toString() << endl;
                }
            }
    };

    State * definition = new State();
    StateWalker walker(definition);
    section->walk(walker);
    if (states[state] != 0){
        Global::debug(1) << "Overriding state " << state << endl;
        delete states[state];
    }
    Global::debug(1) << "Adding state definition " << state << endl;
    states[state] = definition;
}

static StateController * compileStateController(Ast::Section * section, const string & name, int state, StateController::Type type){
    switch (type){
        case StateController::ChangeAnim: {
            class ControllerChangeAnim: public StateController {
            public:
                ControllerChangeAnim(Ast::Section * section, const string & name):
                    StateController(name, section),
                    value(NULL){
                        parse(section);
                    }

                Compiler::Value * value;

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(Compiler::Value *& value):
                            value(value){
                            }

                        Compiler::Value *& value;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "value"){
                                value = Compiler::compile(simple.getValue());
                            }
                        }
                    };

                    Walker walker(value);
                    section->walk(walker);
                    if (value == NULL){
                        ostringstream out;
                        out << "Expected the `value' attribute for state " << name;
                        throw MugenException(out.str());
                    }
                }

                virtual ~ControllerChangeAnim(){
                    delete value;
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    RuntimeValue result = value->evaluate(FullEnvironment(stage, guy));
                    if (result.isDouble()){
                        int value = (int) result.getDoubleValue();
                        guy.setAnimation(value);
                    }
                }
            };

            return new ControllerChangeAnim(section, name);
            break;
        }
        case StateController::ChangeState : {
            class ControllerChangeState: public StateController {
            public:
                ControllerChangeState(Ast::Section * section, const std::string & name):
                    StateController(name, section),
                    value(NULL){
                        parse(section);
                    }

                Compiler::Value * value;

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(Compiler::Value *& value):
                            value(value){
                            }

                        Compiler::Value *& value;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "value"){
                                value = Compiler::compile(simple.getValue());
                            }
                        }
                    };

                    Walker walker(value);
                    section->walk(walker);
                    if (value == NULL){
                        ostringstream out;
                        out << "Expected the `value' attribute for state " << name;
                        throw MugenException(out.str());
                    }
                }

                virtual ~ControllerChangeState(){
                    delete value;
                }
                
                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    RuntimeValue result = value->evaluate(FullEnvironment(stage, guy));
                    if (result.isDouble()){
                        int value = (int) result.getDoubleValue();
                        guy.changeState(stage, value, commands);
                    }
                }
            };

            return new ControllerChangeState(section, name);
            break;
        }
        case StateController::CtrlSet : {
            class ControllerCtrlSet: public StateController {
            public:
                ControllerCtrlSet(Ast::Section * section, const string & name):
                StateController(name, section),
                value(NULL){
                    parse(section);
                }

                Compiler::Value * value;

                virtual ~ControllerCtrlSet(){
                    delete value;
                }

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(Compiler::Value *& value):
                            value(value){
                            }

                        Compiler::Value *& value;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "value"){
                                value = Compiler::compile(simple.getValue());
                            }
                        }
                    };

                    Walker walker(value);
                    section->walk(walker);
                    if (value == NULL){
                        ostringstream out;
                        out << "Expected the `value' attribute for state " << name;
                        throw MugenException(out.str());
                    }
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    RuntimeValue result = value->evaluate(FullEnvironment(stage, guy));
                    guy.setControl(toBool(result));
                }
            };

            return new ControllerCtrlSet(section, name);
            break;
        }
        case StateController::PlaySnd : {
            class ControllerPlaySound: public StateController {
            public:
                ControllerPlaySound(Ast::Section * section, const string & name):
                StateController(name, section),
                group(-1),
                own(false),
                item(NULL){
                    parse(section);
                }

                int group;
                bool own;
                Compiler::Value * item;

                virtual ~ControllerPlaySound(){
                    delete item;
                }

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(ControllerPlaySound & controller):
                            controller(controller){
                            }

                        ControllerPlaySound & controller;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "value"){
                                controller.parseSound(simple.getValue());
                            }
                        }
                    };

                    Walker walker(*this);
                    section->walk(walker);
                }

                virtual void parseSound(const Ast::Value * value){
                    try{
                        string group;
                        const Ast::Value * item;
                        *value >> group >> item;
                        if (PaintownUtil::matchRegex(group, "F[0-9]+")){
                            int realGroup = atoi(PaintownUtil::captureRegex(group, "F([0-9]+)", 0).c_str());
                            this->group = realGroup;
                            this->item = Compiler::compile(item);
                            own = true;
                        } else if (PaintownUtil::matchRegex(group, "[0-9]+")){
                            this->group = atoi(group.c_str());
                            this->item = Compiler::compile(item);
                            own = false;
                        }
                    } catch (const MugenException & e){
                        // Global::debug(0) << "Error with PlaySnd " << controller.name << ": " << e.getReason() << endl;
                        Global::debug(0) << "Error with PlaySnd :" << e.getReason() << endl;
                    }

                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    MugenSound * sound = NULL;
                    if (item != NULL){
                        int itemNumber = (int) item->evaluate(FullEnvironment(stage, guy)).toNumber();
                        if (own){
                            sound = guy.getCommonSound(group, itemNumber);
                        } else {
                            sound = guy.getSound(group, itemNumber);
                        }
                    }

                    if (sound != NULL){
                        sound->play();
                    }
                }
            };

            return new ControllerPlaySound(section, name);
            break;
        }
        case StateController::VarSet : {
            class ControllerVarSet: public StateController {
            public:
                ControllerVarSet(Ast::Section * section, const string & name):
                StateController(name, section),
                value(NULL),
                variable(NULL){
                    parse(section);
                }

                Compiler::Value * value;
                Compiler::Value * variable;
                map<int, Compiler::Value*> variables;
                map<int, Compiler::Value*> floatVariables;
                map<int, Compiler::Value*> systemVariables;

                virtual ~ControllerVarSet(){
                    delete value;
                    delete variable;
                    delete_map(variables);
                    delete_map(floatVariables);
                    delete_map(systemVariables);
                }

                void delete_map(const map<int, Compiler::Value*> & values){
                    for (map<int, Compiler::Value*>::const_iterator it = values.begin(); it != values.end(); it++){
                        Compiler::Value * value = (*it).second;
                        delete value;
                    }
                }

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(ControllerVarSet & controller):
                            controller(controller){
                            }

                        ControllerVarSet & controller;

                        virtual void onAttributeArray(const Ast::AttributeArray & simple){
                            if (simple == "var"){
                                int index = simple.getIndex();
                                const Ast::Value * value = simple.getValue();
                                controller.variables[index] = Compiler::compile(value);
                            } else if (simple == "fvar"){
                                int index = simple.getIndex();
                                const Ast::Value * value = simple.getValue();
                                controller.floatVariables[index] = Compiler::compile(value);
                            } else if (simple == "sysvar"){
                                int index = simple.getIndex();
                                const Ast::Value * value = simple.getValue();
                                controller.systemVariables[index] = Compiler::compile(value);
                            }
                        }

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "value"){
                                controller.value = Compiler::compile(simple.getValue());
                            } else if (simple == "v"){
                                controller.variable = Compiler::compile(simple.getValue());
                            }
                        }
                    };

                    Walker walker(*this);
                    section->walk(walker);
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    for (map<int, Compiler::Value*>::const_iterator it = variables.begin(); it != variables.end(); it++){
                        int index = (*it).first;
                        Compiler::Value * value = (*it).second;
                        guy.setVariable(index, value);
                    }

                    for (map<int, Compiler::Value*>::const_iterator it = floatVariables.begin(); it != floatVariables.end(); it++){
                        int index = (*it).first;
                        Compiler::Value * value = (*it).second;
                        guy.setFloatVariable(index, value);
                    }

                    for (map<int, Compiler::Value*>::const_iterator it = systemVariables.begin(); it != systemVariables.end(); it++){
                        int index = (*it).first;
                        Compiler::Value * value = (*it).second;
                        guy.setSystemVariable(index, value);
                    }

                    if (value != NULL && variable != NULL){
                        /* 'value = 23' is value1
                         * 'v = 9' is value2
                         */
                        guy.setVariable((int) variable->evaluate(FullEnvironment(stage, guy, commands)).toNumber(), value);
                    }
                }
            };

            return new ControllerVarSet(section, name);
            
            break;
        }
        case StateController::VelSet : {
            class VelSet: public StateController {
            public:
                VelSet(Ast::Section * section, const string & name):
                    StateController(name, section),
                    x(NULL),
                    y(NULL){
                        parse(section);
                    }

                Compiler::Value * x;
                Compiler::Value * y;

                virtual ~VelSet(){
                    delete x;
                    delete y;
                }

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(VelSet & controller):
                            controller(controller){
                            }

                        VelSet & controller;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "x"){
                                controller.x = Compiler::compile(simple.getValue());
                            } else if (simple == "y"){
                                controller.y = Compiler::compile(simple.getValue());
                            }
                        }
                    };

                    Walker walker(*this);
                    section->walk(walker);
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    if (x != NULL){
                        RuntimeValue result = x->evaluate(FullEnvironment(stage, guy));
                        if (result.isDouble()){
                            guy.setXVelocity(result.getDoubleValue());
                        }
                    }
                    if (y != NULL){
                        RuntimeValue result = y->evaluate(FullEnvironment(stage, guy));
                        if (result.isDouble()){
                            guy.setYVelocity(result.getDoubleValue());
                        }
                    }
                }
            };

            return new VelSet(section, name);
            break;
        }
        case StateController::HitVelSet : {
            class HitVelSet: public StateController {
            public:
                HitVelSet(Ast::Section * section, const string & name):
                    StateController(name, section),
                    x(NULL),
                    y(NULL){
                        parse(section);
                    }

                Compiler::Value * x;
                Compiler::Value * y;

                virtual ~HitVelSet(){
                    delete x;
                    delete y;
                }

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(HitVelSet & controller):
                            controller(controller){
                            }

                        HitVelSet & controller;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "x"){
                                controller.x = Compiler::compile(simple.getValue());
                            } else if (simple == "y"){
                                controller.y = Compiler::compile(simple.getValue());
                            }
                        }
                    };

                    Walker walker(*this);
                    section->walk(walker);
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    if (x != NULL){
                        RuntimeValue result = x->evaluate(FullEnvironment(stage, guy));
                        if (result.getBoolValue()){
                            guy.setXVelocity(guy.getHitState().xVelocity);
                        }
                    }

                    if (y != NULL){
                        RuntimeValue result = y->evaluate(FullEnvironment(stage, guy));
                        if (result.getBoolValue()){
                            guy.setYVelocity(guy.getHitState().yVelocity);
                        }
                    }
                }

            };

            return new HitVelSet(section, name);
            
            break;
        }
        case StateController::PosAdd : {
            class PosAdd: public StateController {
            public:
                PosAdd(Ast::Section * section, const string & name):
                    StateController(name, section),
                    x(NULL),
                    y(NULL){
                        parse(section);
                    }

                Compiler::Value * x;
                Compiler::Value * y;

                virtual ~PosAdd(){
                    delete x;
                    delete y;
                }

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(PosAdd & controller):
                            controller(controller){
                            }

                        PosAdd & controller;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "x"){
                                controller.x = Compiler::compile(simple.getValue());
                            } else if (simple == "y"){
                                controller.y = Compiler::compile(simple.getValue());
                            }
                        }
                    };

                    Walker walker(*this);
                    section->walk(walker);
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    if (x != NULL){
                        RuntimeValue result = x->evaluate(FullEnvironment(stage, guy));
                        if (result.isDouble()){
                            guy.moveX(result.getDoubleValue());
                            // guy.setX(guy.getX() + result.getDoubleValue());
                        }
                    }
                    if (y != NULL){
                        RuntimeValue result = y->evaluate(FullEnvironment(stage, guy));
                        if (result.isDouble()){
                            guy.moveYNoCheck(-result.getDoubleValue());
                            // guy.setY(guy.getY() + result.getDoubleValue());
                        }
                    }
                }

            };

            return new PosAdd(section, name);

            break;
        }
        case StateController::PosSet : {
            class PosSet: public StateController {
            public:
                PosSet(Ast::Section * section, const string & name):
                    StateController(name, section),
                    x(NULL),
                    y(NULL){
                        parse(section);
                    }

                Compiler::Value * x;
                Compiler::Value * y;

                virtual ~PosSet(){
                    delete x;
                    delete y;
                }

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(PosSet & controller):
                            controller(controller){
                            }

                        PosSet & controller;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "x"){
                                controller.x = Compiler::compile(simple.getValue());
                            } else if (simple == "y"){
                                controller.y = Compiler::compile(simple.getValue());
                            }
                        }
                    };

                    Walker walker(*this);
                    section->walk(walker);
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    if (x != NULL){
                        RuntimeValue result = x->evaluate(FullEnvironment(stage, guy));
                        if (result.isDouble()){
                            guy.setX(result.getDoubleValue());
                        }
                    }
                    if (y != NULL){
                        RuntimeValue result = y->evaluate(FullEnvironment(stage, guy));
                        if (result.isDouble()){
                            guy.setY(result.getDoubleValue());
                        }
                    }
                }

            };

            return new PosSet(section, name);

            break;
        }
        case StateController::VelAdd : {
            class VelAdd: public StateController {
            public:
                VelAdd(Ast::Section * section, const string & name):
                    StateController(name, section),
                    x(NULL),
                    y(NULL){
                        parse(section);
                    }

                Compiler::Value * x;
                Compiler::Value * y;

                virtual ~VelAdd(){
                    delete x;
                    delete y;
                }

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(VelAdd & controller):
                            controller(controller){
                            }

                        VelAdd & controller;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "x"){
                                controller.x = Compiler::compile(simple.getValue());
                            } else if (simple == "y"){
                                controller.y = Compiler::compile(simple.getValue());
                            }
                        }
                    };

                    Walker walker(*this);
                    section->walk(walker);
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    if (x != NULL){
                        RuntimeValue result = x->evaluate(FullEnvironment(stage, guy));
                        if (result.isDouble()){
                            guy.setXVelocity(guy.getXVelocity() + result.getDoubleValue());
                        }
                    }
                    if (y != NULL){
                        RuntimeValue result = y->evaluate(FullEnvironment(stage, guy));
                        if (result.isDouble()){
                            guy.setYVelocity(guy.getYVelocity() + result.getDoubleValue());
                        }
                    }
                }
            };

            return new VelAdd(section, name);

            break;
        }
        case StateController::VelMul : {
            class VelMul: public StateController {
            public:
                VelMul(Ast::Section * section, const string & name):
                    StateController(name, section),
                    x(NULL),
                    y(NULL){
                        parse(section);
                    }

                Compiler::Value * x;
                Compiler::Value * y;

                virtual ~VelMul(){
                    delete x;
                    delete y;
                }

                void parse(Ast::Section * section){
                    class Walker: public Ast::Walker {
                    public:
                        Walker(VelMul & controller):
                            controller(controller){
                            }

                        VelMul & controller;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "x"){
                                controller.x = Compiler::compile(simple.getValue());
                            } else if (simple == "y"){
                                controller.y = Compiler::compile(simple.getValue());
                            }
                        }
                    };

                    Walker walker(*this);
                    section->walk(walker);
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    if (x != NULL){
                        RuntimeValue result = x->evaluate(FullEnvironment(stage, guy));
                        if (result.isDouble()){
                            guy.setXVelocity(guy.getXVelocity() * result.getDoubleValue());
                        }
                    }

                    if (y != NULL){
                        RuntimeValue result = y->evaluate(FullEnvironment(stage, guy));
                        if (result.isDouble()){
                            guy.setYVelocity(guy.getYVelocity() * result.getDoubleValue());
                        }
                    }
                }
            };

            return new VelMul(section, name);
            break;
        }
#if 0
        case StateController::HitDef : {
            /* prevent the same hitdef from being applied */
            if (guy.getHit() != &controller.getHit()){
                guy.setHitDef(&controller.getHit());
                guy.nextTicket();
            }
            break;
        }
        case StateController::StateTypeSet : {
            if (controller.changeMoveType){
                guy.setMoveType(controller.moveType);
            }

            if (controller.changeStateType){
                guy.setStateType(controller.stateType);
            }

            if (controller.changePhysics){
                guy.setCurrentPhysics(controller.physics);
            }
            break;
        }
        case StateController::SuperPause : {
            FullEnvironment env(stage, guy);
            int x = guy.getRX() + (int) controller.posX->evaluate(env).toNumber() * (guy.getFacing() == Object::FACING_LEFT ? -1 : 1);
            int y = guy.getRY() + (int) controller.posY->evaluate(env).toNumber();
            /* 30 is the default I think.. */
            int time = 30;
            if (controller.time != NULL){
                time = (int) controller.time->evaluate(env).toNumber();
            }
            stage.doSuperPause(time, controller.animation, x, y, controller.sound.group, controller.sound.item); 
            break;
        }
        
       
#endif
        case StateController::HitDef :
        case StateController::SuperPause :
        case StateController::StateTypeSet :

        case StateController::AfterImage :
        case StateController::AfterImageTime :
        case StateController::AllPalFX :
        case StateController::AngleAdd :
        case StateController::AngleDraw :
        case StateController::AngleMul :
        case StateController::AngleSet :
        case StateController::AppendToClipboard :
        case StateController::AssertSpecial :
        case StateController::AttackDist :
        case StateController::AttackMulSet :
        case StateController::BGPalFX :
        case StateController::BindToParent :
        case StateController::BindToRoot :
        case StateController::BindToTarget :
        case StateController::ChangeAnim2 :
        case StateController::ClearClipboard :
        case StateController::DefenceMulSet :
        case StateController::DestroySelf :
        case StateController::DisplayToClipboard :
        case StateController::EnvColor :
        case StateController::EnvShake :
        case StateController::Explod :
        case StateController::ExplodBindTime :
        case StateController::ForceFeedback :
        case StateController::FallEnvShake :
        case StateController::GameMakeAnim :
        case StateController::Gravity :
        case StateController::Helper :
        case StateController::HitAdd :
        case StateController::HitBy :
        case StateController::HitFallDamage :
        case StateController::HitFallSet :
        case StateController::HitFallVel :
        case StateController::HitOverride :
        case StateController::LifeAdd :
        case StateController::LifeSet :
        case StateController::MakeDust :
        case StateController::ModifyExplod :
        case StateController::MoveHitReset :
        case StateController::NotHitBy :
        case StateController::Null :
        case StateController::Offset :
        case StateController::PalFX :
        case StateController::ParentVarAdd :
        case StateController::ParentVarSet :
        case StateController::Pause :
        case StateController::PlayerPush :
        case StateController::PosFreeze :
        case StateController::PowerAdd :
        case StateController::PowerSet :
        case StateController::Projectile :
        case StateController::RemoveExplod :
        case StateController::ReversalDef :
        case StateController::ScreenBound :
        case StateController::SelfState :
        case StateController::SprPriority :
        case StateController::SndPan :
        case StateController::StopSnd :
        case StateController::TargetBind :
        case StateController::TargetDrop :
        case StateController::TargetFacing :
        case StateController::TargetLifeAdd :
        case StateController::TargetPowerAdd :
        case StateController::TargetState :
        case StateController::TargetVelAdd :
        case StateController::TargetVelSet :
        case StateController::Trans :
        case StateController::Turn :
        case StateController::VarAdd :
        case StateController::VarRandom :
        case StateController::VarRangeSet :
        case StateController::Width : {
            class DefaultController: public StateController {
            public:
                DefaultController(Ast::Section * section, const string & name):
                StateController(name, section){
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    /* nothing */
                }
            };

            return new DefaultController(section, name);
        }
        case StateController::Unknown : {
            ostringstream out;
            out << "Unknown state controller for " << state << " " << name;
            throw MugenException(out.str());
            break;
        }
        /*
        case InternalCommand : {
            typedef void (Character::*func)(const MugenStage & stage, const vector<string> & inputs);
            func f = (func) controller.internal;
            (guy.*f)(stage, commands);
            break;
        }
        */
    };

    ostringstream out;
    out << "Unknown state controller for " << state << " " << name << " type (" << type << ")";
    throw MugenException(out.str());
}

void Character::parseState(Ast::Section * section){
    std::string head = section->getName();
    head = Util::fixCase(head);

    int state = atoi(PaintownUtil::captureRegex(head, "state *(-?[0-9]+)", 0).c_str());
    string name = PaintownUtil::captureRegex(head, "state *-?[0-9]+ *, *(.*)", 0);

#if 0
    class StateControllerWalker: public Ast::Walker {
        public:
            StateControllerWalker(StateController * controller):
                controller(controller){
                }

            StateController * controller;

            string toString(char x){
                ostringstream out;
                out << x;
                return out.str();
            }

            AttackType::Animation parseAnimationType(string type){
                type = Util::fixCase(type);
                if (type == "light"){
                    return AttackType::Light;
                } else if (type == "medium" || type == "med"){
                    return AttackType::Medium;
                } else if (type == "hard" || type == "heavy"){
                    return AttackType::Hard;
                } else if (type == "back"){
                    return AttackType::Back;
                } else if (type == "up"){
                    return AttackType::Up;
                } else if (type == "diagup"){
                    return AttackType::DiagonalUp;
                } else {
                    Global::debug(0) << "Unknown hitdef animation type " << type << endl;
                }
                return AttackType::NoAnimation;
            }

            virtual void onAttributeArray(const Ast::AttributeArray & simple){
                if (simple == "var"){
                    int index = simple.getIndex();
                    const Ast::Value * value = simple.getValue();
                    controller->addVariable(index, Compiler::compile(value));
                } else if (simple == "var"){
                    int index = simple.getIndex();
                    const Ast::Value * value = simple.getValue();
                    controller->addFloatVariable(index, Compiler::compile(value));
                } else if (simple == "sysvar"){
                    int index = simple.getIndex();
                    const Ast::Value * value = simple.getValue();
                    controller->addSystemVariable(index, Compiler::compile(value));
                }
            }

            virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                if (simple == "type"){
                    string type;
                    simple >> type;
                    type = Mugen::Util::fixCase(type);
                    map<string, StateController::Type> types;
                    types["afterimage"] = StateController::AfterImage;
                    types["afterimagetime"] = StateController::AfterImageTime;
                    types["allpalfx"] = StateController::AllPalFX;
                    types["angleadd"] = StateController::AngleAdd;
                    types["angledraw"] = StateController::AngleDraw;
                    types["anglemul"] = StateController::AngleMul;
                    types["angleset"] = StateController::AngleSet;
                    types["appendtoclipboard"] = StateController::AppendToClipboard;
                    types["assertspecial"] = StateController::AssertSpecial;
                    types["attackdist"] = StateController::AttackDist;
                    types["attackmulset"] = StateController::AttackMulSet;
                    types["bgpalfx"] = StateController::BGPalFX;
                    types["bindtoparent"] = StateController::BindToParent;
                    types["bindtoroot"] = StateController::BindToRoot;
                    types["bindtotarget"] = StateController::BindToTarget;
                    types["changeanim"] = StateController::ChangeAnim;
                    types["changeanim2"] = StateController::ChangeAnim2;
                    types["changestate"] = StateController::ChangeState;
                    types["clearclipboard"] = StateController::ClearClipboard;
                    types["ctrlset"] = StateController::CtrlSet;
                    types["defencemulset"] = StateController::DefenceMulSet;
                    types["destroyself"] = StateController::DestroySelf;
                    types["displaytoclipboard"] = StateController::DisplayToClipboard;
                    types["envcolor"] = StateController::EnvColor;
                    types["envshake"] = StateController::EnvShake;
                    types["explod"] = StateController::Explod;
                    types["explodbindtime"] = StateController::ExplodBindTime;
                    types["forcefeedback"] = StateController::ForceFeedback;
                    types["fallenvshake"] = StateController::FallEnvShake;
                    types["gamemakeanim"] = StateController::GameMakeAnim;
                    types["gravity"] = StateController::Gravity;
                    types["helper"] = StateController::Helper;
                    types["hitadd"] = StateController::HitAdd;
                    types["hitby"] = StateController::HitBy;
                    types["hitdef"] = StateController::HitDef;
                    types["hitfalldamage"] = StateController::HitFallDamage;
                    types["hitfallset"] = StateController::HitFallSet;
                    types["hitfallvel"] = StateController::HitFallVel;
                    types["hitoverride"] = StateController::HitOverride;
                    types["hitvelset"] = StateController::HitVelSet;
                    types["lifeadd"] = StateController::LifeAdd;
                    types["lifeset"] = StateController::LifeSet;
                    types["makedust"] = StateController::MakeDust;
                    types["modifyexplod"] = StateController::ModifyExplod;
                    types["movehitreset"] = StateController::MoveHitReset;
                    types["nothitby"] = StateController::NotHitBy;
                    types["null"] = StateController::Null;
                    types["offset"] = StateController::Offset;
                    types["palfx"] = StateController::PalFX;
                    types["parentvaradd"] = StateController::ParentVarAdd;
                    types["parentvarset"] = StateController::ParentVarSet;
                    types["pause"] = StateController::Pause;
                    types["playerpush"] = StateController::PlayerPush;
                    types["playsnd"] = StateController::PlaySnd;
                    types["posadd"] = StateController::PosAdd;
                    types["posfreeze"] = StateController::PosFreeze;
                    types["posset"] = StateController::PosSet;
                    types["poweradd"] = StateController::PowerAdd;
                    types["powerset"] = StateController::PowerSet;
                    types["projectile"] = StateController::Projectile;
                    types["removeexplod"] = StateController::RemoveExplod;
                    types["reversaldef"] = StateController::ReversalDef;
                    types["screenbound"] = StateController::ScreenBound;
                    types["selfstate"] = StateController::SelfState;
                    types["sprpriority"] = StateController::SprPriority;
                    types["statetypeset"] = StateController::StateTypeSet;
                    types["sndpan"] = StateController::SndPan;
                    types["stopsnd"] = StateController::StopSnd;
                    types["superpause"] = StateController::SuperPause;
                    types["targetbind"] = StateController::TargetBind;
                    types["targetdrop"] = StateController::TargetDrop;
                    types["targetfacing"] = StateController::TargetFacing;
                    types["targetlifeadd"] = StateController::TargetLifeAdd;
                    types["targetpoweradd"] = StateController::TargetPowerAdd;
                    types["targetstate"] = StateController::TargetState;
                    types["targetveladd"] = StateController::TargetVelAdd;
                    types["targetvelset"] = StateController::TargetVelSet;
                    types["trans"] = StateController::Trans;
                    types["turn"] = StateController::Turn;
                    types["varadd"] = StateController::VarAdd;
                    types["varrandom"] = StateController::VarRandom;
                    types["varrangeset"] = StateController::VarRangeSet;
                    types["varset"] = StateController::VarSet;
                    types["veladd"] = StateController::VelAdd;
                    types["velmul"] = StateController::VelMul;
                    types["velset"] = StateController::VelSet;
                    types["width"] = StateController::Width;

                    if (types.find(type) != types.end()){
                        map<string, StateController::Type>::iterator what = types.find(type);
                        controller->setType((*what).second);
                    } else {
                        Global::debug(0) << "Unknown state controller type " << type << endl;
                    }
                } else if (simple == "value"){
                    controller->setValue(simple.getValue());
                } else if (simple == "triggerall"){
                    controller->addTriggerAll(Compiler::compile(simple.getValue()));
                } else if (PaintownUtil::matchRegex(PaintownUtil::lowerCaseAll(simple.idString()), "trigger[0-9]+")){
                    int trigger = atoi(PaintownUtil::captureRegex(PaintownUtil::lowerCaseAll(simple.idString()), "trigger([0-9]+)", 0).c_str());
                    controller->addTrigger(trigger, Compiler::compile(simple.getValue()));
                } else if (simple == "x"){
                    controller->setX(Compiler::compile(simple.getValue()));
                } else if (simple == "y"){
                    controller->setY(Compiler::compile(simple.getValue()));
                } else if (simple == "v"){
                    controller->setVariable(Compiler::compile(simple.getValue()));
                } else if (simple == "movetype"){
                    string type;
                    simple >> type;
                    controller->setMoveType(type);
                } else if (simple == "physics"){
                    string type;
                    simple >> type;
                    if (type == "S"){
                        controller->setPhysics(Physics::Stand);
                    } else if (type == "N"){
                        controller->setPhysics(Physics::None);
                    } else if (type == "C"){
                        controller->setPhysics(Physics::Crouch);
                    } else if (type == "A"){
                        controller->setPhysics(Physics::Air);
                    }
                } else if (simple == "statetype"){
                    string type;
                    simple >> type;
                    controller->setStateType(type);
                } else if (simple == "time"){
                    const Ast::Value * time;
                    simple >> time;
                    controller->setTime(Compiler::compile(time));
                } else if (simple == "anim"){
                    string what;
                    simple >> what;
                    if (PaintownUtil::matchRegex(what, "F[0-9]+")){
                        ostringstream context;
                        context << __FILE__ << ":" << __LINE__;
                        Global::debug(0, context.str()) << "Warning: parse animation " << what << endl;
                    } else if (PaintownUtil::matchRegex(what, "[0-9]+")){
                        int t = atoi(what.c_str());
                        controller->setAnimation(t);
                    }
                } else if (simple == "pos"){
                    try{
                        const Ast::Value * x;
                        const Ast::Value * y;
                        simple >> x >> y;
                        controller->setPosition(Compiler::compile(x), Compiler::compile(y));
                    } catch (const Ast::Exception & e){
                        /* should delete the values above if an exception occurs */
                    }
                } else if (simple == "sound"){
                    try{
                        int group, item;
                        simple >> group >> item;
                        controller->setSound(group, item);
                    } catch (const Ast::Exception & e){
                    }
                } else if (simple == "ctrl"){
                    controller->setControl(Compiler::compile(simple.getValue()));
                } else if (simple == "attr"){
                    string type, attack;
                    simple >> type >> attack;
                    if (type == StateType::Stand ||
                        type == StateType::Crouch ||
                        type == StateType::Air){
                        controller->getHit().attribute.state = type;
                    }

                    if (attack.size() >= 2){
                        string type = toString(attack[0]);
                        string physics = toString(attack[1]);
                        if (type == AttackType::Normal ||
                            type == AttackType::Special ||
                            type == AttackType::Hyper){
                            controller->getHit().attribute.attackType = type;
                        }
                        if (physics == PhysicalAttack::Normal ||
                            physics == PhysicalAttack::Throw ||
                            physics == PhysicalAttack::Projectile){
                            controller->getHit().attribute.physics = physics;
                        }
                    }
                } else if (simple == "hitflag"){
                    string flags;
                    simple >> flags;
                    controller->getHit().hitFlag = flags;
                } else if (simple == "guardflag"){
                    string flags;
                    simple >> flags;
                    controller->getHit().guardFlag = flags;
                } else if (simple == "animtype"){
                    string anim;
                    simple >> anim;
                    controller->getHit().animationType = parseAnimationType(anim);
                } else if (simple == "air.animtype"){
                    string anim;
                    simple >> anim;
                    controller->getHit().animationTypeAir = parseAnimationType(anim);
                } else if (simple == "debug"){
                    controller->setDebug(true);
                } else if (simple == "fall.animtype"){
                    string anim;
                    simple >> anim;
                    controller->getHit().animationTypeFall = parseAnimationType(anim);
                } else if (simple == "priority"){
                    int hit;
                    simple >> hit;
                    controller->getHit().priority.hit = hit;
                    try{
                        string type;
                        simple >> type;
                        controller->getHit().priority.type = type;
                    } catch (const Ast::Exception & e){
                    }
                } else if (simple == "damage"){
                    try{
                        const Ast::Value * container = simple.getValue();
                        /*
                        container->reset();
                        Ast::Value * value;
                        *container >> value;
                        controller->getHit().damage.damage = (Ast::Value*) value->copy();
                        */

                        /* has guard */
                        if (container->hasMultiple()){
                            container->reset();
                            const Ast::Value * value;
                            *container >> value;
                            controller->getHit().damage.damage = Compiler::compile(value);
                            *container >> value;
                            controller->getHit().damage.guardDamage = Compiler::compile(value);
                        } else {
                            /* otherwise its a single expression */
                            controller->getHit().damage.damage = Compiler::compile(container);
                        }
                    } catch (const Ast::Exception & e){
                        ostringstream out;
                        out << "Could not read `damage' from '" << simple.toString() << "': " << e.getReason();
                        throw MugenException(out.str());
                    }
                } else if (simple == "pausetime"){
                    try{
                        simple >> controller->getHit().pause.player1;
                        simple >> controller->getHit().pause.player2;
                    } catch (const Ast::Exception & e){
                    }
                } else if (simple == "guard.pausetime"){
                    try{
                        simple >> controller->getHit().guardPause.player1;
                        simple >> controller->getHit().guardPause.player2;
                    } catch (const Ast::Exception & e){
                    }
                } else if (simple == "sparkno"){
                    string what;
                    simple >> what;
                    /* either S123 or 123 */
                    if (PaintownUtil::matchRegex(what, "[0-9]+")){
                        controller->getHit().spark = atoi(what.c_str());
                    }
                } else if (simple == "guard.sparkno"){
                    string what;
                    simple >> what;
                    /* either S123 or 123 */
                    if (PaintownUtil::matchRegex(what, "[0-9]+")){
                        controller->getHit().guardSpark = atoi(what.c_str());
                    }
                } else if (simple == "sparkxy"){
                    try{
                        simple >> controller->getHit().sparkPosition.x;
                        simple >> controller->getHit().sparkPosition.y;
                    } catch (const Ast::Exception & e){
                    }
                } else if (simple == "hitsound"){
                    string first;
                    bool own = false;
                    int group = 0;
                    int item = 0;
                    /* If not specified, assume item 0 */
                    simple >> first;
                    if (simple.getValue()->hasMultiple()){
                        simple >> item;
                    }
                    if (first[0] == 'S'){
                        own = true;
                        group = atoi(first.substr(1).c_str());
                    } else {
                        group = atoi(first.c_str());
                    }
                    controller->getHit().hitSound.own = own;
                    controller->getHit().hitSound.group = group;
                    controller->getHit().hitSound.item = item;
                } else if (simple == "guardsound"){
                    string first;
                    bool own = false;
                    int group;
                    int item;
                    simple >> first >> item;
                    if (first[0] == 'S'){
                        own = true;
                        group = atoi(first.substr(1).c_str());
                    } else {
                        group = atoi(first.c_str());
                    }
                    controller->getHit().guardHitSound.own = own;
                    controller->getHit().guardHitSound.group = group;
                    controller->getHit().guardHitSound.item = item;
                } else if (simple == "ground.type"){
                    string type;
                    simple >> type;
                    type = Util::fixCase(type);
                    if (type == "low"){
                        controller->getHit().groundType = AttackType::Low;
                    } else if (type == "high"){
                        controller->getHit().groundType = AttackType::High;
                    } else if (type == "trip"){
                        controller->getHit().groundType = AttackType::Trip;
                    }
                } else if (simple == "air.type"){
                    string type;
                    simple >> type;
                    type = Util::fixCase(type);
                    if (type == "low"){
                        controller->getHit().airType = AttackType::Low;
                    } else if (type == "high"){
                        controller->getHit().airType = AttackType::High;
                    } else if (type == "trip"){
                        controller->getHit().airType = AttackType::Trip;
                    }
                } else if (simple == "ground.slidetime"){
                    controller->getHit().groundSlideTime = Compiler::compile(simple.getValue());
                } else if (simple == "guard.slidetime"){
                    simple >> controller->getHit().guardSlideTime;
                } else if (simple == "ground.hittime"){
                    simple >> controller->getHit().groundHitTime;
                } else if (simple == "guard.hittime"){
                    simple >> controller->getHit().guardGroundHitTime;
                } else if (simple == "air.hittime"){
                    simple >> controller->getHit().airHitTime;
                } else if (simple == "guard.ctrltime"){
                    simple >> controller->getHit().guardControlTime;
                } else if (simple == "guard.dist"){
                    simple >> controller->getHit().guardDistance;
                } else if (simple == "yaccel"){
                    simple >> controller->getHit().yAcceleration;
                } else if (simple == "ground.velocity"){
                    if (simple.getValue()->hasMultiple()){
                        try{
                            simple >> controller->getHit().groundVelocity.x;
                            simple >> controller->getHit().groundVelocity.y;
                        } catch (const Ast::Exception & e){
                        }
                    } else {
                            simple >> controller->getHit().groundVelocity.x;
                    }
                } else if (simple == "guard.velocity"){
                    simple >> controller->getHit().guardVelocity;
                } else if (simple == "air.velocity"){
                    if (simple.getValue()->hasMultiple()){
                        try{
                            simple >> controller->getHit().airVelocity.x;
                            simple >> controller->getHit().airVelocity.y;
                        } catch (const Ast::Exception & e){
                        }
                    } else {
                        simple >> controller->getHit().airVelocity.x;
                    }
                } else if (simple == "airguard.velocity"){
                    try{
                        simple >> controller->getHit().airGuardVelocity.x;
                        simple >> controller->getHit().airGuardVelocity.y;
                    } catch (const Ast::Exception & e){
                    }
                } else if (simple == "ground.cornerpush.veloff"){
                    controller->getHit().groundCornerPushoff = Compiler::compile(simple.getValue());
                } else if (simple == "air.cornerpush.veloff"){
                    simple >> controller->getHit().airCornerPushoff;
                } else if (simple == "down.cornerpush.veloff"){
                    simple >> controller->getHit().downCornerPushoff;
                } else if (simple == "guard.cornerpush.veloff"){
                    simple >> controller->getHit().guardCornerPushoff;
                } else if (simple == "airguard.cornerpush.veloff"){
                    simple >> controller->getHit().airGuardCornerPushoff;
                } else if (simple == "airguard.ctrltime"){
                    simple >> controller->getHit().airGuardControlTime;
                } else if (simple == "air.juggle"){
                    simple >> controller->getHit().airJuggle;
                } else if (simple == "guardsound"){
                    try{
                        /* FIXME: parse a string and then determine if its S# or just # */
                        simple >> controller->getHit().guardHitSound.group;
                        simple >> controller->getHit().guardHitSound.item;
                    } catch (const Ast::Exception & e){
                    }
                } else if (simple == "mindist"){
                    simple >> controller->getHit().minimum.x;
                    simple >> controller->getHit().minimum.y;
                } else if (simple == "maxdist"){
                    simple >> controller->getHit().maximum.x;
                    simple >> controller->getHit().maximum.y;
                } else if (simple == "snap"){
                    simple >> controller->getHit().snap.x;
                    simple >> controller->getHit().snap.y;
                } else if (simple == "p1sprpriority"){
                    controller->getHit().player1SpritePriority = Compiler::compile(simple.getValue());
                } else if (simple == "p2sprpriority"){
                    simple >> controller->getHit().player2SpritePriority;
                } else if (simple == "p1facing"){
                    controller->getHit().player1Facing = Compiler::compile(simple.getValue());
                } else if (simple == "p2facing"){
                    controller->getHit().player2Facing = Compiler::compile(simple.getValue());
                } else if (simple == "p1getp2facing"){
                    controller->getHit().player1GetPlayer2Facing = Compiler::compile(simple.getValue());
                } else if (simple == "player2Facing"){
                    controller->getHit().player2Facing = Compiler::compile(simple.getValue());
                } else if (simple == "p1stateno"){
                    controller->getHit().player1State = Compiler::compile(simple.getValue());
                } else if (simple == "p2stateno"){
                    controller->getHit().player2State = Compiler::compile(simple.getValue());
                } else if (simple == "p2getp1state"){
                    controller->getHit().player2GetPlayer1State = Compiler::compile(simple.getValue());
                } else if (simple == "forcestand"){
                    simple >> controller->getHit().forceStand;
                } else if (simple == "fall"){
                    controller->getHit().fall.fall = Compiler::compile(simple.getValue());
                } else if (simple == "fall.xvelocity"){
                    simple >> controller->getHit().fall.xVelocity;
                } else if (simple == "fall.yvelocity"){
                    simple >> controller->getHit().fall.yVelocity;
                } else if (simple == "fall.recover"){
                    controller->getHit().fall.recover = Compiler::compile(simple.getValue());
                } else if (simple == "fall.recovertime"){
                    simple >> controller->getHit().fall.recoverTime;
                } else if (simple == "fall.damage"){
                    controller->getHit().fall.damage = Compiler::compile(simple.getValue());
                } else if (simple == "air.fall"){
                    simple >> controller->getHit().fall.airFall;
                } else if (simple == "forcenofall"){
                    simple >> controller->getHit().fall.forceNoFall;
                // } else if (simple == "waveform"){
                    /* FIXME */
                } else {
                    Global::debug(0) << "Unhandled state controller '" << controller->getName() << "' attribute: " << simple.toString() << endl;
                }
            }
    };
#endif

    class StateControllerWalker: public Ast::Walker {
    public:
        StateControllerWalker():
        type(StateController::Unknown){
        }

        StateController::Type type;

        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
            if (simple == "type"){
                string type;
                simple >> type;
                type = Mugen::Util::fixCase(type);
                map<string, StateController::Type> types;
                types["afterimage"] = StateController::AfterImage;
                types["afterimagetime"] = StateController::AfterImageTime;
                types["allpalfx"] = StateController::AllPalFX;
                types["angleadd"] = StateController::AngleAdd;
                types["angledraw"] = StateController::AngleDraw;
                types["anglemul"] = StateController::AngleMul;
                types["angleset"] = StateController::AngleSet;
                types["appendtoclipboard"] = StateController::AppendToClipboard;
                types["assertspecial"] = StateController::AssertSpecial;
                types["attackdist"] = StateController::AttackDist;
                types["attackmulset"] = StateController::AttackMulSet;
                types["bgpalfx"] = StateController::BGPalFX;
                types["bindtoparent"] = StateController::BindToParent;
                types["bindtoroot"] = StateController::BindToRoot;
                types["bindtotarget"] = StateController::BindToTarget;
                types["changeanim"] = StateController::ChangeAnim;
                types["changeanim2"] = StateController::ChangeAnim2;
                types["changestate"] = StateController::ChangeState;
                types["clearclipboard"] = StateController::ClearClipboard;
                types["ctrlset"] = StateController::CtrlSet;
                types["defencemulset"] = StateController::DefenceMulSet;
                types["destroyself"] = StateController::DestroySelf;
                types["displaytoclipboard"] = StateController::DisplayToClipboard;
                types["envcolor"] = StateController::EnvColor;
                types["envshake"] = StateController::EnvShake;
                types["explod"] = StateController::Explod;
                types["explodbindtime"] = StateController::ExplodBindTime;
                types["forcefeedback"] = StateController::ForceFeedback;
                types["fallenvshake"] = StateController::FallEnvShake;
                types["gamemakeanim"] = StateController::GameMakeAnim;
                types["gravity"] = StateController::Gravity;
                types["helper"] = StateController::Helper;
                types["hitadd"] = StateController::HitAdd;
                types["hitby"] = StateController::HitBy;
                types["hitdef"] = StateController::HitDef;
                types["hitfalldamage"] = StateController::HitFallDamage;
                types["hitfallset"] = StateController::HitFallSet;
                types["hitfallvel"] = StateController::HitFallVel;
                types["hitoverride"] = StateController::HitOverride;
                types["hitvelset"] = StateController::HitVelSet;
                types["lifeadd"] = StateController::LifeAdd;
                types["lifeset"] = StateController::LifeSet;
                types["makedust"] = StateController::MakeDust;
                types["modifyexplod"] = StateController::ModifyExplod;
                types["movehitreset"] = StateController::MoveHitReset;
                types["nothitby"] = StateController::NotHitBy;
                types["null"] = StateController::Null;
                types["offset"] = StateController::Offset;
                types["palfx"] = StateController::PalFX;
                types["parentvaradd"] = StateController::ParentVarAdd;
                types["parentvarset"] = StateController::ParentVarSet;
                types["pause"] = StateController::Pause;
                types["playerpush"] = StateController::PlayerPush;
                types["playsnd"] = StateController::PlaySnd;
                types["posadd"] = StateController::PosAdd;
                types["posfreeze"] = StateController::PosFreeze;
                types["posset"] = StateController::PosSet;
                types["poweradd"] = StateController::PowerAdd;
                types["powerset"] = StateController::PowerSet;
                types["projectile"] = StateController::Projectile;
                types["removeexplod"] = StateController::RemoveExplod;
                types["reversaldef"] = StateController::ReversalDef;
                types["screenbound"] = StateController::ScreenBound;
                types["selfstate"] = StateController::SelfState;
                types["sprpriority"] = StateController::SprPriority;
                types["statetypeset"] = StateController::StateTypeSet;
                types["sndpan"] = StateController::SndPan;
                types["stopsnd"] = StateController::StopSnd;
                types["superpause"] = StateController::SuperPause;
                types["targetbind"] = StateController::TargetBind;
                types["targetdrop"] = StateController::TargetDrop;
                types["targetfacing"] = StateController::TargetFacing;
                types["targetlifeadd"] = StateController::TargetLifeAdd;
                types["targetpoweradd"] = StateController::TargetPowerAdd;
                types["targetstate"] = StateController::TargetState;
                types["targetveladd"] = StateController::TargetVelAdd;
                types["targetvelset"] = StateController::TargetVelSet;
                types["trans"] = StateController::Trans;
                types["turn"] = StateController::Turn;
                types["varadd"] = StateController::VarAdd;
                types["varrandom"] = StateController::VarRandom;
                types["varrangeset"] = StateController::VarRangeSet;
                types["varset"] = StateController::VarSet;
                types["veladd"] = StateController::VelAdd;
                types["velmul"] = StateController::VelMul;
                types["velset"] = StateController::VelSet;
                types["width"] = StateController::Width;

                if (types.find(type) != types.end()){
                    map<string, StateController::Type>::iterator what = types.find(type);
                    this->type = (*what).second;
                } else {
                    Global::debug(0) << "Unknown state controller type " << type << endl;
                }
            }
        }
    };

    if (states[state] == 0){
        ostringstream out;
        out << "Warning! No StateDef for state " << state << " [" << name << "]";
        // delete controller;
        // throw MugenException(out.str());
    } else {
        // StateController * controller = new StateController(name);
        StateControllerWalker walker;
        section->walk(walker);
        StateController::Type type = walker.type;
        if (type == StateController::Unknown){
            Global::debug(0) << "Warning: no type given for controller " << section->getName() << endl;
        } else {
            StateController * controller = compileStateController(section, name, state, type);
            // controller->compile();
            states[state]->addController(controller);
            Global::debug(1) << "Adding state controller '" << name << "' to state " << state << endl;
        }
    }
}

static Filesystem::AbsolutePath findStateFile(const Filesystem::AbsolutePath & base, const string & path){
    if (PaintownUtil::exists(base.join(Filesystem::RelativePath(path)).path())){
        return base.join(Filesystem::RelativePath(path));
    } else {
        return Filesystem::find(Filesystem::RelativePath("mugen/data/" + path));
    }

#if 0
    try{
        /* first look in the character's directory */
        // return Filesystem::find(base.join(Filesystem::RelativePath(path)));
        
    } catch (const Filesystem::NotFound & f){
        /* then look for it in the common data directory.
         * this is where common1.cns lives
         */
        return Filesystem::find(Filesystem::RelativePath("mugen/data/" + path));
    }
#endif
}

void Character::loadStateFile(const Filesystem::AbsolutePath & base, const string & path, bool allowDefinitions, bool allowStates){
    Filesystem::AbsolutePath full = findStateFile(base, path);
    // string full = Filesystem::find(base + "/" + PaintownUtil::trim(path));
    /* st can use the Cmd parser */
    Ast::AstParse parsed((list<Ast::Section*>*) ParseCache::parseCmd(full.path()));
    for (Ast::AstParse::section_iterator section_it = parsed.getSections()->begin(); section_it != parsed.getSections()->end(); section_it++){
        Ast::Section * section = *section_it;
        std::string head = section->getName();
        head = Util::fixCase(head);
        if (allowDefinitions && PaintownUtil::matchRegex(head, "statedef")){
            parseStateDefinition(section);
        } else if (allowStates && PaintownUtil::matchRegex(head, "state ")){
            parseState(section);
        }
    }
}

/* a container for a directory and a file */
struct Location{
    Location(Filesystem::AbsolutePath base, string file):
        base(base), file(file){
        }

    Filesystem::AbsolutePath base;
    string file;
};

void Character::load(int useAct){
#if 0
    // Lets look for our def since some people think that all file systems are case insensitive
    baseDir = Filesystem::find("mugen/chars/" + location + "/");
    Global::debug(1) << baseDir << endl;
    std::string realstr = Mugen::Util::stripDir(location);
    const std::string ourDefFile = Mugen::Util::fixFileName(baseDir, std::string(realstr + ".def"));
    
    if (ourDefFile.empty()){
        throw MugenException( "Cannot locate player definition file for: " + location );
    }
#endif
    
    // baseDir = Filesystem::cleanse(Mugen::Util::getFileDir(location));
    baseDir = location.getDirectory();
    // const std::string ourDefFile = location;
     
    Ast::AstParse parsed(Util::parseDef(location.path()));
    try{
        /* Extract info for our first section of our stage */
        for (Ast::AstParse::section_iterator section_it = parsed.getSections()->begin(); section_it != parsed.getSections()->end(); section_it++){
            Ast::Section * section = *section_it;
            std::string head = section->getName();
            /* this should really be head = Mugen::Util::fixCase(head) */
            head = Mugen::Util::fixCase(head);

            if (head == "info"){
                class InfoWalker: public Ast::Walker {
                    public:
                        InfoWalker(Character & who):
                            self(who){
                            }

                        Character & self;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "name"){
                                simple >> self.name;
                            } else if (simple == "displayname"){
                                simple >> self.displayName;
                            } else if (simple == "versiondate"){
                                simple >> self.versionDate;
                            } else if (simple == "mugenversion"){
                                simple >> self.mugenVersion;
                            } else if (simple == "author"){
                                simple >> self.author;
                            } else if (simple == "pal.defaults"){
                                vector<int> numbers;
                                simple >> numbers;
                                for (vector<int>::iterator it = numbers.begin(); it != numbers.end(); it++){
                                    self.palDefaults.push_back((*it) - 1);
                                }
                                // Global::debug(1) << "Pal" << self.palDefaults.size() << ": " << num << endl;
                            } else throw MugenException("Unhandled option in Info Section: " + simple.toString());
                        }
                };

                InfoWalker walker(*this);
                Ast::Section * section = *section_it;
                section->walk(walker);
            } else if (head == "files"){
                class FilesWalker: public Ast::Walker {
                    public:
                        FilesWalker(Character & self, const Filesystem::AbsolutePath & location):
                            location(location),
                            self(self){
                            }

                        vector<Location> stateFiles;
                        const Filesystem::AbsolutePath & location;

                        Character & self;
                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "cmd"){
                                string file;
                                simple >> file;
                                self.cmdFile = Filesystem::RelativePath(file);
                                /* loaded later after the state files */
                            } else if (simple == "cns"){
                                string file;
                                simple >> file;
                                /* just loads the constants */
                                self.loadCnsFile(Filesystem::RelativePath(file));
                            } else if (PaintownUtil::matchRegex(simple.idString(), "st[0-9]+")){
                                int num = atoi(PaintownUtil::captureRegex(simple.idString(), "st([0-9]+)", 0).c_str());
                                if (num >= 0 && num <= 12){
                                    string path;
                                    simple >> path;
                                    stateFiles.push_back(Location(self.baseDir, path));
                                    // simple >> self.stFile[num];
                                }
                            } else if (simple == "stcommon"){
                                string path;
                                simple >> path;
                                stateFiles.insert(stateFiles.begin(), Location(self.baseDir, path));
                            } else if (simple == "st"){
                                string path;
                                simple >> path;
                                stateFiles.push_back(Location(self.baseDir, path));
                            } else if (simple == "sprite"){
                                simple >> self.sffFile;
                            } else if (simple == "anim"){
                                simple >> self.airFile;
                            } else if (simple == "sound"){
                                simple >> self.sndFile;
                                // Mugen::Util::readSounds(Mugen::Util::fixFileName(self.baseDir, self.sndFile), self.sounds);
                                Util::readSounds(self.baseDir.join(Filesystem::RelativePath(self.sndFile)), self.sounds);
                            } else if (PaintownUtil::matchRegex(simple.idString(), "pal[0-9]+")){
                                int num = atoi(PaintownUtil::captureRegex(simple.idString(), "pal([0-9]+)", 0).c_str());
                                string what;
                                simple >> what;
                                self.palFile[num] = what;
                            } else {
                                Global::debug(0) << "Unhandled option in Files Section: " + simple.toString() << endl;
                            }
                        }
                };

                FilesWalker walker(*this, location);
                Ast::Section * section = *section_it;
                section->walk(walker);

                for (vector<Location>::iterator it = walker.stateFiles.begin(); it != walker.stateFiles.end(); it++){
                    Location & where = *it;
                    try{
                        /* load definitions first */
                        loadStateFile(where.base, where.file, true, false);
                    } catch (const MugenException & e){
                        ostringstream out;
                        out << "Problem loading state file " << where.file << ": " << e.getReason();
                        throw MugenException(out.str());
                    } catch (const Mugen::Cmd::ParseException & e){
                        ostringstream out;
                        out << "Problem loading state file " << where.file << ": " << e.getReason();
                        throw MugenException(out.str());
                    }
                }
                        
                for (vector<Location>::iterator it = walker.stateFiles.begin(); it != walker.stateFiles.end(); it++){
                    Location & where = *it;
                    try{
                        /* then load controllers */
                        loadStateFile(where.base, where.file, false, true);
                    } catch (const MugenException & e){
                        ostringstream out;
                        out << "Problem loading state file " << where.file << ": " << e.getReason();
                        throw MugenException(out.str());
                    } catch (const Mugen::Cmd::ParseException & e){
                        ostringstream out;
                        out << "Problem loading state file " << where.file << ": " << e.getReason();
                        throw MugenException(out.str());
                    }
                }

                loadCmdFile(cmdFile);

#if 0
                /* now just load the state controllers */
                for (vector<Location>::iterator it = walker.stateFiles.begin(); it != walker.stateFiles.end(); it++){
                    Location & where = *it;
                    try{
                        loadStateFile(where.base, where.file, false, true);
                    } catch (const MugenException & e){
                        ostringstream out;
                        out << "Problem loading state file " << where.file << ": " << e.getReason();
                        throw MugenException(out.str());
                    }
                }
#endif

                /*
                   if (commonStateFile != ""){
                   loadStateFile("mugen/data/", commonStateFile);
                   }
                   if (stateFile != ""){
                   loadStateFile("mugen/chars/" + location, stateFile);
                   }
                   if (
                   */

            } else if (head == "arcade"){
                class ArcadeWalker: public Ast::Walker {
                    public:
                        ArcadeWalker(Character & self):
                            self(self){
                            }

                        Character & self;

                        virtual void onAttributeSimple(const Ast::AttributeSimple & simple){
                            if (simple == "intro.storyboard"){
                                simple >> self.introFile;
                            } else if (simple == "ending.storyboard"){
                                simple >> self.endingFile;
                            } else {
                                throw MugenException("Unhandled option in Arcade Section: " + simple.toString());
                            }
                        }
                };

                ArcadeWalker walker(*this);
                Ast::Section * section = *section_it;
                section->walk(walker);
            }
        }
    } catch (const Ast::Exception & e){
        ostringstream out;
        out << "Could not load " << location.path() << ": " << e.getReason();
        throw MugenException(out.str());
    }

    /* Is this just for testing? */
    if (getMaxHealth() == 0 || getHealth() == 0){
        setHealth(1000);
        setMaxHealth(1000);
    }

    // Current palette
    if (palDefaults.empty()){
	// Correct the palette defaults
	for (unsigned int i = 0; i < palFile.size(); ++i){
	    palDefaults.push_back(i);
	}
    }
    /*
    if (palDefaults.size() < palFile.size()){
	bool setPals[palFile.size()];
	for( unsigned int i =0;i<palFile.size();++i){
	    setPals[i] = false;
	}
	// Set the ones already set
	for (unsigned int i = 0; i < palDefaults.size(); ++i){
	    setPals[palDefaults[i]] = true;
	}
	// now add the others
	for( unsigned int i =0;i<palFile.size();++i){
	    if(!setPals[i]){
		palDefaults.push_back(i);
	    }
	}
    }
    */
    std::string paletteFile = "";
    currentPalette = useAct;
    if (palFile.find(currentPalette) == palFile.end()){
        /* FIXME: choose a default. its not just palette 1 because that palette
         * might not exist
         */
	Global::debug(1) << "Couldn't find palette: " << currentPalette << " in palette collection. Defaulting to internal palette if available." << endl;
        paletteFile = palFile.begin()->second;
    } else {
	paletteFile = palFile[currentPalette];
	Global::debug(1) << "Current pal: " << currentPalette << " | Palette File: " << paletteFile << endl;
    }
    /*
    if (currentPalette > palFile.size() - 1){
        currentPalette = 1;
    }
    */
    Global::debug(1) << "Reading Sff (sprite) Data..." << endl; 
    /* Sprites */
    // Mugen::Util::readSprites( Mugen::Util::fixFileName(baseDir, sffFile), Mugen::Util::fixFileName(baseDir, paletteFile), sprites);
    Util::readSprites(baseDir.join(Filesystem::RelativePath(sffFile)), baseDir.join(Filesystem::RelativePath(paletteFile)), sprites);
    Global::debug(1) << "Reading Air (animation) Data..." << endl;
    /* Animations */
    // animations = Mugen::Util::loadAnimations(Mugen::Util::fixFileName(baseDir, airFile), sprites);
    animations = Util::loadAnimations(baseDir.join(Filesystem::RelativePath(airFile)), sprites);

    fixAssumptions();

    /*
    State * state = states[-1];
    for (vector<StateController*>::const_iterator it = state->getControllers().begin(); it != state->getControllers().end(); it++){
        Global::debug(0) << "State -1: '" << (*it)->getName() << "'" << endl;
    }
    */
}
        
bool Character::hasAnimation(int index) const {
    typedef std::map< int, MugenAnimation * > Animations;
    Animations::const_iterator it = getAnimations().find(index);
    return it != getAnimations().end();
}

/* completely arbitrary number, just has to be unique and unlikely to
 * be used by the system. maybe negative numbers are better?
 */
static const int JumpIndex = 234823;

class MutableCompiledInteger: public Compiler::Value {
public:
    MutableCompiledInteger(int value):
        value(value){
        }

    int value;

    virtual RuntimeValue evaluate(const Environment & environment) const {
        return RuntimeValue(value);
    }

    virtual void set(int value){
        this->value = value;
    }

    virtual int get() const {
        return this->value;
    }

    virtual ~MutableCompiledInteger(){
    }
};

void Character::resetJump(MugenStage & stage, const vector<string> & inputs){
    MutableCompiledInteger * number = (MutableCompiledInteger*) getSystemVariable(JumpIndex);
    number->set(0);
    changeState(stage, JumpStart, inputs);
}

void Character::doubleJump(MugenStage & stage, const vector<string> & inputs){
    MutableCompiledInteger * number = (MutableCompiledInteger*) getSystemVariable(JumpIndex);
    number->set(number->get() + 1);
    changeState(stage, AirJumpStart, inputs);
}

void Character::stopGuarding(MugenStage & stage, const vector<string> & inputs){
    guarding = false;
    if (stateType == StateType::Crouch){
        changeState(stage, Crouching, inputs);
    } else if (stateType == StateType::Air){
        changeState(stage, 52, inputs);
    } else {
        changeState(stage, Standing, inputs);
    }
}

static StateController * parseController(const string & input, const string & name, int state, StateController::Type type){
    try{
        list<Ast::Section*>* sections = (list<Ast::Section*>*) Mugen::Cmd::parse(input.c_str());
        if (sections->size() == 0){
            ostringstream out;
            out << "Could not parse controller: " << input;
            throw MugenException(out.str());
        }
        Ast::Section * first = sections->front();
        return compileStateController(first, name, state, type);
    } catch (const Ast::Exception & e){
        throw MugenException(e.getReason());
    } catch (const Mugen::Cmd::ParseException & e){
        ostringstream out;
        out << "Could not parse " << input << " because " << e.getReason();
        throw MugenException(out.str());
    }
}

void Character::fixAssumptions(){
    /* need a -1 state controller that changes to state 20 if holdfwd
     * or holdback is pressed
     */

    if (states[-1] != 0){
        /* walk */
        {
            ostringstream raw;
            raw << "[State -1, paintown-internal-walk]\n";
            raw << "triggerall = stateno = 0\n";
            raw << "trigger1 = command = \"holdfwd\"\n";
            raw << "trigger2 = command = \"holdback\"\n";
            raw << "value = " << WalkingForwards << "\n";
            states[-1]->addController(parseController(raw.str(), "paintown internal walk", -1, StateController::ChangeState));

            /*
            StateController * controller = new StateController("walk");
            controller->setType(StateController::ChangeState);
            Ast::Number value(WalkingForwards);
            controller->setValue(&value);
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("stateno"),
                        new Ast::Number(0))));
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::SimpleIdentifier("ctrl")));
            controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("command"),
                        new Ast::String(new string("holdfwd")))));
            controller->addTrigger(2, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("command"),
                        new Ast::String(new string("holdback")))));
            states[-1]->addController(controller);
            controller->compile();
            */
        }


        /* crouch */
        {
            ostringstream raw;
            raw << "[State -1, paintown-internal-crouch]\n";
            raw << "value = " << StandToCrouch << "\n";
            raw << "triggerall = ctrl\n";
            raw << "trigger1 = stateno = 0\n";
            raw << "trigger2 = stateno = " << WalkingForwards << "\n";
            raw << "triggerall = command = \"holddown\"\n";

            states[-1]->addControllerFront(parseController(raw.str(), "paintown internal crouch", -1, StateController::ChangeState));
            /*
            StateController * controller = new StateController("crouch");
            controller->setType(StateController::ChangeState);
            Ast::Number value(StandToCrouch);
            controller->setValue(&value);
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::SimpleIdentifier("ctrl")));
            controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("stateno"),
                        new Ast::Number(0))));
            controller->addTrigger(2, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("stateno"),
                        new Ast::Number(WalkingForwards))));
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("command"),
                        new Ast::String(new string("holddown")))));
            states[-1]->addControllerFront(controller);
            controller->compile();
            */
        }

        /* jump */
        {
            class InternalJumpController: public StateController {
            public:
                InternalJumpController():
                StateController("jump"){
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    guy.resetJump(stage, commands);
                }
            };

            InternalJumpController * controller = new InternalJumpController();
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::SimpleIdentifier("ctrl")));
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("statetype"),
                        new Ast::String(new string("S")))));
            controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("command"),
                        new Ast::String(new string("holdup")))));
            states[-1]->addController(controller);

            /*
            StateController * controller = new StateController("jump");
            controller->setType(StateController::InternalCommand);
            controller->setInternal(&Mugen::Character::resetJump);
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::SimpleIdentifier("ctrl")));
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("statetype"),
                        new Ast::String(new string("S")))));
            controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("command"),
                        new Ast::String(new string("holdup")))));
            states[-1]->addController(controller);
            controller->compile();
            */
        }

        /* double jump */
        {
            string jumpCommand = "internal:double-jump-command";
            vector<Ast::Key*> keys;
            keys.push_back(new Ast::KeyModifier(Ast::KeyModifier::Release, new Ast::KeySingle("U")));
            keys.push_back(new Ast::KeySingle("U"));
            Command * doubleJumpCommand = new Command(jumpCommand, new Ast::KeyList(keys), 5, 0);
            addCommand(doubleJumpCommand);

            internalJumpNumber = new MutableCompiledInteger(0);
            setSystemVariable(JumpIndex, internalJumpNumber);

            class InternalDoubleJumpController: public StateController {
            public:
                InternalDoubleJumpController():
                StateController("double jump"){
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    guy.doubleJump(stage, commands);
                }
            };

            InternalDoubleJumpController * controller = new InternalDoubleJumpController();
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::SimpleIdentifier("ctrl")));
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("statetype"),
                        new Ast::String(new string("A")))));
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::GreaterThan,
                                                                                          new Ast::ExpressionUnary(Ast::ExpressionUnary::Minus,
                                                                                                                   new Ast::Keyword("pos y")),
                                                                                          new Ast::SimpleIdentifier("internal:airjump-height"))));
            controller->addTriggerAll(Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::LessThan,
                        new Ast::Function("sysvar", new Ast::ValueList(new Ast::Number(JumpIndex))),
                        new Ast::SimpleIdentifier("internal:extra-jumps"))));
            controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                        new Ast::SimpleIdentifier("command"),
                        // new Ast::String(new string("holdup")
                        new Ast::String(new string(jumpCommand)
                            ))));
            states[-1]->addController(controller);
        }
    }

    {
        if (states[StopGuardStand] != 0){
            class StopGuardStandController: public StateController {
            public:
                StopGuardStandController():
                StateController("stop guarding"){
                }

                virtual void activate(MugenStage & stage, Character & guy, const vector<string> & commands) const {
                    guy.stopGuarding(stage, commands);
                }
            };

            StopGuardStandController * controller = new StopGuardStandController();
            controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                    new Ast::SimpleIdentifier("animtime"),
                    new Ast::Number(0))));
            states[StopGuardStand]->addController(controller);
        }
    }

    /* need a 20 state controller that changes to state 0 if holdfwd
     * or holdback is not pressed
     */
    if (states[20] != 0){
        ostringstream raw;
        raw << "[State 20, paintown-internal-stop-walking]\n";
        raw << "value = " << Standing << "\n";
        raw << "trigger1 = command != \"holdfwd\"\n";
        raw << "trigger1 = command != \"holdback\"\n";

        states[20]->addController(parseController(raw.str(), "paintown internal stop walking", 20, StateController::ChangeState));

        /*
        StateController * controller = new StateController("stop walking");
        controller->setType(StateController::ChangeState);
        Ast::Number value(Standing);
        controller->setValue(&value);
        controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Unequals,
                    new Ast::SimpleIdentifier("command"),
                    new Ast::String(new string("holdfwd")))));
        controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Unequals,
                    new Ast::SimpleIdentifier("command"),
                    new Ast::String(new string("holdback")))));
        states[20]->addController(controller);
        controller->compile();
        */
    }

    if (states[Standing] != 0){
        states[Standing]->setControl(Compiler::compile(1));
    }

    /* stand after crouching */
    if (states[11] != 0){
        ostringstream raw;
        raw << "[State 11, paintown-internal-stand-after-crouching]\n";
        raw << "value = " << CrouchToStand << "\n";
        raw << "trigger1 = command != \"holddown\"\n";

        states[11]->addController(parseController(raw.str(), "stand after crouching", 11, StateController::ChangeState));

        /*
        StateController * controller = new StateController("stop walking");
        controller->setType(StateController::ChangeState);
        Ast::Number value(CrouchToStand);
        controller->setValue(&value);
        controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Unequals,
                    new Ast::SimpleIdentifier("command"),
                    new Ast::String(new string("holddown")))));
        states[11]->addController(controller);
        controller->compile();
        */
    }

    /* get up kids */
    if (states[Liedown] != 0){
        ostringstream raw;
        raw << "[State " << Liedown << ", paintown-internal-get-up]\n";
        raw << "value = " << GetUpFromLiedown << "\n";
        raw << "trigger1 = time >= " << getLieDownTime() << "\n";

        states[Liedown]->addController(parseController(raw.str(), "get up", Liedown, StateController::ChangeState));

        /*
        StateController * controller = new StateController("get up");
        controller->setType(StateController::ChangeState);
        Ast::Number value(GetUpFromLiedown);
        controller->setValue(&value);

        controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::GreaterThanEquals,
                    new Ast::SimpleIdentifier("time"),
                    new Ast::Number(getLieDownTime()))));

        states[Liedown]->addController(controller);
        controller->compile();
        */
    }

    /* standing turn state */
    {
        State * turn = new State();
        turn->setType(State::Unchanged);
        turn->setAnimation(Compiler::compile(5));
        states[5] = turn;

        ostringstream raw;
        raw << "[State 5, paintown-internal-turn]\n";
        raw << "value = " << Standing << "\n";
        raw << "trigger1 = animtime = 0\n";

        turn->addController(parseController(raw.str(), "turn", 5, StateController::ChangeState));

        /*
        StateController * controller = new StateController("stand");
        controller->setType(StateController::ChangeState);
        Ast::Number value(Standing);
        controller->setValue(&value);
        controller->addTrigger(1, Compiler::compileAndDelete(new Ast::ExpressionInfix(Ast::ExpressionInfix::Equals,
                    new Ast::SimpleIdentifier("animtime"),
                    new Ast::Number(0))));
        turn->addController(controller);
        controller->compile();
        */
    }

#if 0
    /* if y reaches 0 then auto-transition to state 52.
     * probably just add a trigger to state 50
     */
    if (states[50] != 0){
        StateController * controller = new StateController("jump land");
        controller->setType(StateController::ChangeState);
        controller->setValue1(new Ast::Number(52));
        controller->addTrigger(1, new Ast::ExpressionInfix(Ast::ExpressionInfix::GreaterThanEquals,
                    new Ast::Keyword("pos y"),
                    new Ast::Number(0)));
        controller->addTrigger(1, new Ast::ExpressionInfix(Ast::ExpressionInfix::GreaterThan,
                    new Ast::Keyword("vel y"),
                    new Ast::Number(0)));
        states[50]->addController(controller);

    }
#endif
}

// Render sprite
void Character::renderSprite(const int x, const int y, const unsigned int group, const unsigned int image, Bitmap *bmp , const int flip, const double scalex, const double scaley ){
    MugenSprite *sprite = sprites[group][image];
    if (sprite){
	Bitmap *bitmap = sprite->getBitmap();//bitmaps[group][image];
	/*if (!bitmap){
	    bitmap = new Bitmap(Bitmap::memoryPCX((unsigned char*) sprite->pcx, sprite->newlength));
	    bitmaps[group][image] = bitmap;
	}*/
	const int width = (int)(bitmap->getWidth() * scalex);
	const int height =(int)(bitmap->getHeight() * scaley);
	if (flip == 1){
	    bitmap->drawStretched(x,y, width, height, *bmp);
	} else if (flip == -1){
	    // temp bitmap to flip and crap
	    Bitmap temp = Bitmap::temporaryBitmap(bitmap->getWidth(), bitmap->getHeight());
	    temp.fill(Bitmap::MaskColor());
	    bitmap->drawHFlip(0,0,temp);
	    temp.drawStretched(x-width,y, width, height, *bmp);
	}
    }
}
        
bool Character::canRecover() const {
    /* FIXME */
    return true;
    // return getY() == 0;
}

void Character::nextPalette(){
    if (currentPalette < palDefaults.size()-1){
	currentPalette++;
    } else currentPalette = 0;
    Global::debug(1) << "Current pal: " << currentPalette << " | Location: " << palDefaults[currentPalette] << " | Palette File: " << palFile[palDefaults[currentPalette]] << endl;
   /*
    // Now replace the palettes
    unsigned char pal[768];
    if (Mugen::Util::readPalette(Mugen::Util::fixFileName(baseDir, palFile[palDefaults[currentPalette]]),pal)){
	for( std::map< unsigned int, std::map< unsigned int, MugenSprite * > >::iterator i = sprites.begin() ; i != sprites.end() ; ++i ){
	    for( std::map< unsigned int, MugenSprite * >::iterator j = i->second.begin() ; j != i->second.end() ; ++j ){
		if( j->second ){
		    MugenSprite *sprite = j->second;
		    if ( sprite->samePalette){
			memcpy( sprite->pcx + (sprite->reallength), pal, 768);
		    } else {
			if (!(sprite->groupNumber == 9000 && sprite->imageNumber == 1)){
			    memcpy( sprite->pcx + (sprite->reallength)-768, pal, 768);
			} 
		    }
		}
	    }
	}
	// reload with new palette
	for( std::map< int, MugenAnimation * >::iterator i = animations.begin() ; i != animations.end() ; ++i ){
	    if( i->second )i->second->reloadBitmaps();
	}
    }
    */
}

void Character::priorPalette(){
    if (currentPalette > 0){
	currentPalette--;
    } else currentPalette = palDefaults.size() -1;
    Global::debug(1) << "Current pal: " << currentPalette << " | Palette File: " << palFile[palDefaults[currentPalette]] << endl;
    // Now replace the palettes
    /*unsigned char pal[768];
    if (Mugen::Util::readPalette(Mugen::Util::fixFileName(baseDir, palFile[palDefaults[currentPalette]]),pal)){
	for( std::map< unsigned int, std::map< unsigned int, MugenSprite * > >::iterator i = sprites.begin() ; i != sprites.end() ; ++i ){
	    for( std::map< unsigned int, MugenSprite * >::iterator j = i->second.begin() ; j != i->second.end() ; ++j ){
		if( j->second ){
		    MugenSprite *sprite = j->second;
		    if ( sprite->samePalette){
			memcpy( sprite->pcx + (sprite->reallength), pal, 768);
		    } else {
			if (!(sprite->groupNumber == 9000 && sprite->imageNumber == 1)){
			    memcpy( sprite->pcx + (sprite->reallength)-768, pal, 768);
			} 
		    }
		}
	    }
	}
	// Get rid of animation lists;
	for( std::map< int, MugenAnimation * >::iterator i = animations.begin() ; i != animations.end() ; ++i ){
	    if( i->second )i->second->reloadBitmaps();
	}
    }*/
}

const Bitmap * Character::getCurrentFrame() const {
    return getCurrentAnimation()->getCurrentFrame()->getSprite()->getBitmap();
}

void Character::drawReflection(Bitmap * work, int rel_x, int rel_y, int intensity){
    getCurrentAnimation()->renderReflection(getFacing() == Object::FACING_LEFT, true, intensity, getRX() - rel_x, (int)(getZ() + getY() - rel_y), *work);
}

MugenAnimation * Character::getCurrentAnimation() const {
    typedef std::map< int, MugenAnimation * > Animations;
    Animations::const_iterator it = getAnimations().find(currentAnimation);
    if (it != getAnimations().end()){
        MugenAnimation * animation = (*it).second;
        return animation;
    }
    return NULL;
}

/* returns all the commands that are currently active */
vector<string> Character::doInput(const MugenStage & stage){
    if (behavior == NULL){
        throw MugenException("Internal error: No behavior specified");
    }

    return behavior->currentCommands(stage, this, commands, getFacing() == Object::FACING_RIGHT);

    /*
    vector<string> out;

    InputMap<Mugen::Keys>::Output output = InputManager::getMap(getInput());

    // if (hasControl()){
        Global::debug(2) << "Commands" << endl;
        for (vector<Command*>::iterator it = commands.begin(); it != commands.end(); it++){
            Command * command = *it;
            if (command->handle(output)){
                Global::debug(2) << "command: " << command->getName() << endl;
                out.push_back(command->getName());
            }
        }
    // }

    return out;
    */
}

bool Character::isPaused(){
    return hitState.shakeTime > 0;
}

int Character::pauseTime() const {
    return hitState.shakeTime;
}

/*
InputMap<Mugen::Keys> & Character::getInput(){
    if (getFacing() == Object::FACING_RIGHT){
        return inputLeft;
    }
    return inputRight;
}
*/

static bool holdingBlock(const vector<string> & commands){
    for (vector<string>::const_iterator it = commands.begin(); it != commands.end(); it++){
        if (*it == "holdback"){
            return true;
        }
    }

    return false;
}

/* Inherited members */
void Character::act(vector<Object*>* others, World* world, vector<Object*>* add){

    blocking = false;

    // if (hitState.shakeTime > 0 && moveType != Move::Hit){
    if (hitState.shakeTime > 0){
        hitState.shakeTime -= 1;
        return;
    }

    /*
    if (nextCombo > 0){
        nextCombo -= 1;
        if (nextCombo <= 0){
            combo = 0;
        }
    }
    */

    MugenAnimation * animation = getCurrentAnimation();
    if (animation != 0){
	/* Check debug state */
	if (debug){
	    if (!animation->showingDefense()){
		animation->toggleDefense();
	    }
	    if (!animation->showingOffense()){
		animation->toggleOffense();
	    }
	} else {
	    if (animation->showingDefense()){
		animation->toggleDefense();
	    }
	    if (animation->showingOffense()){
		animation->toggleOffense();
	    }
	}
        animation->logic();
    }

    /* redundant for now */
    if (hitState.shakeTime > 0){
        hitState.shakeTime -= 1;
    } else if (hitState.hitTime > -1){
        hitState.hitTime -= 1;
    }

    /* if shakeTime is non-zero should we update stateTime? */
    stateTime += 1;
    
    /* hack! */
    MugenStage & stage = *(MugenStage*) world;

    /* active is the current set of commands */
    vector<string> active = doInput(stage);
    /* always run through the negative states */

    blocking = holdingBlock(active);

    if (needToGuard){
        needToGuard = false;
        /* misnamed state, but this is the first guard state and will
         * eventually transition to stand/crouch/air guard
         */
        guarding = true;
        changeState(stage, Mugen::StartGuardStand, active);
    }

    doStates(stage, active, -3);
    doStates(stage, active, -2);
    doStates(stage, active, -1);
    doStates(stage, active, currentState);

    /*
    while (doStates(active, currentState)){
        / * empty * /
    }
    */

    /*! do regeneration if set */
    if (regenerateHealth){
        if (getHealth() < 5){
            setHealth(5);
            regenerateTime = REGENERATE_TIME;
        }

        if (regenerating){

            /* avoid rounding errors */
            if (getHealth() >= getMaxHealth() - 2){
                setHealth(getMaxHealth());
            } else {
                setHealth((int) ((getMaxHealth() + getHealth()) / 2.0));
            }

            if (getHealth() == getMaxHealth()){
                regenerating = false;
            }
            regenerateTime = REGENERATE_TIME;
        } else if (getHealth() < getMaxHealth() && regenerateTime == REGENERATE_TIME){
            regenerateTime -= 1;
        } else if (regenerateTime <= 0){
            regenerating = true;
        } else {
            regenerateTime -= 1;
        }

        /*
        if (getHealth() < getMaxHealth()){
            regenerateTime = REGENERATE_TIME;
        } else {
            if (regenerateTime <= 0){
                setHealth((getMaxHealth() + getHealth())/2);
                regenerateHealthDifference = getHealth();
            } else {
                regenerateTime -= 1;
            }
        }
        */
    }
}
        
void Character::addPower(double d){
    power += d;
    /* max power is 3000. is that specified somewhere or just hard coded
     * in the engine?
     */
    if (power > 3000){
        power = 3000;
    }
}
        
void Character::didHit(Character * enemy, MugenStage & stage){
    if (getHit() != NULL){
        hitState.shakeTime = getHit()->pause.player1;
    }

    if (states[getCurrentState()]->powerChanged()){
        addPower(states[getCurrentState()]->getPower()->evaluate(FullEnvironment(stage, *this)).toNumber());
    }

    /* if he is already in a Hit state then increase combo */
    if (enemy->getMoveType() == Move::Hit){
        combo += 1;
    } else {
        combo = 1;
    }

    // nextCombo = 15;

    hitCount += 1;

    if (behavior != NULL){
        behavior->hit(enemy);
    }
}

void Character::wasHit(MugenStage & stage, Character * enemy, const HitDefinition & hisHit){
    hitState.update(stage, *this, getY() > 0, hisHit);
    setXVelocity(hitState.xVelocity);
    setYVelocity(hitState.yVelocity);
    lastTicket = enemy->getTicket();

    if (hisHit.damage.damage != 0){
        takeDamage(stage, enemy, (int) hisHit.damage.damage->evaluate(FullEnvironment(stage, *this)).toNumber());
    }

    /*
    if (getHealth() <= 0){
        hitState.fall.fall = true;
    }
    */

    juggleRemaining -= enemy->getCurrentJuggle() + hisHit.airJuggle;
    
    vector<string> active;
    /* FIXME: replace 5000 with some constant */
    changeState(stage, 5000, active);

    /*
    vector<string> active;
    while (doStates(active, currentState)){
    }
    */
}

/* returns true if a state change occured */
bool Character::doStates(MugenStage & stage, const vector<string> & active, int stateNumber){
    int oldState = getCurrentState();
    if (states[stateNumber] != 0){
        State * state = states[stateNumber];
        for (vector<StateController*>::const_iterator it = state->getControllers().begin(); it != state->getControllers().end(); it++){
            const StateController * controller = *it;
            Global::debug(2 * !controller->getDebug()) << "State " << stateNumber << " check state controller " << controller->getName() << endl;

#if 0
            /* more debugging */
            bool hasFF = false;
            for (vector<string>::const_iterator it = active.begin(); it != active.end(); it++){
                if (*it == "holdup"){
                    hasFF = true;
                }
            }
            if (stateNumber == -1 && controller->getName() == "jump" && hasFF){
            if (controller->getName() == "run fwd"){
                int x = 2;
            }
            /* for debugging
            if (stateNumber == 20 && controller->getName() == "3"){
                int x = 2;
            }
            */
#endif

            try{
                if (controller->canTrigger(stage, *this, active)){
                    /* activate may modify the current state */
                    controller->activate(stage, *this, active);

                    if (stateNumber >= 0 && getCurrentState() != oldState){
                        return true;
                    }
                }
            } catch (const MugenException & me){
                Global::debug(0) << "Error while processing state " << stateNumber << ", " << controller->getName() << ". Error with trigger: " << me.getReason() << endl;
            }
        }
    }
    return false;
}

void Character::draw(Bitmap * work, int cameraX, int cameraY){

    if (debug){
        const Font & font = Font::getFont(Global::DEFAULT_FONT, 18, 18);
        int x = 0;
        if (getAlliance() == MugenStage::Player2Side){
            x = 640 - font.textLength("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") - 1;
        }
        int y = 1;
        FontRender * render = FontRender::getInstance();
        render->addMessage(font, x, y, Bitmap::makeColor(255, 255, 255), -1, "State %d Animation %d", getCurrentState(), currentAnimation);
        y += font.getHeight();
        render->addMessage(font, x, y, Bitmap::makeColor(255, 255, 255), -1, "Vx %f Vy %f", getXVelocity(), getYVelocity());
        y += font.getHeight();
        render->addMessage(font, x, y, Bitmap::makeColor(255, 255, 255), -1, "X %f Y %f", getX(), getY());
        y += font.getHeight();
        render->addMessage(font, x, y, Bitmap::makeColor(255, 255, 255), -1, "Time %d", getStateTime());
        y += font.getHeight();
        if (getMoveType() == Move::Hit){
            render->addMessage(font, x, y, Bitmap::makeColor(255, 255, 255), -1, "HitShake %d HitTime %d", getHitState().shakeTime, getHitState().hitTime);
            y += font.getHeight();
            render->addMessage(font, x, y, Bitmap::makeColor(255, 255, 255), -1, "Hit velocity x %f y %f", getHitState().xVelocity, getHitState().yVelocity);
        }
    }

    /*
    int color = Bitmap::makeColor(255,255,255);
    font.printf( x, y, color, *work, "State %d Animation %d", 0,  getCurrentState(), currentAnimation);
    font.printf( x, y + font.getHeight() + 1, color, *work, "X %f Y %f", 0, (float) getXVelocity(), (float) getYVelocity());
    font.printf( x, y + font.getHeight() * 2 + 1, color, *work, "Time %d", 0, getStateTime());
    */

    MugenAnimation * animation = getCurrentAnimation();
    /* this should never be 0... */
    if (animation != 0){
        int x = getRX() - cameraX;
        int y = getRY() - cameraY;

        if (isPaused() && moveType == Move::Hit){
            x += PaintownUtil::rnd(3) - 1;
        }

        animation->render(getFacing() == Object::FACING_LEFT, false, x, y, *work, xscale, yscale);
    }
}

bool Character::canTurn() const {
    return getCurrentState() == Standing ||
           getCurrentState() == WalkingForwards ||
           getCurrentState() == WalkingBackwards ||
           getCurrentState() == Crouching;
}

static MugenSound * findSound(const Mugen::SoundMap & sounds, int group, int item){
    Mugen::SoundMap::const_iterator findGroup = sounds.find(group);
    if (findGroup != sounds.end()){
        const map<unsigned int, MugenSound*> & found = (*findGroup).second;
        map<unsigned int, MugenSound*>::const_iterator sound = found.find(item);
        if (sound != found.end()){
            return (*sound).second;
        }
    }
    return NULL;
}

MugenSound * Character::getCommonSound(int group, int item) const {
    if (getCommonSounds() == NULL){
        return NULL;
    }
    return findSound(*getCommonSounds(), group, item);
}
        
MugenSound * Character::getSound(int group, int item) const {
    return findSound(getSounds(), group, item);
    /*
    map<unsigned int, map<unsigned int, MugenSound*> >::const_iterator findGroup = sounds.find(group);
    if (findGroup != sounds.end()){
        const map<unsigned int, MugenSound*> & found = (*findGroup).second;
        map<unsigned int, MugenSound*>::const_iterator sound = found.find(item);
        if (sound != found.end()){
            return (*sound).second;
        }
    }
    return 0;
    */
}

void Character::doTurn(MugenStage & stage){
    vector<string> active;
    if (getCurrentState() != Mugen::Crouching){
	changeState(stage, Mugen::StandTurning, active);
    } else {
	changeState(stage, Mugen::CrouchTurning, active);
    }
    reverseFacing();
}

void Character::grabbed(Object*){
}

void Character::unGrab(){
}

bool Character::isGrabbed(){
    return false;
}

Object* Character::copy(){
    return this;
}

const vector<MugenArea> Character::getAttackBoxes() const {
    return getCurrentAnimation()->getAttackBoxes(getFacing() == Object::FACING_LEFT);
}

const vector<MugenArea> Character::getDefenseBoxes() const {
    return getCurrentAnimation()->getDefenseBoxes(getFacing() == Object::FACING_LEFT);
}

const std::string& Character::getAttackName(){
    return getName();
}

bool Character::collision(ObjectAttack*){
    return false;
}

int Character::getDamage() const {
    return 0;
}

bool Character::isCollidable(Object*){
    return true;
}

bool Character::isGettable(){
    return false;
}

bool Character::isGrabbable(Object*){
    return true;
}

bool Character::isAttacking(){
    return false;
}

int Character::getWidth() const {
    return groundfront;
}

int Character::getBackWidth() const {
    return groundback;
}
        
int Character::getBackX() const {
    if (getFacing() == Object::FACING_LEFT){
        return getRX() + getBackWidth();
    }
    return getRX() - getBackWidth();
}

int Character::getFrontX() const {
    if (getFacing() == Object::FACING_LEFT){
        return getRX() + getWidth();
    }
    return getRX() - getWidth();
}

Network::Message Character::getCreateMessage(){
    return Network::Message();
}

void Character::getAttackCoords(int&, int&){
}

double Character::minZDistance() const{
    return 0;
}

void Character::attacked(World*, Object*, std::vector<Object*, std::allocator<Object*> >&){
}
        
int Character::getCurrentCombo() const {
    return combo;
}

/* TODO: implement these */
void Character::setUnhurtable(){
}

void Character::setHurtable(){
}
        
void Character::addWin(WinGame win){
    wins.push_back(win);
}

void Character::addMatchWin(){
    matchWins += 1;
}

void Character::resetPlayer(){
    clearWins();
    power = 0;
    setHealth(getMaxHealth());
}
        
bool Character::isBlocking(const HitDefinition & hit){
    /* FIXME: can only block if in the proper state relative to the hit def */
    return hasControl() && blocking;
}

bool Character::isGuarding() const {
    return guarding;
}
        
void Character::guarded(Character * enemy, const HitDefinition & hit){
    /* FIXME: call hitState.updateGuard */
    hitState.guarded = true;
    lastTicket = enemy->getTicket();
    /* the character will transition to the guard state when he next acts */
    needToGuard = true;
    bool inAir = getY() > 0;
    if (inAir){
    } else {
        setXVelocity(hit.guardVelocity);
    }
}

}
