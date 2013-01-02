#include "../inanity2/inanity-base.hpp"
#include "../inanity2/inanity-graphics.hpp"
#include "../inanity2/inanity-dx.hpp"
#include "../inanity2/inanity-shaders.hpp"
#include "../inanity2/inanity-input.hpp"
#include "../inanity2/inanity-physics.hpp"
#include "../inanity2/inanity-bullet.hpp"
#include <sstream>
#include <iostream>
#include <fstream>

#define TEST_GRAPHICS_DIRECTX
//#define TEST_GRAPHICS_OPENGL

using namespace Inanity;
using namespace Inanity::Graphics;
using namespace Inanity::Graphics::Shaders;

#include "test.hpp"

struct Vertex
{
	float3 position;
	float3 normal;
	float2 texcoord;
};

struct TestShader
{
	Attribute<float4> aPosition;
	Attribute<float3> aNormal;
	Attribute<float2> aTexcoord;

	ptr<UniformGroup> ugCamera;
	Uniform<float4x4> uViewProj;
	Uniform<float3> uCameraPosition;

	ptr<UniformGroup> ugLight;
	Uniform<float3> uLightPosition;
	Uniform<float3> uLightDirection;

	ptr<UniformGroup> ugMaterial;
	Sampler<float3, float2> uDiffuseSampler;

	ptr<UniformGroup> ugModel;
	Uniform<float4x4> uWorldViewProj;
	Uniform<float4x4> uWorld;

	TestShader() :
		aPosition(0),
		aNormal(1),
		aTexcoord(2),

		ugCamera(NEW(UniformGroup(0))),
		uViewProj(ugCamera->AddUniform<float4x4>()),
		uCameraPosition(ugCamera->AddUniform<float3>()),

		ugLight(NEW(UniformGroup(1))),
		uLightPosition(ugLight->AddUniform<float3>()),
		uLightDirection(ugLight->AddUniform<float3>()),

		ugMaterial(NEW(UniformGroup(2))),
		uDiffuseSampler(0),

		ugModel(NEW(UniformGroup(3))),
		uWorldViewProj(ugModel->AddUniform<float4x4>()),
		uWorld(ugModel->AddUniform<float4x4>())
	{
		ugCamera->Finalize();
		ugLight->Finalize();
		ugMaterial->Finalize();
		ugModel->Finalize();
	}
};

class Game : public Object
{
private:
	ptr<Window> window;
	ptr<Device> device;
	ptr<Context> context;
	ptr<Presenter> presenter;

	ptr<Input::Manager> inputManager;

	ptr<UniformBuffer> ubCamera;
	ptr<UniformBuffer> ubLight;
	ptr<UniformBuffer> ubMaterial;
	ptr<UniformBuffer> ubModel;
	ptr<UniformGroup> ugCamera;
	ptr<UniformGroup> ugLight;
	ptr<UniformGroup> ugMaterial;
	ptr<UniformGroup> ugModel;
	float alpha;

	ContextState drawingState;

	PresentMode mode;
	TestShader t;

	long long lastTick;
	float tickCoef;

	float3 cameraPosition;
	float cameraAlpha, cameraBeta;

	ptr<Physics::World> physicsWorld;
	struct Cube
	{
		ptr<Physics::RigidBody> rigidBody;
		float3 scale;
		Cube(ptr<Physics::RigidBody> rigidBody, const float3& scale = float3(1, 1, 1))
		: rigidBody(rigidBody), scale(scale) {}
	};
	std::vector<Cube> cubes;

public:
	Game() : lastTick(0), cameraPosition(-20, 0, 10), cameraAlpha(0), cameraBeta(0)
	{
		tickCoef = 1.0f / Time::GetTicksPerSecond();
	}

