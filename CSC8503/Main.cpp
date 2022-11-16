#include "Window.h"

#include "Debug.h"

#include "StateMachine.h"
#include "StateTransition.h"
#include "State.h"

#include "GameServer.h"
#include "GameClient.h"

#include "NavigationGrid.h"
#include "NavigationMesh.h"

#include "TutorialGame.h"
#include "NetworkedGame.h"

#include "PushdownMachine.h"

#include "PushdownState.h"

#include "BehaviourNode.h"
#include "BehaviourSelector.h"
#include "BehaviourSequence.h"
#include "BehaviourAction.h"
//#include "../CSC8503Common/BehaviourTree.h"


using namespace NCL;
using namespace CSC8503;

#include <chrono>
#include <thread>
#include <sstream>


void TestBehaviourTree() {
	float	behaviourTimer;
	int		distanceToTarget;

	BehaviourAction* findKey = new BehaviourAction("Find Key", 
		[&](float dt, BehaviourState state)->BehaviourState{
			if (state == Initialise) {
				std::cout << "Looking for a key!\n";
				behaviourTimer = rand() % 100;
				state = Ongoing;
			}
			else if (state == Ongoing) {
				behaviourTimer -= dt;
				if (behaviourTimer <= 0.0f) {
					std::cout << "Found a key!\n";
					return Success;
				}
			}
			return state;
		}
	);
	BehaviourAction* goToRoom = new BehaviourAction("Go To Room",
		[&](float dt, BehaviourState state)->BehaviourState {
			if (state == Initialise) {
				std::cout << "Going to the loot room!\n";
				state = Ongoing;
			}
			else if (state == Ongoing) {
				distanceToTarget -= dt;
				if (distanceToTarget <= 0.0f) {
					std::cout << "Reached room!\n";
					return Success;
				}
			}
			return state;
		}
	);
	BehaviourAction* openDoor = new BehaviourAction("Open Door",
		[&](float dt, BehaviourState state)->BehaviourState {
			if (state == Initialise) {
				std::cout << "Opening Door!\n";
				return Success;
			}
			return state;
		}
	);
	BehaviourAction* lookForTreasure = new BehaviourAction("Look For Treasure",
		[&](float dt, BehaviourState state)->BehaviourState {
			if (state == Initialise) {
				std::cout << "Looking for treasure!\n";
				return Ongoing;
			}
			else if (state == Ongoing) {
				bool found = rand() % 2;
				if (found) {
					std::cout << "I found some treasure!\n";
					return Success;
				}
				std::cout << "No treasure in here...\n";
				return Failure;
			}
			return state;
		}
	);
	BehaviourAction* lookForItems = new BehaviourAction("Look For Items",
		[&](float dt, BehaviourState state)->BehaviourState {
			if (state == Initialise) {
				std::cout << "Looking for items!\n";
				return Ongoing;
			}
			else if (state == Ongoing) {
				bool found = rand() % 2;
				if (found) {
					std::cout << "I found some items!\n";
					return Success;
				}
				std::cout << "No items in here...\n";
				return Failure;
			}
			return state;
		}
	);

	BehaviourSequence* sequence = new BehaviourSequence("Room Sequence");
	sequence->AddChild(findKey);
	sequence->AddChild(goToRoom);
	sequence->AddChild(openDoor);

	BehaviourSelector* selection = new BehaviourSelector("Loot Selection");
	selection->AddChild(lookForTreasure);
	selection->AddChild(lookForItems);

	BehaviourSequence* rootSequence = new BehaviourSequence("Root Sequence");
	rootSequence->AddChild(sequence);
	rootSequence->AddChild(selection);

	for (int i = 0; i < 5; ++i) {
		rootSequence->Reset();
		behaviourTimer		= 0.0f;
		distanceToTarget	= rand() % 250;
		BehaviourState state = Ongoing;
		std::cout << "We're going on an adventure!\n";
		while (state == Ongoing) {
			state = rootSequence->Execute(1.0f); //fake dt
		}
		if (state == Success) {
			std::cout << "What a successful adventure!\n";
		}
		else if (state == Failure) {
			std::cout << "What a waste of time!\n";
		}
	}
	std::cout << "All done!\n";
}















void TestStateMachine() {
	StateMachine* testMachine = new StateMachine();

	int data = 0;
	State* A = new State([&](float dt)->void
		{
			std::cout << "I'm in state A!\n";
			data++;
		}
	);
	State* B = new State([&](float dt)->void
		{
			std::cout << "I'm in state B!\n";
			data--;
		}
	);

	StateTransition* stateAB = new StateTransition(A, B, [&](void)-> bool
		{
			return data > 10;
		}
	);
	StateTransition* stateBA = new StateTransition(B,A, [&](void)-> bool
		{
			return data < 0;
		}
	);
	testMachine->AddState(A);
	testMachine->AddState(B);
	testMachine->AddTransition(stateAB);
	testMachine->AddTransition(stateBA);

	for (int i = 0; i < 100; ++i) {
		testMachine->Update(1.0f);
	}
}

