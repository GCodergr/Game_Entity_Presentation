// Note: This demo is based on Vittorio Romeo 'Dive into C++11' series
// Youtube Playlist: https://www.youtube.com/playlist?list=PLTEcWGdSiQenl4YRPvSqW7UPC6SiGNN7e
// Original Source Code: https://github.com/SuperV1234/Tutorials


#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <bitset>
#include <array>
#include <cassert>
#include <type_traits>
#include <random> 

// We will need some additional includes for frametime handling
// and callbacks.
#include <chrono>
#include <functional>

// And we'll use SFML for gfx and input management.
#include <SFML/Graphics.hpp>


namespace SpaceInvaders
{
	// C++11 pseudo-random generator
	std::minstd_rand rndEngine;

	// Forward declarations
	struct Component;
	class Entity;
	class EntityManager;

	// Name aliases for ComponentID and our group type
	using ComponentID = std::size_t;
	using Group = std::size_t;
	
	// Let's hide implementation details into an "Internal" namespace
	namespace Internal
	{
		inline ComponentID GetUniqueComponentID() noexcept
		{
			// We store a `static` lastID variable: static means
			// that every time we call this function it will refer
			// to the same `lastID` instance.

			// Basically, calling this function returns an unique ID
			// every time.

			static ComponentID lastID{ 0u };
			return lastID++;
		}
	}

	// Now, some "template magic" comes into play.
	// We create a function that returns an unique ComponentID based
	// upon the type passed.
	template<typename T> inline ComponentID GetComponentTypeID() noexcept
	{
		// We an use a `static_assert` to make sure this function
		// is only called with types that inherit from `Component`.
		static_assert(std::is_base_of<Component, T>::value,
			"T must inherit from Component");

		// Every time we call this function with a specific type `T`,
		// we are actually calling an instantiation of this template,
		// with its own unique static `typeID` variable.

		// Upon calling this function for the first time with a specific 
		// type `T1`, `typeID` will be initialized with an unique ID.
		// Subsequent calls with the same type `T1` will return the 
		// same ID.

		static ComponentID typeID{Internal::GetUniqueComponentID()};
		return typeID;
	}

	// Let's define a maximum number of components
	const std::size_t maxComponents{32};

	// Let's typedef an `std::bitset` for our components
	using ComponentBitset = std::bitset<maxComponents>;

	// And let's also typedef an `std::array` for them
	using ComponentArray = std::array<Component*, maxComponents>;	

	const std::size_t maxGroups{32};
	using GroupBitset = std::bitset<maxGroups>;

	// We begin by defining a base `Component` class.
	// Game components will inherit from this class.
	struct Component
	{
		// We will use a pointer to store the parent entity.
		Entity* entity;

		// Usually a game component will have:
		// * Some data
		// * Update behavior
		// * Drawing behavior

		// Therefore we define three virtual methods that
		// will be overridden by game component types.
		virtual void Initialize() { }
		virtual void Update(float frameTime) { }
		virtual void Draw() { }
		
		// As we'll be using this class polymorphically, it requires
		// a virtual destructor.
		virtual ~Component() { }
	};

	// Next, we define an Entity class. 
	// It will basically be an aggregate of components,
	// with some methods that help us update and draw
	// all of them.
	class Entity 
	{
		private:
			// The entity will need a reference to its manager
			EntityManager& manager;

			// We'll keep track of whether the entity is alive or dead
			// with a boolean and we'll store the components in a private
			// vector of `std::unique_ptr<Component>`, to allow polymorphism.
			bool alive{true};
			std::vector<std::unique_ptr<Component>> components;

			bool active{ true };

			// Let's add an array to quickly get a component with 
			// a specific ID, and a bitset to check the existance of
			// a component with a specific ID.
			ComponentArray componentArray;
			ComponentBitset componentBitset;

			// Let's add a bitset to our entities.
			GroupBitset groupBitset;

		// Now we will define some public methods to update and
		// draw, to add components and to destroy the entity.
		public:
			Entity(EntityManager& manager) : manager(manager) { }

			// Updating and drawing simply consists in updating and drawing
			// all the components.
			void Update(float frameTime) 	
			{ 
				for(auto& c : components) 
					c->Update(frameTime); 
			}

