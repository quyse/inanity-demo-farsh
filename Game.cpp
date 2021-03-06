#include "Game.hpp"
#include "Painter.hpp"
#include "Geometry.hpp"
#include "GeometryFormats.hpp"
#include "Material.hpp"
#include "Skeleton.hpp"
#include "BoneAnimation.hpp"
#include "../inanity/script/lua/State.hpp"
#ifndef ___INANITY_PLATFORM_EMSCRIPTEN
#include "../inanity/inanity-sqlitefs.hpp"
#endif
#include <iostream>

Game* Game::singleGame = 0;

const float Game::hzAFRun1 = 50.0f / 30;
const float Game::hzAFRun2 = 66.0f / 30;
const float Game::hzAFBattle1 = 400.0f / 30;
const float Game::hzAFBattle2 = 450.0f / 30;

Game::Game() :
	heroAnimationTime(hzAFBattle1),
	bloomLimit(10.0f), toneLuminanceKey(0.12f), toneMaxLuminance(3.1f)
{
	singleGame = this;
}

void Game::Run()
{
	try
	{
		ptr<Graphics::System> system = Inanity::Platform::Game::CreateDefaultGraphicsSystem();

		ptr<Graphics::Adapter> adapter = system->GetAdapters()[0];
		device = system->CreateDevice(adapter);
		ptr<Graphics::Monitor> monitor = adapter->GetMonitors()[0];

		int screenWidth = 800;
		int screenHeight = 600;
		bool fullscreen = false;

		ptr<Platform::Window> window = monitor->CreateDefaultWindow(
			"F.A.R.S.H.", screenWidth, screenHeight);
		this->window = window;

		inputManager = Inanity::Platform::Game::CreateInputManager(window);

		ptr<Graphics::MonitorMode> monitorMode;
		if(fullscreen)
			monitorMode = monitor->TryCreateMode(screenWidth, screenHeight);
		presenter = device->CreateWindowPresenter(window, monitorMode);

		context = system->CreateContext(device);

#ifdef ___INANITY_PLATFORM_EMSCRIPTEN
		ptr<FileSystem> shaderCacheFileSystem = NEW(Data::TempFileSystem());
#else
		const char* shadersCacheFileName =
#ifdef _DEBUG
			"shaders_debug"
#else
			"shaders"
#endif
			;
		ptr<FileSystem> shaderCacheFileSystem = NEW(Data::SQLiteFileSystem(shadersCacheFileName));
#endif
			;

		ptr<ShaderCache> shaderCache = NEW(ShaderCache(shaderCacheFileSystem, device,
			device->CreateShaderCompiler(), device->CreateShaderGenerator(), NEW(Crypto::WhirlpoolStream())));

		fileSystem =
#ifdef PRODUCTION
			NEW(Data::BlobFileSystem(Platform::FileSystem::GetNativeFileSystem()->LoadFile("data")))
#else
			NEW(Data::BufferedFileSystem(NEW(Platform::FileSystem("assets"))))
#endif
		;

		geometryFormats = NEW(GeometryFormats());

		painter = NEW(Painter(device, context, presenter, shaderCache, geometryFormats));

		{
			SamplerSettings samplerSettings;
			samplerSettings.SetFilter(SamplerSettings::filterLinear);
			samplerSettings.SetWrap(SamplerSettings::wrapRepeat);
			textureManager = NEW(TextureManager(fileSystem, device, samplerSettings));
		}

		// GUI canvas and fonts
		canvas = Gui::GrCanvas::Create(device, shaderCache);
		{
			ptr<Gui::FontEngine> fontEngine = NEW(Gui::FtEngine());
			ptr<Gui::FontFace> fontFace = fontEngine->LoadFontFace(fileSystem->LoadFile("/DejaVuSans.ttf"));
			const int fontSize = 13;
			ptr<Gui::FontShape> fontShape = fontFace->CreateShape(fontSize);
			ptr<Gui::FontGlyphs> fontGlyphs = fontFace->CreateGlyphs(canvas, fontSize, Gui::FontFace::CreateGlyphsConfig());
			font = NEW(Gui::Font(fontShape, fontGlyphs));
		}

		physicsWorld = NEW(Physics::BtWorld());

		// запустить стартовый скрипт
		ptr<Script::Lua::State> luaState = NEW(Script::Lua::State());
		luaState->Register<Game>();
		luaState->Register<Material>();
		scriptState = luaState;

		ptr<Script::Function> mainScript = scriptState->LoadScript(fileSystem->LoadFile(
#ifdef PRODUCTION
			"/main.luab"
#else
			"/main.lua"
#endif
		));
		mainScript->Run();

		window->SetMouseLock(true);
		window->SetCursorVisible(false);

		try
		{
			window->Run(Handler::Bind(MakePointer(this), &Game::Tick));

			scriptState = 0;
		}
		catch(Exception* exception)
		{
			scriptState = 0;
			THROW_SECONDARY("Error while running game", exception);
		}
	}
	catch(Exception* exception)
	{
		THROW_SECONDARY("Can't initialize game", exception);
	}
}