class TestPacketReceiver : public PacketReceiver {
public:
	TestPacketReceiver(string name) {
		this->name = name;
	}
	void ReceivePacket(int type, GamePacket* payload, int source) {
		if (type == String_Message) {
			StringPacket* realPacket = (StringPacket*)payload;

			string msg = realPacket->GetStringFromData();

			std::cout << name << " received message: " << msg << std::endl;
		}
	}
	string name;
};

void TestNetworking() {
	NetworkBase::Initialise();

	TestPacketReceiver serverReceiver("Server");
	TestPacketReceiver clientReceiver("Client1");

	TestPacketReceiver clientReceiver2("Client2");

	int port = NetworkBase::GetDefaultPort();

	GameServer* server = new GameServer(port, 2);
	GameClient* client = new GameClient();
	GameClient* client2 = new GameClient();

	server->RegisterPacketHandler(String_Message, &serverReceiver);
	client->RegisterPacketHandler(String_Message, &clientReceiver);
	client2->RegisterPacketHandler(String_Message, &clientReceiver2);

	bool canConnect = client->Connect(127, 0, 0, 1, port);
	bool canConnect2 = client2->Connect(127, 0, 0, 1, port);
//	std::this_thread::sleep_for(std::chrono::seconds(1));

	for (int i = 0; i < 100; ++i) {
		StringPacket a("Client1 says hello! " + std::to_string(i));
		StringPacket b("Client2 says hello! " + std::to_string(i));
		StringPacket c("Server says hello! " + std::to_string(i));
		server->SendGlobalPacket(c);
		client->SendPacket(a);
		client2->SendPacket(b);
		server->UpdateServer();
		client->UpdateClient();
		client2->UpdateClient();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

//	std::this_thread::sleep_for(std::chrono::seconds(1));

	NetworkBase::Destroy();
}

vector<Vector3> testNodes;

void TestPathfinding() {
	NavigationGrid grid("TestGrid1.txt");

	NavigationPath outPath;

	Vector3 startPos(80,0,10);
	Vector3 endPos(80, 0, 80);

	bool found = grid.FindPath(startPos, endPos, outPath);

	Vector3 pos;
	while (outPath.PopWaypoint(pos)) {
		testNodes.push_back(pos);
	}
}

void DisplayPathfinding() {
	for (int i = 1; i < testNodes.size(); ++i) {
		Vector3 a = testNodes[i - 1];
		Vector3 b = testNodes[i];

		Debug::DrawLine(a, b, Vector4(0, 1, 0, 1));
	}
}

class PauseScreen : public PushdownState {
	PushdownResult OnUpdate(float dt, PushdownState* * newState) override {
		if (Window::GetKeyboard()->KeyPressed(KeyboardKeys::U)) {
			return PushdownResult::Pop;
		}
		return PushdownResult::NoChange;
	}
	void OnAwake() override {
		std::cout << "Press U to unpause game!\n";
	}
};

class GameScreen : public PushdownState {
	PushdownResult OnUpdate(float dt, PushdownState** newState) override {
		pauseReminder -= dt;
		if (pauseReminder < 0) {		
			std::cout << "Coins mined: " << coinsMined << "\n";
			std::cout << "Press P to pause game, or F1 to return to main menu!\n";
			pauseReminder += 1.0f;
		}
		if (Window::GetKeyboard()->KeyDown(KeyboardKeys::P)) {
			*newState = new PauseScreen();
			return  PushdownResult::Push;
		}
		if (Window::GetKeyboard()->KeyDown(KeyboardKeys::F1)) {		
			std::cout << "Returning to main menu!\n";
			return PushdownResult::Pop;
		}
		if (rand() % 7 == 0) {
			coinsMined++;
		}
		return PushdownResult::NoChange;
	};
	void OnAwake() override {
		std::cout << "Preparing to mine coins!\n";
	}
protected:
	int coinsMined = 0;
	float pauseReminder = 1;
};


class IntroScreen : public PushdownState {
	PushdownResult OnUpdate(float dt, PushdownState** newState) override {
		if (Window::GetKeyboard()->KeyPressed(KeyboardKeys::SPACE)) {
			if (newState == nullptr) {

			}
			*newState = new GameScreen();
			return PushdownResult::Push;
		}
		if (Window::GetKeyboard()->KeyPressed(KeyboardKeys::ESCAPE)) {
			return PushdownResult::Pop;
		}
		return PushdownResult::NoChange;
	};
	void OnAwake() override {
		std::cout << "Welcome to a really awesome game!\n";
		std::cout << "Press Space To Begin or escape to quit!\n";
	}
};

void TestPushdownAutomata(Window* w) {
	PushdownMachine machine(new IntroScreen());
	while (w->UpdateWindow()) {
		float dt = w->GetTimer()->GetTimeDeltaSeconds();
		if (!machine.Update(dt)) {
			return;
		}
	}
}

struct SomeData {
	Vector3 a;
	float b;
};

struct SomeData2 {
	Vector3 position;
	Vector3 velocity;
	float health;
	int objectID;
};

struct SomeData3 {
	Vector3 position;
	float health;
	Vector3 velocity;
	int objectID;
};



/*

The main function should look pretty familar to you!
We make a window, and then go into a while loop that repeatedly
runs our 'game' until we press escape. Instead of making a 'renderer'
and updating it, we instead make a whole game, and repeatedly update that,
instead. 

This time, we've added some extra functionality to the window class - we can
hide or show the 

*/
int main() {
	Window*w = Window::CreateGameWindow("CSC8503 Game technology!", 1280, 720);

	if (!w->HasInitialised()) {
		return -1;
	}	
	//TestBehaviourTree();
	//TestPushdownAutomata(w);

	//TestStateMachine();

	//Vector3 c = Vector3::Cross(Vector3(-1, 0, 0), Vector3(0, 1, 0));

	//Matrix3 rotMatrix;
	//rotMatrix.SetColumn(0, Vector3(-1, 0, 0));
	//rotMatrix.SetColumn(1, Vector3( 0, 1, 0));
	//rotMatrix.SetColumn(2, c);

	//Quaternion q(rotMatrix);

	//Vector3 l = q * Vector3(0, 0, 1);

	//Plane p1 = Plane::PlaneFromTri(Vector3(-1, 0, -230), Vector3(0, 1, -230), Vector3(1, 0, -230));

	//p1.DistanceFromPlane
	////TestStateMachine();
	//TestNetworking();
	//TestPathfinding();
	//
	w->ShowOSPointer(false);
	w->LockMouseToWindow(true);

	int x = sizeof(Vector3);

	int x1 = sizeof(SomeData);
	int x2 = sizeof(SomeData2);
	int x3 = sizeof(SomeData3);

	std::cout << x << " , " << x1 << " , " << x2 << " , " << x3 << "\n";

	NavigationMesh* m = new NavigationMesh("smalltest.navmesh");
	NavigationPath p;
	m->FindPath(Vector3(32.49, 5, 55.6), Vector3(94.9, 5, 20.4), p);
	//m->FindPath(Vector3(500.49, 5, -55.6), Vector3(94.9, 5, -200.4), p);
	TutorialGame* g = new TutorialGame();
	w->GetTimer()->GetTimeDeltaSeconds(); //Clear the timer so we don't get a larget first dt!
	while (w->UpdateWindow() && !Window::GetKeyboard()->KeyDown(KeyboardKeys::ESCAPE)) {
		float dt = w->GetTimer()->GetTimeDeltaSeconds();
		if (dt > 0.1f) {
			std::cout << "Skipping large time delta" << std::endl;
			continue; //must have hit a breakpoint or something to have a 1 second frame time!
		}
		if (Window::GetKeyboard()->KeyPressed(KeyboardKeys::PRIOR)) {
			w->ShowConsole(true);
		}
		if (Window::GetKeyboard()->KeyPressed(KeyboardKeys::NEXT)) {
			w->ShowConsole(false);
		}

		if (Window::GetKeyboard()->KeyPressed(KeyboardKeys::T)) {
			w->SetWindowPosition(0, 0);
		}

		std::stringstream lol;

		//lol.str

		//Debug::Print()


		DisplayPathfinding();

		w->SetTitle("Gametech frame time:" + std::to_string(1000.0f * dt));

		g->UpdateGame(dt);
	}
	Window::DestroyGameWindow();
}
//
//int main() {
//
//	while (gameActive) {
//		//This will tell us how long 'DoSomeThings' took last time around...
//		float timeStep = timer.CalculateTime();
//		DoSomeThings();
//	}
//}
//
//
//void GameFrame() {
//	for (each object in world) {
//		bool shouldThink = rand() % 2;
//		//A random chance = a random variation in timestep!
//		if (shouldThink) {
//			object.Update();
//		}
//	}
//}
//
//void Object::Update() {
//	//Different paths will take different amount of time to
//	//traverse! So this could be hit in any frame...
//	if ((position - targetPosition).Length() < 1) {
//		targetPosition = PickRandomPosition();
//		//Pathfinding could take a variable amount of time to calculate!
//		path = GeneratePath(targetPosition);
//	}
//	else {
//		FollowPath();
//	}
//	canSeeEnemy = DoRaycasting(); //Could have some variance...
//}
//
//
//
//void Object::FollowPath() {
//	Vector3 dir = (targetPosition - position).Normalised();
//
//	//A varying timestep means that a different number of frames
//	//is occuring each second!
//	//This means that over different seconds, this could move
//	//by a varying amount! A low spec machine will make the
//	//object move slower than a high spec machine!
//	position += dir;
//}
//
//
//void Object::FollowPath(float timeStep) {
//	Vector3 dir = (targetPosition - position).Normalised();
//
////A lot more consistent! A lower spec machine gets a longer
////timestep than a high spec machine, so dir will be scaled
////appropriately.
//	position += dir * timeStep;
//
////BUT a varying timetep does mean that certain events
////cannot be accurately predicted:
//	timeToExplode -= timeStep;
//	if (timeToExplode <= 0.0f) {
//		Explode();
//	}
////The timer could end up at 0.000001...
////It could therefore be a frame late!
//}
//
//