	void onTick(int)
	{
		long long tick = Time::GetTicks();
		float frameTime = lastTick ? (tick - lastTick) * tickCoef : 0;
		lastTick = tick;

		const float maxAngleChange = frameTime * 50;

		ptr<Input::Frame> inputFrame = inputManager->GetCurrentFrame();
		while(inputFrame->NextEvent())
		{
			const Input::Event& inputEvent = inputFrame->GetCurrentEvent();

			//PrintInputEvent(inputEvent);

			switch(inputEvent.device)
			{
			case Input::Event::deviceKeyboard:
				if(inputEvent.keyboard.type == Input::Event::Keyboard::typeKeyDown)
				{
					if(inputEvent.keyboard.key == 27)
					{
						window->Close();
						return;
					}
				}
				break;
			case Input::Event::deviceMouse:
				switch(inputEvent.mouse.type)
				{
				case Input::Event::Mouse::typeButtonDown:
					break;
				case Input::Event::Mouse::typeButtonUp:
					break;
				case Input::Event::Mouse::typeMove:
					cameraAlpha -= std::max(std::min(inputEvent.mouse.offsetX * 0.005f, maxAngleChange), -maxAngleChange);
					cameraBeta -= std::max(std::min(inputEvent.mouse.offsetY * 0.005f, maxAngleChange), -maxAngleChange);
					break;
				}
				break;
			}
		}

		float3 cameraDirection = float3(cos(cameraAlpha) * cos(cameraBeta), sin(cameraAlpha) * cos(cameraBeta), sin(cameraBeta));
		float3 cameraRightDirection = normalize(cross(cameraDirection, float3(0, 0, 1)));
		float3 cameraUpDirection = cross(cameraRightDirection, cameraDirection);

		const Input::State& inputState = inputFrame->GetCurrentState();
		/*
		left up right down Q E
		37 38 39 40
		65 87 68 83 81 69
		*/
		float cameraStep = frameTime * 10;
		if(inputState.keyboard[37] || inputState.keyboard[65])
			cameraPosition -= cameraRightDirection * cameraStep;
		if(inputState.keyboard[38] || inputState.keyboard[87])
			cameraPosition += cameraDirection * cameraStep;
		if(inputState.keyboard[39] || inputState.keyboard[68])
			cameraPosition += cameraRightDirection * cameraStep;
		if(inputState.keyboard[40] || inputState.keyboard[83])
			cameraPosition -= cameraDirection * cameraStep;
		if(inputState.keyboard[81])
			cameraPosition -= cameraUpDirection * cameraStep;
		if(inputState.keyboard[69])
			cameraPosition += cameraUpDirection * cameraStep;

		float color[4] = { 0, 0, 0, 0 };
		context->ClearRenderBuffer(presenter->GetBackBuffer(), color);
		context->ClearDepthStencilBuffer(drawingState.depthStencilBuffer, 1.0f);

		context->Reset();

		alpha += frameTime;
		t.uCameraPosition.SetValue(cameraPosition);

		float4x4 viewMatrix = CreateLookAtMatrix(cameraPosition, cameraPosition + cameraDirection, float3(0, 0, 1));
		float4x4 projMatrix = CreateProjectionPerspectiveFovMatrix(3.1415926535897932f / 4, float(mode.width) / float(mode.height), 1, 10000);
		t.uViewProj.SetValue(viewMatrix * projMatrix);
		//float3 lightPosition = float3(400 * cos(alpha / 5), 400 * sin(alpha / 5), -50);
		float3 lightPosition = cameraPosition + cameraRightDirection * (-3.0f);
		t.uLightPosition.SetValue(lightPosition);
		t.uLightDirection.SetValue(normalize(lightPosition));

		context->SetUniformBufferData(ubCamera, ugCamera->GetData(), ugCamera->GetSize());
		context->SetUniformBufferData(ubLight, ugLight->GetData(), ugLight->GetSize());
		context->SetUniformBufferData(ubMaterial, ugMaterial->GetData(), ugMaterial->GetSize());

		context->GetTargetState() = drawingState;

		physicsWorld->Simulate(frameTime);

		for(size_t i = 0; i < cubes.size(); ++i)
		{
			float4x4 worldMatrix = CreateScalingMatrix(cubes[i].scale) * cubes[i].rigidBody->GetTransform();
			t.uWorldViewProj.SetValue(worldMatrix * viewMatrix * projMatrix);
			t.uWorld.SetValue(worldMatrix);
			context->SetUniformBufferData(ubModel, ugModel->GetData(), ugModel->GetSize());
			context->Draw();
		}

		presenter->Present();
	}