void Game::Tick()
{
	float frameTime = ticker.Tick();

	static bool theTimePaused = false;

	const float maxAngleChange = frameTime * 50;

	bool shoot = false;

	ptr<Input::Frame> inputFrame = inputManager->GetCurrentFrame();
	while(inputFrame->NextEvent())
	{
		const Input::Event& inputEvent = inputFrame->GetCurrentEvent();

		//std::cout << inputEvent << "[" << inputFrame->GetCurrentState().cursorX << ' ' << inputFrame->GetCurrentState().cursorY << "] ";

		switch(inputEvent.device)
		{
		case Input::Event::deviceKeyboard:
			if(inputEvent.keyboard.type == Input::Event::Keyboard::typeKeyDown)
			{
				switch(inputEvent.keyboard.key)
				{
				case 27: // escape
					window->Close();
					return;
				case 32:
					//physicsCharacter.FastCast<Physics::BtCharacter>()->GetInternalController()->jump();
					break;
#ifndef PRODUCTION
				case 'M':
					try
					{
						scriptState->LoadScript(fileSystem->LoadFile("/console.lua"))->Run();
						std::cout << "console.lua successfully executed.\n";
					}
					catch(Exception* exception)
					{
						std::ostringstream s;
						MakePointer(exception)->PrintStack(s);
						std::cout << s.str() << '\n';
					}
					break;
				case 'Z':
					shoot = true;
					break;
				case 'X':
					theTimePaused = !theTimePaused;
					break;

				case '1':
					bloomLimit -= 0.1f;
					printf("bloomLimit: %f\n", bloomLimit);
					break;
				case '2':
					bloomLimit += 0.1f;
					printf("bloomLimit: %f\n", bloomLimit);
					break;
				case '3':
					toneLuminanceKey -= 0.01f;
					printf("toneLuminanceKey: %f\n", toneLuminanceKey);
					break;
				case '4':
					toneLuminanceKey += 0.01f;
					printf("toneLuminanceKey: %f\n", toneLuminanceKey);
					break;
				case '5':
					toneMaxLuminance -= 0.1f;
					printf("toneMaxLuminance: %f\n", toneMaxLuminance);
					break;
				case '6':
					toneMaxLuminance += 0.1f;
					printf("toneMaxLuminance: %f\n", toneMaxLuminance);
					break;

				case '7':
					zombieMaterial->specular.x -= 0.01f;
					zombieMaterial->specular.y = zombieMaterial->specular.x;
					zombieMaterial->specular.z = zombieMaterial->specular.x;
					printf("specular: %f\n", zombieMaterial->specular.x);
					break;
				case '8':
					zombieMaterial->specular.x += 0.01f;
					zombieMaterial->specular.y = zombieMaterial->specular.x;
					zombieMaterial->specular.z = zombieMaterial->specular.x;
					printf("specular: %f\n", zombieMaterial->specular.x);
					break;
				case '9':
					zombieMaterial->specular.w -= 0.01f;
					printf("glossiness: %f\n", zombieMaterial->specular.w);
					break;
				case '0':
					zombieMaterial->specular.w += 0.01f;
					printf("glossiness: %f\n", zombieMaterial->specular.w);
					break;
				case 'L':
					{
						static bool mouseLock = true;
						mouseLock = !mouseLock;
						window->SetMouseLock(mouseLock);
					}
					break;
				case 'V':
					{
						static bool cursorVisible = false;
						cursorVisible = !cursorVisible;
						window->SetCursorVisible(cursorVisible);
					}
					break;
				default: break;
#endif
				}
			}
			break;
		case Input::Event::deviceMouse:
			switch(inputEvent.mouse.type)
			{
			case Input::Event::Mouse::typeButtonDown:
				shoot = true;
				break;
			case Input::Event::Mouse::typeButtonUp:
				break;
			case Input::Event::Mouse::typeRawMove:
				cameraAlpha -= std::max(std::min(inputEvent.mouse.rawMoveX * 0.005f, maxAngleChange), -maxAngleChange);
				cameraBeta -= std::max(std::min(inputEvent.mouse.rawMoveY * 0.005f, maxAngleChange), -maxAngleChange);
				cameraAlpha -= std::max(std::min(inputEvent.mouse.rawMoveZ * 0.005f, maxAngleChange), -maxAngleChange);
				break;
			default: break;
			}
			break;
		}
	}

	cameraBeta = clamp(cameraBeta, -1.5f, 1.5f);

	vec3 cameraDirection = vec3(cos(cameraAlpha) * cos(cameraBeta), sin(cameraAlpha) * cos(cameraBeta), sin(cameraBeta));
	//vec3 cameraRightDirection = normalize(cross(cameraDirection, vec3(0, 0, 1)));
	//vec3 cameraUpDirection = cross(cameraRightDirection, cameraDirection);

	const Input::State& inputState = inputFrame->GetCurrentState();
	/*
	left up right down Q E
	37 38 39 40
	65 87 68 83 81 69
	*/
	float cameraStep = 5;
	vec3 cameraMove(0, 0, 0);
	vec3 cameraMoveDirectionFront(cos(cameraAlpha), sin(cameraAlpha), 0);
	vec3 cameraMoveDirectionUp(0, 0, 1);
	vec3 cameraMoveDirectionRight = cross(cameraMoveDirectionFront, cameraMoveDirectionUp);
	if(inputState.keyboard[37] || inputState.keyboard[65])
		cameraMove -= cameraMoveDirectionRight * cameraStep;
	if(inputState.keyboard[38] || inputState.keyboard[87])
		cameraMove += cameraMoveDirectionFront * cameraStep;
	if(inputState.keyboard[39] || inputState.keyboard[68])
		cameraMove += cameraMoveDirectionRight * cameraStep;
	if(inputState.keyboard[40] || inputState.keyboard[83])
		cameraMove -= cameraMoveDirectionFront * cameraStep;
	if(inputState.keyboard[81])
		cameraMove -= cameraMoveDirectionUp * cameraStep;
	if(inputState.keyboard[69])
		cameraMove += cameraMoveDirectionUp * cameraStep;

	//heroCharacter->Walk(cameraMove);

	physicsWorld->Simulate(frameTime);

	mat4x4 heroTransform = heroCharacter->GetTransform();
	vec3 heroPosition(heroTransform(0, 3), heroTransform(1, 3), heroTransform(2, 3));
	quat heroOrientation = axis_rotation(vec3(0, 0, 1), cameraAlpha);

	cameraPosition += cameraMove * frameTime;

	if(shoot)
	{
		mat4x4 transform = CreateTranslationMatrix(cameraPosition);
		ptr<Physics::RigidBody> rigidBody = physicsWorld->CreateRigidBody(rigidModels[0].rigidBody->GetShape(), 100, transform);
		rigidBody->ApplyImpulse(vec3(cos(cameraAlpha) * cos(cameraBeta), sin(cameraAlpha) * cos(cameraBeta), sin(cameraBeta)) * 10000.0f, cameraPosition);
		AddRigidModel(rigidModels[0].geometry, rigidModels[0].material, rigidBody);
	}

	static float shootAlpha = 0;
	shootAlpha += frameTime;
	if(shootAlpha > 2.0f)
	{
		shootAlpha = 0;
		vec3 dir(cos(alpha), sin(alpha), 0);
		vec3 pos = vec3(10, 10, 5) + dir * 10.0f;
		mat4x4 transform = CreateTranslationMatrix(pos);
		ptr<Physics::RigidBody> rigidBody = physicsWorld->CreateRigidBody(rigidModels[0].rigidBody->GetShape(), 100, transform);
		rigidBody->ApplyImpulse(dir * -1000.0f, pos);
		AddRigidModel(rigidModels[0].geometry, rigidModels[0].material, rigidBody);
	}

	alpha += frameTime;

	int screenWidth = presenter->GetWidth();
	int screenHeight = presenter->GetHeight();
	painter->Resize(screenWidth, screenHeight);

	mat4x4 viewMatrix = CreateLookAtMatrix(cameraPosition, cameraPosition + cameraDirection, vec3(0, 0, 1));
	mat4x4 projMatrix = CreateProjectionPerspectiveFovMatrix(3.1415926535897932f / 4, float(screenWidth) / float(screenHeight), 0.1f, 100.0f);

	// зарегистрировать все объекты
	painter->BeginFrame(frameTime);
	painter->SetCamera(projMatrix * viewMatrix, cameraPosition);
	painter->SetAmbientColor(ambientColor);

	for(size_t i = 0; i < staticModels.size(); ++i)
	{
		const StaticModel& model = staticModels[i];
		painter->AddModel(model.material, model.geometry, model.transform);
	}

	for(size_t i = 0; i < rigidModels.size(); ++i)
	{
		const RigidModel& model = rigidModels[i];
		painter->AddModel(model.material, model.geometry, model.rigidBody->GetTransform());
	}

	for(size_t i = 0; i < staticLights.size(); ++i)
	{
		ptr<StaticLight> light = staticLights[i];
		if(light->shadow)
			painter->AddShadowLight(light->position, light->color, light->transform);
		else
			painter->AddBasicLight(light->position, light->color);
	}

	if(!theTimePaused)
		heroAnimationTime += frameTime;
	while(heroAnimationTime >= hzAFBattle2)
		heroAnimationTime += hzAFBattle1 - hzAFBattle2;

	// TEST: set time to zero
	//heroAnimationTime = 0;

	// TEST: rotate hero constantly
#if 1
	static float heroTime = 0;
	heroTime += frameTime;
	heroOrientation = axis_rotation(vec3(0, 0, 1), heroTime);
#endif

	heroAnimationFrame->Setup(heroPosition, heroOrientation, heroAnimationTime);
	//vec3 shouldBeHeroPosition = heroPosition - (heroAnimationFrame->animationWorldPositions[0] - heroPosition) * vec3(1, 1, 0);
	//heroAnimationFrame->Setup(shouldBeHeroPosition, heroOrientation, heroAnimationTime);
	painter->AddSkinnedModel(heroMaterial, heroGeometry, heroAnimationFrame);
	zombieAnimationFrame->Setup(heroPosition, heroOrientation, heroAnimationTime);
	painter->AddSkinnedModel(zombieMaterial, zombieGeometry, zombieAnimationFrame);
	if(0)
	for(size_t i = 0; i < heroAnimationFrame->animationWorldPositions.size(); ++i)
		painter->AddModel(
			staticModels[0].material,
			staticModels[0].geometry,
			fromEigen((
				Eigen::Translation3f(toEigen(heroAnimationFrame->animationWorldPositions[i])) *
				toEigenQuat(heroAnimationFrame->animationWorldOrientations[i]) *
				Eigen::Scaling(Eigen::Vector3f(0.1f, 0.1f, 0.1f))
			).matrix().eval())
		);
	circularAnimationFrame->Setup(heroPosition, heroOrientation, heroAnimationTime);
	painter->AddModel(circularMaterial, circularGeometry,
		fromEigen((
			Eigen::Translation3f(toEigen(circularAnimationFrame->animationWorldPositions[0])) *
			toEigenQuat(circularAnimationFrame->animationWorldOrientations[0])
		).matrix().eval())
	);
	axeAnimationFrame->Setup(heroPosition, heroOrientation, heroAnimationTime);
	painter->AddModel(axeMaterial, axeGeometry,
		fromEigen((
			Eigen::Translation3f(toEigen(axeAnimationFrame->animationWorldPositions[0])) *
			toEigenQuat(axeAnimationFrame->animationWorldOrientations[0])
		).matrix().eval())
	);

	painter->SetupPostprocess(bloomLimit, toneLuminanceKey, toneMaxLuminance);

	painter->Draw();

	canvas->SetContext(context);

	// fps
	{
		Context::LetFrameBuffer lfb(context, presenter->GetFrameBuffer());
		Context::LetViewport lv(context, screenWidth, screenHeight);

		static int tickCount = 0;
		static const int needTickCount = 100;
		static float allTicksTime = 0;
		allTicksTime += frameTime;
		static float lastAllTicksTime = 0;
		if(++tickCount >= needTickCount)
		{
			lastAllTicksTime = allTicksTime;
			allTicksTime = 0;
			tickCount = 0;
		}
		char fpsString[64];
		sprintf(fpsString, "frameTime: %.6f sec, FPS: %.6f", lastAllTicksTime / needTickCount, needTickCount / lastAllTicksTime);
		font->DrawString(canvas, fpsString, 'Zyyy', vec2(19.0f, (float)screenHeight - 21.0f), vec4(1, 1, 1, 1));
		font->DrawString(canvas, fpsString, 'Zyyy', vec2(20.0f, (float)screenHeight - 20.0f), vec4(1, 0, 0, 1));
		canvas->Flush();
	}

	presenter->Present();
}

