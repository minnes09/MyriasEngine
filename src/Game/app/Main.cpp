// Game executable: for now a minimal scene to check the engine visually (window, loop, ECS, rendering).

#include "App/Myrias.h"

#include "Core/Fatal.h"
#include "ECS/Components/Renderable.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/Velocity.h"
#include "ECS/EntityManager.h"
#include "ECS/Systems/System.h"

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

namespace
{
constexpr unsigned int kWindowWidth  = 1280;
constexpr unsigned int kWindowHeight = 720;

// Integrates velocity and reflects it on the window edges.
class BounceSystem : public ISystem
{
public:
	explicit BounceSystem(sf::Vector2f bounds) : bounds(bounds) {}

	void Update(float dt) override
	{
		auto& entities     = EntityManager::Get();
		auto* movementPool = entities.GetPool<MovementComponent>();
		if (!movementPool)
			return;

		movementPool->ForEach(
		    [&](DEPRECATED_EntityId entity, MovementComponent& movement)
		    {
			    auto* transform = entities.GetComponent<TransformComponent>(entity);
			    if (!transform)
				    return;

			    const auto*        renderable = entities.GetComponent<RenderableComponent>(entity);
			    const sf::Vector2f size       = renderable ? renderable->size : sf::Vector2f{};

			    transform->position += movement.velocity * dt;
			    Reflect(transform->position.x, movement.velocity.x, size.x, bounds.x);
			    Reflect(transform->position.y, movement.velocity.y, size.y, bounds.y);
		    });
	}

private:
	static void Reflect(float& position, float& velocity, float size, float limit)
	{
		if (position < 0.f)
		{
			position = 0.f;
			velocity = -velocity;
		}
		else if (position + size > limit)
		{
			position = limit - size;
			velocity = -velocity;
		}
	}

	sf::Vector2f bounds;
};

// Creates the test scene: a static box and three bouncing balls.
void CreateScene(EntityManager& entities)
{
	const DEPRECATED_EntityId box = entities.CreateEntity();
	entities.AddComponent(box, TransformComponent{ { 590.f, 620.f } });
	entities.AddComponent(box, RenderableComponent{ .shape = Shape::Rectangle,
	                                                .size  = { 100.f, 20.f },
	                                                .color = sf::Color(80, 160, 255) });

	const sf::Color colors[] = { sf::Color::White, sf::Color(255, 120, 80), sf::Color(120, 220, 120) };
	for (int i = 0; i < 3; ++i)
	{
		const DEPRECATED_EntityId ball = entities.CreateEntity();
		entities.AddComponent(ball, TransformComponent{ { 200.f + 300.f * i, 150.f + 80.f * i } });
		entities.AddComponent(ball, MovementComponent{ .velocity = { 220.f + 40.f * i, 180.f - 30.f * i } });
		entities.AddComponent(ball, RenderableComponent{ .shape            = Shape::Circle,
		                                                 .size             = { 24.f, 24.f },
		                                                 .color            = colors[i],
		                                                 .outlineColor     = sf::Color::Black,
		                                                 .outlineThickness = 2.f });
	}
}
} // namespace

int main()
{
	InstallFatalHandlers();
	Myrias engine({ .width = kWindowWidth, .height = kWindowHeight, .title = "Myrias Game" });

	CreateScene(EntityManager::Get());

	BounceSystem bounceSystem({ static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight) });
	engine.AddSystem(bounceSystem);

	return engine.Run();
}