			void Draw() 		
			{ 
				for(auto& c : components) 
					c->Draw(); 
			}

			// We will also define some methods to control the lifetime
			// of the entity.
			bool IsAlive() const 	
			{ 
				return alive; 
			}

			void Destroy() 			
			{ 
				alive = false; 
			}

			bool IsActive() const
			{
				return active;
			}
			
			void Enable()
			{
				active = true;
			}

			void Disable()
			{
				active = false;
			}

			// To check if this entity has a component, we simply
			// query the bitset.
			template<typename T> bool HasComponent() const
			{
				return componentBitset[GetComponentTypeID<T>()];
			}

			// Groups will be handled at runtime, not compile-time:
			// therefore we will pass groups as a function argument.
			bool HasGroup(Group group) const noexcept
			{ 
				return groupBitset[group]; 
			}

			// To add/remove group we define some methods that alter
			// the bitset and tell the manager what we're doing,
			// so that the manager can internally store this entity 
			// in its grouped containers. 
			// We'll need to define this method after the definition
			// of `Manager`, as we're gonna call `EntityManager::AddtoGroup` here.
			void AddGroup(Group group) noexcept;

			void DelGroup(Group group) noexcept
			{ 
				groupBitset[group] = false;
				// We won't notify the manager that a group has been
				// removed here, as it will automatically remove 
				// entities from the "wrong" group containers during
				// refresh.
			} 

			// Now, we'll define a method that allows us to add components
			// to our entity.
			// We'll take advantage of C++11 variadic templates and emplacement
			// to directly construct our components in place.
			// `T` is the component type. `TArgs` is a parameter pack of 
			// types used to construct the component.
			template<typename T, typename... TArgs> 
			T& AddComponent(TArgs&&... args)
			{
				// Before adding a component, we make sure it doesn't
				// already exist by using an assertion.
				assert(!HasComponent<T>());

				// We begin by allocating the component of type `T`
				// on the heap, by forwarding the passed arguments
				// to its constructor.
				T* c(new T(std::forward<TArgs>(args)...));
				
				// We set the component's entity to the current
				// instance.
				c->entity = this;

				// We will wrap the raw pointer into a smart one,
				// so that we can emplace it into our container and 
				// to make sure we do not leak any memory.
				std::unique_ptr<Component> uPtr{c};

				// Now we'll add the smart pointer to our container:
				// `std::move` is required, as `std::unique_ptr` cannot
				// be copied.
				components.emplace_back(std::move(uPtr));
				
				// When we add a component of type `T`, we add it to 
				// the array and to the bitset.
				componentArray[GetComponentTypeID<T>()] = c;
				componentBitset[GetComponentTypeID<T>()] = true;
				
				// We can now call `Component::Initialize()`:
				c->Initialize();

				// ...and we will return a reference to the newly added
				// component, in case the user wants to do something
				// with it.
				return *c;
			}

			template<typename T> T& GetComponent() const
			{
				// To retrieve a specific component, we get it from
				// the array. We'll also assert its existance.

				assert(HasComponent<T>());
				auto ptr(componentArray[GetComponentTypeID<T>()]);
				return *reinterpret_cast<T*>(ptr);
			}
	};

	// Even if the `Entity` class may seem complex, conceptually it is
	// very simple. Just think of an entity as a container for components,
	// with syntatic sugar methods to quicky add/update/draw components.

	// If `Entity` is an aggregate of components, `EntityManager` is an aggregate
	// of entities. Implementation is straightforward, and resembles the 
	// previous one.
	class EntityManager
	{
		private:
			std::vector<std::unique_ptr<Entity>> entities;

			// We store entities in groups by creating "group buckets" in an 
			// array. `std::vector<Entity*>` could be also replaced for 
			// `std::set<Entity*>`.
			std::array<std::vector<Entity*>, maxGroups> groupedEntities;

		public:
			void Update(float frameTime) 	
			{ 
				for (auto& e : entities)
				{
					if (e->IsActive())
					{
						e->Update(frameTime);
					}
				}
			}

			void Draw() 			
			{ 
				for (auto& e : entities)
				{
					if (e->IsActive())
					{
						e->Draw();
					}
				}
			}