ptr<Game> Game::Get()
{
	return singleGame;
}

ptr<Texture> Game::LoadTexture(const String& fileName)
{
	return textureManager->Get(fileName);
}

ptr<Geometry> Game::LoadGeometry(const String& fileName)
{
	return NEW(Geometry(
		device->CreateStaticVertexBuffer(fileSystem->LoadFile(fileName + ".vertices"), geometryFormats->vl),
		device->CreateStaticIndexBuffer(fileSystem->LoadFile(fileName + ".indices"), sizeof(short))
	));
}

ptr<Geometry> Game::LoadSkinnedGeometry(const String& fileName)
{
	return NEW(Geometry(
		device->CreateStaticVertexBuffer(fileSystem->LoadFile(fileName + ".vertices"), geometryFormats->vlSkinned),
		device->CreateStaticIndexBuffer(fileSystem->LoadFile(fileName + ".indices"), sizeof(short))
	));
}

ptr<Skeleton> Game::LoadSkeleton(const String& fileName)
{
	return Skeleton::Deserialize(fileSystem->LoadStream(fileName));
}

ptr<BoneAnimation> Game::LoadBoneAnimation(const String& fileName, ptr<Skeleton> skeleton)
{
	if(!skeleton)
	{
		std::vector<Skeleton::Bone> bones(1);
		bones[0].originalWorldPosition = vec3(0, 0, 0);
		bones[0].originalRelativePosition = vec3(0, 0, 0);
		bones[0].parent = 0;
		skeleton = NEW(Skeleton(bones));
	}
	return BoneAnimation::Deserialize(fileSystem->LoadStream(fileName), skeleton);
}

