#include "StateGameObject.h"
#include "./AI/StateMachines/StateTransition.h"
#include "./AI/StateMachines/StateMachine.h"
#include "./AI/StateMachines/State.h"
#include "./Physics/PhysicsObject.h"

using namespace NCL;
using namespace CSC8503;

StateGameObject::StateGameObject() {

}

StateGameObject::~StateGameObject() {
	delete stateMachine;
}

void StateGameObject::Update(float dt) {
}

void StateGameObject::MoveLeft(float dt) {

}

void StateGameObject::MoveRight(float dt) {
}