			// When we add a group to an entity, we just add it to the
			// correct "group bucket".
			void AddToGroup(Entity* entity, Group group)
			{
				// It would be wise to either assert that the bucket doesn't
				// already contain `entity`, or use a set to prevent duplicates
				// in exchange for less efficient insertion/iteration.

				groupedEntities[group].emplace_back(entity);
			}

			// To get entities that belong to a certain group, we can simply
			// get one of the "buckets" from the array.
			std::vector<Entity*>& GetEntitiesByGroup(Group group)
			{
				return groupedEntities[group];
			}

			// During refresh, we need to remove dead entities and entities
			// with incorrect groups from the buckets.
			void Refresh()
			{
				for(auto i(0u); i < maxGroups; ++i)
				{
					auto& v(groupedEntities[i]);

					v.erase(
						std::remove_if(std::begin(v), std::end(v), 
						[i](Entity* entity) 
						{ 
							return !entity->IsAlive() || !entity->HasGroup(i); 
						}), 
						std::end(v));
				}

				// Cleaning up "dead" entities.
				entities.erase(
					std::remove_if(std::begin(entities), std::end(entities), 
					[](const std::unique_ptr<Entity>& entity) 
					{ 
						return !entity->IsAlive(); 
					}), 
					std::end(entities));
			}

			Entity& AddEntity()
			{				
				Entity* e(new Entity(*this));
				std::unique_ptr<Entity> uPtr{e};
				entities.emplace_back(std::move(uPtr));
				return *e;
			}	
	};

	// Here's the definition of `Entity::addToGroup`
	void Entity::AddGroup(Group group) noexcept
	{
		groupBitset[group] = true;
		manager.AddToGroup(this, group);
	}

	//
	// Let's create the components for our Space Invaders clone
	//
	using FrameTime = float;

	const int windowWidth{800}, windowHeight{600};
	const float playerShipWidth{66.f}, playerShipHeight{50.f}, playerShipVelocity{0.6f};
	const float enemyShipWidth{ 69.3f }, enemyShipHeight{ 56.f }, enemyShipVelocity{ 0.05f };
	const float	bulletWidth{ 9.f }, bulletHeight{ 37.f }, bulletVelocity{ 0.5f };
	const int maxPlayerBullets{ 6 }, maxEnemyBullets{ 36 };
	const int countEnemyColumn{9}, countEnemyRow{4};
	const float ftStep{1.f}, ftSlice{1.f};

	// Forward declaration
	struct Game;

	// Entities can have a position in the game world.
	struct Transform : Component
	{
		sf::Vector2f position;

		Transform() = default;
		Transform(const sf::Vector2f& position) : position{ position } { }

		float x() const noexcept { return position.x; }
		float y() const noexcept { return position.y; }
	};

	// Entities can have a physical body and a velocity.
	struct Physics : Component
	{
		Transform* transform{nullptr};
		sf::Vector2f velocity, halfSize;

		// We will use a callback to handle the "out of bounds" event.
		std::function<void(const sf::Vector2f&)> onOutOfBounds;

		Physics(const sf::Vector2f& halfSize) : halfSize{ halfSize } { }

		void Initialize() override
		{	
			// A requirement for `Physics` is obviously `Transform`.
			transform = &entity->GetComponent<Transform>();
		}

		void Update(float frameTime) override
		{
			transform->position += velocity * frameTime;

			if(onOutOfBounds == nullptr) return;

			if (left() < 0) 
				onOutOfBounds(sf::Vector2f{ 1.f, 0.f });
			else if (right() > windowWidth) 
				onOutOfBounds(sf::Vector2f{ -1.f, 0.f });

			if (top() < 0) 
				onOutOfBounds(sf::Vector2f{ 0.f, 1.f });
			else if (bottom() > windowHeight) 
				onOutOfBounds(sf::Vector2f{ 0.f, -1.f });
		}

		float x() 		const  noexcept { return transform->x(); }
		float y() 		const  noexcept { return transform->y(); }
		float left() 	const  noexcept { return x() - halfSize.x; }
		float right() 	const  noexcept { return x() + halfSize.x; }
		float top() 	const  noexcept { return y() - halfSize.y; }
		float bottom() 	const  noexcept { return y() + halfSize.y; }

		void SetY(float yValue) { transform->position.y = yValue; }
	};