ptr<Physics::Shape> Game::CreatePhysicsBoxShape(const vec3& halfSize)
{
	return physicsWorld->CreateBoxShape(halfSize);
}

ptr<Physics::RigidBody> Game::CreatePhysicsRigidBody(ptr<Physics::Shape> physicsShape, float mass, const vec3& position)
{
	Eigen::Affine3f startTransform = Eigen::Affine3f::Identity();
	startTransform.translate(toEigen(position));
	return physicsWorld->CreateRigidBody(physicsShape, mass, fromEigen(startTransform.matrix()));
}

void Game::AddStaticModel(ptr<Geometry> geometry, ptr<Material> material, const vec3& position)
{
	StaticModel model;
	model.geometry = geometry;
	model.material = material;
	Eigen::Affine3f transform = Eigen::Affine3f::Identity();
	transform.translate(toEigen(position));
	model.transform = fromEigen(transform.matrix());
	staticModels.push_back(model);
}

void Game::AddRigidModel(ptr<Geometry> geometry, ptr<Material> material, ptr<Physics::RigidBody> physicsRigidBody)
{
	RigidModel model;
	model.geometry = geometry;
	model.material = material;
	model.rigidBody = physicsRigidBody;
	rigidModels.push_back(model);
}

void Game::AddStaticRigidBody(ptr<Physics::RigidBody> rigidBody)
{
	staticRigidBodies.push_back(rigidBody);
}