	void process()
	{
#ifdef TEST_GRAPHICS_DIRECTX
		ptr<Graphics::System> system = NEW(DxSystem());
#endif
#ifdef TEST_GRAPHICS_OPENGL
		ptr<Graphics::System> system = NEW(GlSystem());
#endif

		device = system->CreatePrimaryDevice();
		ptr<Win32Window> window = system->CreateDefaultWindow().FastCast<Win32Window>();
		this->window = window;
		window->SetTitle("F.A.R.S.H.");

		inputManager = NEW(Input::RawManager(window->GetHWND()));
		window->SetInputManager(inputManager);

#ifdef _DEBUG
		mode.width = 640;
		mode.height = 480;
		mode.fullscreen = false;
#else
		mode.width = GetSystemMetrics(SM_CXSCREEN);
		mode.height = GetSystemMetrics(SM_CYSCREEN);
		mode.fullscreen = true;
#endif
		mode.pixelFormat = PixelFormats::intR8G8B8A8;
		presenter = device->CreatePresenter(window->CreateOutput(), mode);

		context = device->GetContext();

		// разметка
		std::vector<Layout::Element> layoutElements;
		layoutElements.push_back(Layout::Element(DataTypes::Float3, 0, 0));
		layoutElements.push_back(Layout::Element(DataTypes::Float3, 12, 1));
		layoutElements.push_back(Layout::Element(DataTypes::Float2, 24, 2));
		ptr<Layout> layout = NEW(Layout(layoutElements, sizeof(Vertex)));

		// шейдер :)
		Interpolant<float4> tPosition(Semantics::VertexPosition);
		Interpolant<float3> tNormal(Semantics::CustomNormal);
		Interpolant<float2> tTexcoord(Semantics::CustomTexcoord0);
		Interpolant<float3> tWorldPosition(Semantic(Semantics::CustomTexcoord0 + 1));

		Temp<float4> tmpPosition;
		Temp<float3> tmpNormal;
		Temp<float> tmpDiffuse;

		Fragment<float4> tTarget(Semantics::TargetColor0);

		Expression vertexShader = (
			tPosition = mul(t.aPosition, t.uWorldViewProj),
			tNormal = mul(t.aNormal, t.uWorld.Cast<float3x3>()),
			tTexcoord = t.aTexcoord,
			tWorldPosition = mul(t.aPosition, t.uWorld).Swizzle<float3>("xyz")
			);

		Expression pixelShader = (
			tPosition,
			tNormal,
			tTexcoord,
			tWorldPosition,
			tmpNormal = normalize(tNormal),
			tmpDiffuse = pow(max(0, dot(tmpNormal, normalize(normalize(t.uLightPosition - tWorldPosition) + normalize(t.uCameraPosition - tWorldPosition)))), 8.0f),
			tTarget = newfloat4(tmpDiffuse, tmpDiffuse, tmpDiffuse, 1)
			//tTarget = newfloat4(1, 0, 0, 1)
			//tTarget = newfloat4((tNormal.Swizzle<float>("x") + Value<float>(1)) / Value<float>(2), (tNormal.Swizzle<float>("y") + Value<float>(1)) / Value<float>(2), 0, 1)
			);

		ptr<HlslGenerator> shaderGenerator = NEW(HlslGenerator());
		ptr<ShaderSource> vertexShaderSource = shaderGenerator->Generate(vertexShader, ShaderTypes::vertex);
		ptr<ShaderSource> pixelShaderSource = shaderGenerator->Generate(pixelShader, ShaderTypes::pixel);

		ptr<DxShaderCompiler> shaderCompiler = NEW(DxShaderCompiler());
		ptr<File> vertexShaderBinary = shaderCompiler->Compile(vertexShaderSource);
		ptr<File> pixelShaderBinary = shaderCompiler->Compile(pixelShaderSource);

		ptr<FileSystem> fs = FolderFileSystem::GetNativeFileSystem();
		fs->SaveFile(vertexShaderSource->GetCode(), "vs.fx");
		fs->SaveFile(pixelShaderSource->GetCode(), "ps.fx");
		fs->SaveFile(vertexShaderBinary, "vs.fxo");
		fs->SaveFile(pixelShaderBinary, "ps.fxo");

		drawingState.viewportWidth = mode.width;
		drawingState.viewportHeight = mode.height;
		drawingState.renderBuffers[0] = presenter->GetBackBuffer();
		drawingState.depthStencilBuffer = device->CreateDepthStencilBuffer(mode.width, mode.height);
		drawingState.vertexShader = device->CreateVertexShader(vertexShaderBinary);
		drawingState.pixelShader = device->CreatePixelShader(pixelShaderBinary);

		ugCamera = t.ugCamera;
		ugLight = t.ugLight;
		ugMaterial = t.ugMaterial;
		ugModel = t.ugModel;

		ubCamera = device->CreateUniformBuffer(ugCamera->GetSize());
		drawingState.uniformBuffers[ugCamera->GetSlot()] = ubCamera;
		ubLight = device->CreateUniformBuffer(ugLight->GetSize());
		drawingState.uniformBuffers[ugLight->GetSlot()] = ubLight;
		ubMaterial = device->CreateUniformBuffer(ugMaterial->GetSize());
		drawingState.uniformBuffers[ugMaterial->GetSlot()] = ubMaterial;
		ubModel = device->CreateUniformBuffer(ugModel->GetSize());
		drawingState.uniformBuffers[ugModel->GetSlot()] = ubModel;

		alpha = 0;

#if 0
		ptr<File> vertexBufferFile = NEW(MemoryFile(sizeof(Vertex) * 3));
		Vertex* vertexBufferData = (Vertex*)vertexBufferFile->GetData();
		vertexBufferData[0].position = float3(-0.5f, -0.5f, 0);
		vertexBufferData[1].position = float3(0, 0.5f, 0);
		vertexBufferData[2].position = float3(0.5, 0, 0);
		drawingState.vertexBuffer = device->CreateVertexBuffer(vertexBufferFile, layout);
		ptr<File> indexBufferFile = NEW(MemoryFile(sizeof(short) * 3));
		short* indexBufferData = (short*)indexBufferFile->GetData();
		indexBufferData[0] = 0;
		indexBufferData[1] = 1;
		indexBufferData[2] = 2;
		drawingState.indexBuffer = device->CreateIndexBuffer(indexBufferFile, sizeof(short));
#else
//		drawingState.vertexBuffer = device->CreateVertexBuffer(fs->LoadFile("circular.geo.vertices"), layout);
//		drawingState.indexBuffer = device->CreateIndexBuffer(fs->LoadFile("circular.geo.indices"), layout);
		drawingState.vertexBuffer = device->CreateVertexBuffer(fs->LoadFile("box.geo.vertices"), layout);
		drawingState.indexBuffer = device->CreateIndexBuffer(fs->LoadFile("box.geo.indices"), layout);
#endif

		physicsWorld = NEW(Physics::BtWorld());
		ptr<Physics::Shape> physicsShape = physicsWorld->CreateBoxShape(float3(20, 20, 1));
		cubes.push_back(Cube(physicsWorld->CreateRigidBody(physicsShape, 0, CreateTranslationMatrix(0, 0, 0)), float3(20, 20, 1)));
		physicsShape = physicsWorld->CreateBoxShape(float3(1, 1, 1));
		for(int i = 0; i < 5; ++i)
			for(int j = 0; j < 5; ++j)
				for(int k = 0; k < 5; ++k)
					cubes.push_back(physicsWorld->CreateRigidBody(physicsShape, 10, CreateTranslationMatrix(i * 4 + k * 0.5f - 2, j * 4 + k * 0.2f - 2, k * 4 + 10)));

		window->Run(Win32Window::ActiveHandler::CreateDelegate(MakePointer(this), &Game::onTick));
	}
};

#ifdef _DEBUG
int main()
#else
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, INT)
#endif
{
	//freopen("output.txt", "w", stdout);

	try
	{
		MakePointer(NEW(Game()))->process();
	}
	catch(Exception* exception)
	{
		std::ostringstream s;
		MakePointer(exception)->PrintStack(s);
		std::cout << s.str() << '\n';
	}

	return 0;
}
