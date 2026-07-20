#include "Window.h"

#include "Debug.h"

#include "./AI/StateMachines/StateMachine.h"
#include "./AI/StateMachines/StateTransition.h"
#include "./AI/StateMachines/State.h"

#include "./Networking/GameServer.h"
#include "./Networking/GameClient.h"

#include "./AI/Navigation/NavigationGrid.h"
#include "./AI/Navigation/NavigationMesh.h"

#include "TutorialGame.h"
#include "NetworkedGame.h"

#include "./AI/PushdownAutomata/PushdownMachine.h"
#include "./AI/PushdownAutomata/PushdownState.h"

#include "./AI/BehaviourTrees/BehaviourNode.h"
#include "./AI/BehaviourTrees/BehaviourSelector.h"
#include "./AI/BehaviourTrees/BehaviourSequence.h"
#include "./AI/BehaviourTrees/BehaviourAction.h"

#include "GameTechOpenGLRenderer.h"
#include "KeyboardMouseController.h"

#ifdef USEOGL
#include "GameTechOpenGLRenderer.h"
#endif
#ifdef USEVULKAN
#include "GameTechVulkanRenderer.h"
#endif

using namespace NCL;
using namespace CSC8503;

void TestPathfinding() {
}

void DisplayPathfinding() {
}

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
	NCL::WindowInitialisation windotInit{
		.width = 1280,
		.height = 720,
		.windowTitle = "CSC8503 Game technology!",
	};

	Window*w = Window::CreateGameWindow(windotInit);

	if (!w->HasInitialised()) {
		return -1;
	}	

	w->ShowOSPointer(false);
	w->LockMouseToWindow(true);

	std::unique_ptr<GameWorld>		world = std::make_unique<GameWorld>();
	std::unique_ptr<PhysicsSystem>	physics = std::make_unique<PhysicsSystem>(*world);

#ifdef USEVULKAN
	std::unique_ptr<GameTechRendererInterface> renderer = std::make_unique<GameTechVulkanRenderer>(*world);
#else	
	std::unique_ptr<GameTechRendererInterface> renderer = std::make_unique<GameTechOpenGLRenderer>(*world);
#endif

	Debug::CreateDebugFont("PressStart2P.fnt", *renderer->LoadTexture("PressStart2P.png"));

	Controller* c = new KeyboardMouseController(*Window::GetKeyboard(), *Window::GetMouse());

	c->MapAxis(0, "Sidestep");
	c->MapAxis(1, "UpDown");
	c->MapAxis(2, "Forward");

	c->MapAxis(3, "XLook");
	c->MapAxis(4, "YLook");

	std::unique_ptr<TutorialGame> game = std::make_unique<TutorialGame>(*world, *renderer, *physics, *c);

	w->GetTimer().GetTimeDeltaSeconds(); //Clear the timer so we don't get a larget first dt!
	while (w->UpdateWindow() && !Window::GetKeyboard()->KeyDown(KeyCodes::ESCAPE)) {
		float dt = w->GetTimer().GetTimeDeltaSeconds();
		if (dt > 0.1f) {
			std::cout << "Skipping large time delta" << std::endl;
			continue; //must have hit a breakpoint or something to have a 1 second frame time!
		}
		if (Window::GetKeyboard()->KeyPressed(KeyCodes::PRIOR)) {
			w->ShowConsole(true);
		}
		if (Window::GetKeyboard()->KeyPressed(KeyCodes::NEXT)) {
			w->ShowConsole(false);
		}

		if (Window::GetKeyboard()->KeyPressed(KeyCodes::T)) {
			w->SetWindowPosition(0, 0);
		}

		w->SetTitle("Gametech frame time:" + std::to_string(1000.0f * dt));

		game->UpdateGame(dt);
		world->UpdateWorld(dt);
		physics->Update(dt);
		game->PostPhysicsUpdate();

		renderer->RenderFrame(dt);

		Debug::UpdateRenderables(dt);
	}
	Window::DestroyGameWindow();
}