ptr<StaticLight> Game::AddStaticLight()
{
	ptr<StaticLight> light = NEW(StaticLight());
	staticLights.push_back(light);
	return light;
}

void Game::SetDecalMaterial(ptr<Material> decalMaterial)
{
	this->decalMaterial = decalMaterial;
}

void Game::SetAmbient(float r, float g, float b)
{
	this->ambientColor = vec3(r, g, b);
}

void Game::SetZombieParams(ptr<Material> material, ptr<Geometry> geometry, ptr<Skeleton> skeleton, ptr<BoneAnimation> animation)
{
	this->zombieMaterial = material;
	this->zombieGeometry = geometry;
	this->zombieSkeleton = skeleton;
	this->zombieAnimation = animation;
}

void Game::SetHeroParams(ptr<Material> material, ptr<Geometry> geometry, ptr<Skeleton> skeleton, ptr<BoneAnimation> animation)
{
	this->heroMaterial = material;
	this->heroGeometry = geometry;
	this->heroSkeleton = skeleton;
	this->heroAnimation = animation;
}

void Game::SetAxeParams(ptr<Material> material, ptr<Geometry> geometry, ptr<BoneAnimation> animation)
{
	this->axeMaterial = material;
	this->axeGeometry = geometry;
	this->axeAnimation = animation;
}