	// An entity can have a rectangular shape 
	// that can be rendered on screen.
	struct RectangleRenderer : Component
	{
		Game* game{nullptr};
		Transform* transform{nullptr};
		sf::RectangleShape shape;
		sf::Vector2f size;
		std::string textureFilename;
		sf::Texture texture;

		RectangleRenderer(Game* game, const sf::Vector2f& halfSize, const std::string& textureFilename)
			: game{ game }, size{ halfSize * 2.f }, textureFilename{ textureFilename } {}
		
		void Initialize() override
		{	
			transform = &entity->GetComponent<Transform>();

			shape.setSize(size);
			shape.setFillColor(sf::Color::White);
			shape.setOrigin(size.x / 2.f, size.y / 2.f);
			
			texture.loadFromFile(textureFilename);
			shape.setTexture(&texture);
		}

		void Update(float frameTime) override
		{
			shape.setPosition(transform->position);
		}

		void Draw() override;
	};

	// The player ship needs a component to manage
	// keyboard input.
	struct PlayerController : Component
	{
		Transform* transform{ nullptr };
		Physics* physics{nullptr};
		Game* game{ nullptr };
		EntityManager* manager{ nullptr };
		int currentPlayerBullet;

		float const fireRate = 1000.f; // In milliseconds
		float accumulatedTime = fireRate + 1.f;
		
		PlayerController(Game* game, EntityManager* manager, int& currentPlayerBullet)
			: game{ game } , manager{ manager }, currentPlayerBullet{ currentPlayerBullet }   {}

		void Initialize() override
		{	
			// A requirement for `PlayerController` is `Transform` and 'Physics'
			transform = &entity->GetComponent<Transform>();
			physics = &entity->GetComponent<Physics>();
		}

