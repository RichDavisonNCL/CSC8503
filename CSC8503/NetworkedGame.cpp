#include "NetworkedGame.h"
#include "NetworkPlayer.h"
#include "./Networking/NetworkObject.h"

#include "../NCLCoreClasses/Window.h"

#define COLLISION_MSG 30
#define TEST_MSG 666

using namespace NCL;
using namespace CSC8503;

struct MessagePacket : public GamePacket {
	short playerID	= -1;
	short messageID = -1;

	MessagePacket() {
		type = BasicNetworkMessages::Message;
		size = sizeof(short) * 2;
	}
};

NetworkedGame::NetworkedGame(GameWorld& gameWorld, GameTechRendererInterface& renderer, PhysicsSystem& physics, Controller& gameController)
	: TutorialGame(gameWorld, renderer, physics, gameController)	{

	timeToNextPacket  = 0.0f;
	packetsToSnapshot = 0;
}

NetworkedGame::~NetworkedGame()	{

}

void NetworkedGame::UpdateGame(float dt) {
	timeToNextPacket -= dt;
	if (timeToNextPacket < 0) {

		timeToNextPacket += 1.0f / 20.0f; //20hz server/client update
	}


	if (IsServer()) {
		UpdateAsServer(dt);
	}
	if (IsClient()) {
		UpdateAsClient(dt);
	}

	if (!IsServer() && Window::GetKeyboard()->KeyPressed(KeyCodes::F9)) {
		InitialiseServer(NetworkSystem::GetDefaultPort(), 4);
	}
	if (!IsClient() && Window::GetKeyboard()->KeyPressed(KeyCodes::F10)) {
		InitialiseClient(127, 0, 0, 1, NetworkSystem::GetDefaultPort());
	}

	//StartLevel();

	UpdateNetwork();

	TutorialGame::UpdateGame(dt);
}

void NetworkedGame::UpdateAsServer(float dt) {
	packetsToSnapshot--;
	if (packetsToSnapshot < 0) {
		BroadcastSnapshot(false);
		packetsToSnapshot = 5;
	}
	else {
		BroadcastSnapshot(true);
	}

	if (Window::GetKeyboard()->KeyPressed(KeyCodes::B)) {
		MessagePacket newPacket;
		newPacket.messageID = TEST_MSG;
		BroadcastServerPacket(newPacket);
	}
}

void NetworkedGame::UpdateAsClient(float dt) {
	if (Window::GetKeyboard()->KeyPressed(KeyCodes::SPACE)) {
		//fire button pressed!	
 		ClientPacket newPacket;
		newPacket.buttonstates[0] = 1;
		newPacket.lastID = 0; //You'll need to work this out somehow...

		SendClientPacket(newPacket);
	}
}

void NetworkedGame::BroadcastSnapshot(bool deltaFrame) {
	//std::vector<GameObject*>::const_iterator first;
	//std::vector<GameObject*>::const_iterator last;

	//world.GetObjectIterators(first, last);

	//for (auto i = first; i != last; ++i) {
	//	NetworkObject* o = (*i)->GetNetworkObject();
	//	if (!o) {
	//		continue;
	//	}
	//	//TODO - you'll need some way of determining
	//	//when a player has sent the server an acknowledgement
	//	//and store the lastID somewhere. A map between player
	//	//and an int could work, or it could be part of a 
	//	//NetworkPlayer struct. 
	//	int playerState = 0;
	//	GamePacket* newPacket = nullptr;
	//	if (o->WritePacket(&newPacket, deltaFrame, playerState)) {
	//		thisServer->SendGlobalPacket(*newPacket);
	//		delete newPacket;
	//	}
	//}
}

void NetworkedGame::UpdateMinimumState() {
	//Periodically remove old data from the server
	int minID = INT_MAX;
	int maxID = 0; //we could use this to see if a player is lagging behind?

	for (auto i : stateIDs) {
		minID = std::min(minID, i.second);
		maxID = std::max(maxID, i.second);
	}
	//every client has acknowledged reaching at least state minID
	//so we can get rid of any old states!
	std::vector<GameObject*>::const_iterator first;
	std::vector<GameObject*>::const_iterator last;
	world.GetObjectIterators(first, last);

	for (auto i = first; i != last; ++i) {
		NetworkObject* o = (*i)->GetNetworkObject();
		if (!o) {
			continue;
		}
		o->UpdateStateHistory(minID); //clear out old states so they arent taking up memory...
	}
}

void NetworkedGame::SpawnPlayer() {

}

void NetworkedGame::StartLevel() {

}

void NetworkedGame::ReceivePacketFromClient(GamePacket* payload, int source) {
	
}

void NetworkedGame::ReceivePacketFromServer(GamePacket* payload) {
	switch (payload->type) {
		case BasicNetworkMessages::Message: {
			std::cout << "Received message from server!\n";
		}break;
	}
}

void NetworkedGame::OnPlayerCollision(NetworkPlayer* a, NetworkPlayer* b) {
//detected a collision between players!
//Only servers should be in control of when this actually has an effect...
	if (IsServer()) {
		MessagePacket newPacket;
		newPacket.messageID = COLLISION_MSG;
		newPacket.playerID  = a->GetPlayerNum();
		//newPacket.playerID = b->GetPlayerNum();
		BroadcastServerPacket(newPacket);
	}
}