void Game::SetCircularParams(ptr<Material> material, ptr<Geometry> geometry, ptr<BoneAnimation> animation)
{
	this->circularMaterial = material;
	this->circularGeometry = geometry;
	this->circularAnimation = animation;
}

void Game::PlaceHero(float x, float y, float z)
{
	Eigen::Affine3f startTransform = Eigen::Affine3f::Identity();
	startTransform.translate(Eigen::Vector3f(x, y, z));
	mat4x4 initialTransform = fromEigen(startTransform.matrix());
	heroCharacter = physicsWorld->CreateCharacter(physicsWorld->CreateCapsuleShape(0.2f, 1.4f), initialTransform);
	heroAnimationFrame = NEW(BoneAnimationFrame(heroAnimation));
	circularAnimationFrame = NEW(BoneAnimationFrame(circularAnimation));
	zombieAnimationFrame = NEW(BoneAnimationFrame(zombieAnimation));
	axeAnimationFrame = NEW(BoneAnimationFrame(axeAnimation));
}

void Game::PlaceCamera(const vec3& position, float alpha, float beta)
{
	this->cameraPosition = position;
	this->cameraAlpha = alpha;
	this->cameraBeta = beta;
}

//******* Game::StaticLight

StaticLight::StaticLight() :
	position(-1, 0, 0), target(0, 0, 0), angle(3.1415926535897932f / 4), nearPlane(0.1f), farPlane(100.0f), color(1, 1, 1), shadow(false)
{
	UpdateTransform();
}

void StaticLight::UpdateTransform()
{
	transform =
		CreateProjectionPerspectiveFovMatrix(angle, 1.0f, nearPlane, farPlane) *
		CreateLookAtMatrix(position, target, vec3(0, 0, 1));
}

void StaticLight::SetPosition(const vec3& position)
{
	this->position = position;
	UpdateTransform();
}

void StaticLight::SetTarget(const vec3& target)
{
	this->target = target;
	UpdateTransform();
}

void StaticLight::SetProjection(float angle, float nearPlane, float farPlane)
{
	this->angle = angle * 3.1415926535897932f / 180;
	this->nearPlane = nearPlane;
	this->farPlane = farPlane;
	UpdateTransform();
}

void StaticLight::SetColor(const vec3& color)
{
	this->color = color;
}

void StaticLight::SetShadow(bool shadow)
{
	this->shadow = shadow;
}