		void Update(FrameTime frameTime)
		{
			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left) && 
				physics->left() > 0)
			{
				physics->velocity.x = -playerShipVelocity;
			}
			else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right) && 
				physics->right() < windowWidth)
			{
				physics->velocity.x = playerShipVelocity;
			}
			else 
			{
				physics->velocity.x = 0;
			}

			accumulatedTime += frameTime;

			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space))
			{			
				if (accumulatedTime > fireRate)
				{
					UsePlayerShipWeapon(transform->position, currentPlayerBullet);

					// Reset Timer
					accumulatedTime = 0.f;
				} 
			}	
		}	

		void UsePlayerShipWeapon(const sf::Vector2f& bulletSpawnLocation, int& currentPlayerBullet);
	};

	// We'll use groups to keep track of our entities.
	enum SpaceInvadersGroup : std::size_t
	{
		PlayerShip,
		OffensiveEnemyShip,
		PlayerBullet,
		EnemyBullet,
		DefensiveEnemyShip
	};

	struct WeaponAIController : Component
	{
		Transform* transform{ nullptr };
		EntityManager* manager{ nullptr };
		int currentEnemyBullet;

		float nextFireTimePoint = 0.f;
		float accumulatedTime = 0.f;

		WeaponAIController(EntityManager* manager, int& currentEnemyBullet)
			: manager{ manager }, currentEnemyBullet{currentEnemyBullet}   {}

		void Initialize() override
		{
			// A requirement for `WeaponAIController` is `Transform`.
			transform = &entity->GetComponent<Transform>();

			GetNextFireTimePoint();
		}

		void Update(float frameTime) override
		{
			accumulatedTime += frameTime;
		
			if (accumulatedTime > nextFireTimePoint)
			{
				UseEnemyShipWeapon(transform->position, currentEnemyBullet);

				GetNextFireTimePoint();

				// Reset timer
				accumulatedTime = 0.f;
			}
		}

		void GetNextFireTimePoint()
		{
			nextFireTimePoint = static_cast<float>((1 + (rndEngine() % 15)) * 1000); // In milliseconds
		}

		void UseEnemyShipWeapon(const sf::Vector2f& bulletSpawnLocation, int& currentEnemyBullet);
	};

	template<class T1, class T2> bool IsIntersecting(T1& A, T2& B) noexcept
	{
		return A.right() >= B.left() && A.left() <= B.right() 
				&& A.bottom() >= B.top() && A.top() <= B.bottom();
	}

	void TestCollisionPlayerBulletWithEnemyShip(Entity& playerBullet, Entity& enemyShip) noexcept
	{	
		auto& cpPlayerBulletPhysics(playerBullet.GetComponent<Physics>());
		auto& cpEnemyShipPhysics(enemyShip.GetComponent<Physics>());

		if (!cpPlayerBulletPhysics.entity->IsActive()) return;
		if(!IsIntersecting(cpPlayerBulletPhysics, cpEnemyShipPhysics)) return;
		
		// Destroy Enemy Ship 
		enemyShip.Destroy();
		// Disable Player Bullet  
		playerBullet.Disable();
	}

	void TestCollisionEnemyBulletWithPlayerShip(Entity& enemyBullet, Entity& playerShip) noexcept
	{
		auto& cpEnemyBulletPhysics(enemyBullet.GetComponent<Physics>());
		auto& cpPlayerShipPhysics(playerShip.GetComponent<Physics>());

		if (!cpEnemyBulletPhysics.entity->IsActive()) return;
		if (!IsIntersecting(cpEnemyBulletPhysics, cpPlayerShipPhysics)) return;

		// Destroy Player Ship   
		playerShip.Destroy();
		// Disable Enemy Bullet 
		enemyBullet.Disable();
	}

	struct Game
	{	
		// Useful fields
		FrameTime lastFt{0.f}, currentSlice{0.f}; 
		bool running{false};
		EntityManager manager;

		int currentPlayerBullet = 0;
		int currentEnemyBullet = 0;

		// Create a window
		sf::RenderWindow window{ sf::VideoMode(windowWidth, windowHeight), "Space Invaders - Components" };

		// Creating entities can be done through simple "factory" functions.
		Entity& CreatePlayerShip()
		{
			sf::Vector2f halfSize{ playerShipWidth / 2.f, playerShipHeight / 2.f };
			auto& entity(manager.AddEntity());

			entity.AddComponent<Transform>(sf::Vector2f{ windowWidth / 2.f, windowHeight - 60.f });
			entity.AddComponent<Physics>(halfSize);
			entity.AddComponent<RectangleRenderer>(this, halfSize, "data/playerShip1_blue.png");
			entity.AddComponent<PlayerController>(this, &manager, currentPlayerBullet);

			entity.AddGroup(SpaceInvadersGroup::PlayerShip);

			return entity;
		}

		Entity& CreatePlayerBullet()
		{
			sf::Vector2f halfSize{ bulletWidth / 2.f, bulletHeight / 2.f };
			auto& entity(manager.AddEntity());

			entity.AddComponent<Transform>(sf::Vector2f{ windowWidth / 2.f, windowHeight / 2.f });
			entity.AddComponent<Physics>(halfSize);
			entity.AddComponent<RectangleRenderer>(this, halfSize, "data/laserBlue03.png");

			auto& cPhysics(entity.GetComponent<Physics>());
			cPhysics.velocity = sf::Vector2f{ 0, -bulletVelocity };
			
			// Disable Bullet
			entity.Disable();

			entity.AddGroup(SpaceInvadersGroup::PlayerBullet);

			return entity;
		}

		void CreateAllPlayerBullets()
		{
			for (size_t i = 0; i < maxPlayerBullets; i++)
			{
				CreatePlayerBullet();
			}
		}

		Entity& CreateEnemyBullet()
		{
			sf::Vector2f halfSize{ bulletWidth / 2.f, bulletHeight / 2.f };
			auto& entity(manager.AddEntity());

			entity.AddComponent<Transform>(sf::Vector2f{ windowWidth / 2.f, windowHeight / 2.f });
			entity.AddComponent<Physics>(halfSize);
			entity.AddComponent<RectangleRenderer>(this, halfSize, "data/laserRed03.png");

			auto& cPhysics(entity.GetComponent<Physics>());
			cPhysics.velocity = sf::Vector2f{ 0, bulletVelocity };

			// Disable Bullet
			entity.Disable();

			entity.AddGroup(SpaceInvadersGroup::EnemyBullet);

			return entity;
		}

		void CreateAllEnemyBullets()
		{
			for (size_t i = 0; i < maxEnemyBullets; i++)
			{
				CreateEnemyBullet();
			}
		}

		Entity& CreateOffensiveEnemyShip(const sf::Vector2f& position)
		{
			sf::Vector2f halfSize{ enemyShipWidth / 2.f, enemyShipHeight / 2.f };
			auto& entity(manager.AddEntity());
			
			entity.AddComponent<Transform>(position);
			entity.AddComponent<Physics>(halfSize);
			entity.AddComponent<RectangleRenderer>(this, halfSize, "data/enemyRed2.png");
			entity.AddComponent<WeaponAIController>(&manager, currentEnemyBullet);

			auto& cPhysics(entity.GetComponent<Physics>());
			cPhysics.velocity = sf::Vector2f{ enemyShipVelocity, 0 };

			entity.AddGroup(SpaceInvadersGroup::OffensiveEnemyShip);

			return entity;
		}

		Entity& CreateDefensiveEnemyShip(const sf::Vector2f& position)
		{
			sf::Vector2f halfSize{ enemyShipWidth / 2.f, enemyShipHeight / 2.f };
			auto& entity(manager.AddEntity());

			entity.AddComponent<Transform>(position);
			entity.AddComponent<Physics>(halfSize);
			entity.AddComponent<RectangleRenderer>(this, halfSize, "data/enemyGreen3.png");

			auto& cPhysics(entity.GetComponent<Physics>());
			cPhysics.velocity = sf::Vector2f{ enemyShipVelocity, 0 };

			entity.AddGroup(SpaceInvadersGroup::DefensiveEnemyShip);

			return entity;
		}

		void CreateEnemyShips()
		{
			for (int iX{ 0 }; iX < countEnemyColumn; ++iX)
			{
				for (int iY{ 0 }; iY < countEnemyRow; ++iY)
				{
					if (iY % 2 == 0)
					{
						CreateOffensiveEnemyShip(sf::Vector2f{
							(iX + 1) * (enemyShipWidth + 5) + 22,
							(iY + 1) * (enemyShipHeight + 5) });
					}
					else
					{
						CreateDefensiveEnemyShip(sf::Vector2f{
							(iX + 1) * (enemyShipWidth + 5) + 22,
							(iY + 1) * (enemyShipHeight + 5) });
					}
				}
			}
		}

		Game()
		{
			window.setFramerateLimit(240);

			CreatePlayerShip();
			CreateEnemyShips();
			CreateAllPlayerBullets();
			CreateAllEnemyBullets();
		}

		void Run()
		{
			running = true;

			while(running)
			{
				auto timePoint1(std::chrono::high_resolution_clock::now());
				
				window.clear(sf::Color::Black);

				InputPhase(lastFt);
				UpdatePhase();
				DrawPhase();		

				auto timePoint2(std::chrono::high_resolution_clock::now());
				auto elapsedTime(timePoint2 - timePoint1);
				FrameTime ft{ std::chrono::duration_cast<
					std::chrono::duration<float, std::milli >> (elapsedTime).count() };
				
				lastFt = ft;	
			}	
		}

		void InputPhase(FrameTime frameTime)
		{
			sf::Event event;
			while(window.pollEvent(event)) 
			{ 
				if (event.type == sf::Event::Closed)
				{
					window.close();
					break;
				}
			}

			if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape)) 
				running = false;
		}

		void UpdatePhase()
		{
			currentSlice += lastFt;
			for(; currentSlice >= ftSlice; currentSlice -= ftSlice)
			{	
				manager.Refresh();
				manager.Update(ftStep);

				float leftEnemyShipBorder = 0.f;
				float rightEnemyShipBorder = windowWidth;
				bool needToChangeEnemyShipDirection = false;

				// We get our entities by group...
				auto& playerShip(manager.GetEntitiesByGroup(PlayerShip));
				auto& playerBullets(manager.GetEntitiesByGroup(PlayerBullet));
				auto& offensiveEnemyShips(manager.GetEntitiesByGroup(OffensiveEnemyShip));
				auto& defensiveEnemyShips(manager.GetEntitiesByGroup(DefensiveEnemyShip));
				auto& enemyBullets(manager.GetEntitiesByGroup(EnemyBullet));

				// ...and perform collision tests on them.
				for (auto& pB : playerBullets)
				{
					for (auto& deS : defensiveEnemyShips)
					{
						TestCollisionPlayerBulletWithEnemyShip(*pB, *deS);
						
						auto& cPhysics = deS->GetComponent<Physics>();
						float left = cPhysics.left();
						float right = cPhysics.right();
						if (left < leftEnemyShipBorder || right > rightEnemyShipBorder)
						{
							needToChangeEnemyShipDirection = true;
						}
					}

					for (auto& oeS : offensiveEnemyShips)
					{
						TestCollisionPlayerBulletWithEnemyShip(*pB, *oeS);
						
						auto& cPhysics = oeS->GetComponent<Physics>();
						float left = cPhysics.left();
						float right = cPhysics.right();
						if (left < leftEnemyShipBorder || right > rightEnemyShipBorder)
						{
							needToChangeEnemyShipDirection = true;
						}
					}

					// Check player Bullets if they go out of bounds
					auto& cPhysics = pB->GetComponent<Physics>();

					if (cPhysics.bottom() < 0.f)
					{
						pB->Disable();
					}
				}

				for (auto& eB : enemyBullets)
				{
					for (auto& pS : playerShip)
						TestCollisionEnemyBulletWithPlayerShip(*eB, *pS);

					// Check enemy Bullets if they go out of bounds
					auto& cPhysics = eB->GetComponent<Physics>();

					if (cPhysics.bottom() > windowHeight)
					{
						eB->Disable();
					}
				}

				if (needToChangeEnemyShipDirection)
				{
					ChangeEnemiesShipDirection();
				}
			}
		}

		void ChangeEnemiesShipDirection()
		{
			auto& offensiveEnemyShips(manager.GetEntitiesByGroup(OffensiveEnemyShip));
			auto& defensiveEnemyShips(manager.GetEntitiesByGroup(DefensiveEnemyShip));

			for (auto& deS : defensiveEnemyShips)
			{
				auto& cPhysics = deS->GetComponent<Physics>();

				cPhysics.velocity.x = -cPhysics.velocity.x;

				// Move down
				cPhysics.SetY(cPhysics.y() + 5.f);
			}

			for (auto& oeS : offensiveEnemyShips)
			{
				auto& cPhysics = oeS->GetComponent<Physics>();

				cPhysics.velocity.x = -cPhysics.velocity.x;

				// Move down
				cPhysics.SetY(cPhysics.y() + 5.f);
			}
		}

		void DrawPhase() 
		{ 
			manager.Draw(); 
			window.display(); 
		}

		void Render(const sf::Drawable& mDrawable) 
		{ 
			window.draw(mDrawable); 
		}
	};

	void RectangleRenderer::Draw()
	{
		game->Render(shape);
	}

	void PlayerController::UsePlayerShipWeapon(const sf::Vector2f& bulletSpawnLocation, int& currentPlayerBullet)
	{
		if (currentPlayerBullet == maxPlayerBullets)
		{
			currentPlayerBullet = 0;
		}

		auto playerBulletList = manager->GetEntitiesByGroup(SpaceInvadersGroup::PlayerBullet);

		auto& cPlayerBulletTransform(playerBulletList[currentPlayerBullet]->GetComponent<Transform>());

		cPlayerBulletTransform.position.x = bulletSpawnLocation.x;
		cPlayerBulletTransform.position.y = bulletSpawnLocation.y - 45;

		playerBulletList[currentPlayerBullet]->Enable();

		currentPlayerBullet++;
	}

	void WeaponAIController::UseEnemyShipWeapon(const sf::Vector2f& bulletSpawnLocation, int& currentEnemyBullet)	
	{
		if (currentEnemyBullet == maxEnemyBullets)
		{
			currentEnemyBullet = 0;
		}

		auto& enemyBulletList = manager->GetEntitiesByGroup(SpaceInvadersGroup::EnemyBullet);

		auto& cEnemyBulletTransform(enemyBulletList[currentEnemyBullet]->GetComponent<Transform>());

		cEnemyBulletTransform.position.x = bulletSpawnLocation.x;
		cEnemyBulletTransform.position.y = bulletSpawnLocation.y + 45;

		enemyBulletList[currentEnemyBullet]->Enable();

		currentEnemyBullet++;
	}
}

// Program entry point
int main() 
{	
	SpaceInvaders::Game{}.Run();

	return 0;
}