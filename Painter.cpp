#include "Painter.hpp"

const int Painter::shadowMapSize = 1024;

Painter::BasicLight::BasicLight(ptr<UniformGroup> ug) :
	uLightPosition(ug->AddUniform<float3>()),
	uLightColor(ug->AddUniform<float3>())
{}

Painter::ShadowLight::ShadowLight(ptr<UniformGroup> ug, int samplerNumber) :
	BasicLight(ug),
	uLightTransform(ug->AddUniform<float4x4>()),
	uShadowSampler(samplerNumber)
{}

Painter::LightVariant::LightVariant() :
	ugLight(NEW(UniformGroup(1))),
	uAmbientColor(ugLight->AddUniform<float3>())
{}

Painter::ShaderKey::ShaderKey(int basicLightsCount, int shadowLightsCount, bool skinned) :
basicLightsCount(basicLightsCount), shadowLightsCount(shadowLightsCount), skinned(skinned)
{}

Painter::ShaderKey::operator size_t() const
{
	return basicLightsCount | (shadowLightsCount << 3) | (skinned << 6);
}

Painter::Shader::Shader() {}

Painter::Shader::Shader(ptr<VertexShader> vertexShader, ptr<PixelShader> pixelShader)
: vertexShader(vertexShader), pixelShader(pixelShader) {}

Painter::Painter(ptr<Device> device, ptr<Context> context, ptr<Presenter> presenter, int screenWidth, int screenHeight) :
	device(device),
	context(context),
	presenter(presenter),
	screenWidth(screenWidth),
	screenHeight(screenHeight),

	aPosition(0),
	aNormal(1),
	aTexcoord(2),

	ugShadow(NEW(UniformGroup(0))),
	uShadowWorldViewProj(ugShadow->AddUniform<float4x4>()),

	ugCamera(NEW(UniformGroup(0))),
	uViewProj(ugCamera->AddUniform<float4x4>()),
	uCameraPosition(ugCamera->AddUniform<float3>()),

	ugMaterial(NEW(UniformGroup(2))),
	uDiffuseSampler(0),
	uSpecularSampler(1),

	ugModel(NEW(UniformGroup(3))),
	uWorldViewProj(ugModel->AddUniform<float4x4>()),
	uWorld(ugModel->AddUniform<float4x4>()),

	ubShadow(device->CreateUniformBuffer(ugShadow->GetSize())),
	ubCamera(device->CreateUniformBuffer(ugCamera->GetSize())),
	ubMaterial(device->CreateUniformBuffer(ugMaterial->GetSize())),
	ubModel(device->CreateUniformBuffer(ugModel->GetSize()))
{
	// финализировать uniform группы
	ugShadow->Finalize();
	ugCamera->Finalize();
	ugMaterial->Finalize();
	ugModel->Finalize();

	// создать ресурсы
	// запомнить размеры
	this->screenWidth = screenWidth;
	this->screenHeight = screenHeight;

	//** создать ресурсы
	rbBack = presenter->GetBackBuffer();
	dsbDepth = device->CreateDepthStencilBuffer(screenWidth, screenHeight);
	for(int i = 0; i < maxShadowLightsCount; ++i)
		dsbShadows[i] = device->CreateDepthStencilBuffer(shadowMapSize, shadowMapSize, true);

	shadowSamplerState = device->CreateSamplerState();
	shadowSamplerState->SetWrap(SamplerState::wrapBorder, SamplerState::wrapBorder, SamplerState::wrapBorder);
	{
		float borderColor[] = { 0, 0, 0, 0 };
		shadowSamplerState->SetBorderColor(borderColor);
	}

	//** инициализировать состояния конвейера
	ContextState cleanState;

	// shadow pass
	shadowContextState = cleanState;
	shadowContextState.viewportWidth = shadowMapSize;
	shadowContextState.viewportHeight = shadowMapSize;
	shadowContextState.uniformBuffers[ugShadow->GetSlot()] = ubShadow;

	// варианты света
	for(int basicLightsCount = 0; basicLightsCount <= maxBasicLightsCount; ++basicLightsCount)
		for(int shadowLightsCount = 0; shadowLightsCount <= maxShadowLightsCount; ++shadowLightsCount)
		{
			LightVariant& lightVariant = lightVariants[basicLightsCount][shadowLightsCount];

			// инициализировать uniform'ы
			for(int i = 0; i < basicLightsCount; ++i)
				lightVariant.basicLights.push_back(BasicLight(lightVariant.ugLight));
			for(int i = 0; i < shadowLightsCount; ++i)
				// первые два семплера - для материала
				lightVariant.shadowLights.push_back(ShadowLight(lightVariant.ugLight, i + 2));

			lightVariant.ugLight->Finalize();

			// создать uniform-буфер для параметров
			lightVariant.ubLight = device->CreateUniformBuffer(lightVariant.ugLight->GetSize());

			// инициализировать состояние контекста
			ContextState& cs = lightVariant.csOpaque;
			cs = cleanState;
			cs.viewportWidth = screenWidth;
			cs.viewportHeight = screenHeight;
			cs.renderBuffers[0] = rbBack;
			cs.depthStencilBuffer = dsbDepth;
			cs.uniformBuffers[ugCamera->GetSlot()] = ubCamera;
			cs.uniformBuffers[lightVariant.ugLight->GetSlot()] = lightVariant.ubLight;
			cs.uniformBuffers[ugMaterial->GetSlot()] = ubMaterial;
			cs.uniformBuffers[ugModel->GetSlot()] = ubModel;

			// применить семплеры карт теней
			for(int i = 0; i < shadowLightsCount; ++i)
			{
				ShadowLight& shadowLight = lightVariant.shadowLights[i];
				shadowLight.uShadowSampler.SetTexture(dsbShadows[i]->GetTexture());
				shadowLight.uShadowSampler.SetSamplerState(shadowSamplerState);
				shadowLight.uShadowSampler.Apply(cs);
			}
		}

	//** шейдеры

	// генератор шейдеров и компилятор
	ptr<HlslGenerator> shaderGenerator = NEW(HlslGenerator());
	ptr<DxShaderCompiler> shaderCompiler = NEW(DxShaderCompiler());
	// переменные шейдеров
	Interpolant<float4> tPosition(Semantics::VertexPosition);
	Interpolant<float3> tNormal(Semantics::CustomNormal);
	Interpolant<float2> tTexcoord(Semantics::CustomTexcoord0);
	Interpolant<float3> tWorldPosition(Semantic(Semantics::CustomTexcoord0 + 1));
	Fragment<float4> tTarget(Semantics::TargetColor0);
	// вершинный шейдер
	ptr<ShaderSource> vertexShaderSource = shaderGenerator->Generate(Expression((
		tPosition = mul(aPosition, uWorldViewProj),
		tNormal = mul(aNormal, uWorld.Cast<float3x3>()),
		tTexcoord = aTexcoord,
		tWorldPosition = mul(aPosition, uWorld).Swizzle<float3>("xyz")
		)), ShaderTypes::vertex);
	ptr<VertexShader> vertexShader = device->CreateVertexShader(shaderCompiler->Compile(vertexShaderSource));

	// вершинный шейдер для теней
	ptr<ShaderSource> shadowVertexShaderSource = shaderGenerator->Generate(Expression((
		tPosition = mul(aPosition, uShadowWorldViewProj)
		)), ShaderTypes::vertex);
	shadowContextState.vertexShader = device->CreateVertexShader(shaderCompiler->Compile(shadowVertexShaderSource));

	// варианты шейдеров
	for(int basicLightsCount = 0; basicLightsCount <= maxBasicLightsCount; ++basicLightsCount)
		for(int shadowLightsCount = 0; shadowLightsCount <= maxShadowLightsCount; ++shadowLightsCount)
		{
			ShaderKey shaderKey(basicLightsCount, shadowLightsCount, false);

			LightVariant& lightVariant = lightVariants[basicLightsCount][shadowLightsCount];

			// пиксельный шейдер
			Temp<float4> tmpWorldPosition;
			Temp<float3> tmpNormalizedNormal;
			Temp<float3> tmpToCamera;
			Temp<float3> tmpDiffuse;
			Temp<float3> tmpSpecular;
			Temp<float3> tmpColor;
			Expression shader = (
				tPosition,
				tNormal,
				tTexcoord,
				tWorldPosition,
				tmpWorldPosition = newfloat4(tWorldPosition, 1.0f),
				tmpNormalizedNormal = normalize(tNormal),
				tmpToCamera = normalize(uCameraPosition - tWorldPosition),
				//tmpDiffuse = newfloat3(0, 0, 1),
				//tmpSpecular = newfloat3(0, 1, 0),
				//tmpDiffuse = newfloat3(0.5f, 0.5f, 0.5f),
				//tmpSpecular = newfloat3(0.5f, 0.5f, 0.5f),
				tmpDiffuse = uDiffuseSampler.Sample(tTexcoord),
				tmpSpecular = uSpecularSampler.Sample(tTexcoord),
				tmpColor = lightVariant.uAmbientColor * tmpDiffuse
				);

			// учесть все простые источники света
			for(int i = 0; i < basicLightsCount; ++i)
			{
				BasicLight& basicLight = lightVariant.basicLights[i];

				// направление на источник света
				Temp<float3> tmpToLight;
				shader.Assign((shader,
					tmpToLight = normalize(basicLight.uLightPosition - tWorldPosition)
					));
				// диффузная составляющая
				Value<float3> diffuse =
					tmpDiffuse * max(dot(tmpNormalizedNormal, tmpToLight), 0);
				// specular составляющая
				Value<float3> specular =
					tmpSpecular * pow(max(dot(tmpToLight + tmpToCamera, tmpNormalizedNormal), 0), 4.0f);

				// добавка к цвету
				shader.Assign((shader,
					tmpColor = tmpColor + basicLight.uLightColor * (diffuse + specular)
					));
			}

			// учесть все источники света с тенями
			for(int i = 0; i < shadowLightsCount; ++i)
			{
				ShadowLight& shadowLight = lightVariant.shadowLights[i];

				// направление на источник света
				Temp<float3> tmpToLight;
				shader.Assign((shader,
					tmpToLight = normalize(shadowLight.uLightPosition - tWorldPosition)
					));
				// диффузная составляющая
				Value<float3> diffuse =
					tmpDiffuse * max(dot(tmpNormalizedNormal, tmpToLight), 0);
				// specular составляющая
				Value<float3> specular =
					tmpSpecular * pow(max(dot(tmpToLight + tmpToCamera, tmpNormalizedNormal), 0), 4.0f);

				// тень
				Temp<float4> shadowCoords;
				Temp<float> shadowMultiplier;
				shader.Assign((shader,
					shadowCoords = mul(tmpWorldPosition, shadowLight.uLightTransform),
					shadowCoords = shadowCoords / shadowCoords.Swizzle<float>("w"),
					shadowMultiplier = shadowLight.uShadowSampler.Sample(
						newfloat2(
							(shadowCoords.Swizzle<float>("x") + Value<float>(1.0f)) * Value<float>(0.5f),
							(Value<float>(1.0f) - shadowCoords.Swizzle<float>("y")) * Value<float>(0.5f))) > shadowCoords.Swizzle<float>("z") - Value<float>(0.0001f)
					));

				// добавка к цвету
				shader.Assign((shader,
					tmpColor = tmpColor + shadowLight.uLightColor * shadowMultiplier * (diffuse + specular)
					));
			}

			// вернуть цвет
			shader.Assign((shader,
				tTarget = newfloat4(tmpColor, 1.0f)
				));

			ptr<ShaderSource> pixelShaderSource = shaderGenerator->Generate(shader, ShaderTypes::pixel);
			ptr<PixelShader> pixelShader = device->CreatePixelShader(shaderCompiler->Compile(pixelShaderSource));

			shaders[shaderKey] = Shader(vertexShader, pixelShader);
		}
}

void Painter::BeginShadow(int shadowNumber, const float4x4& shadowViewProj)
{
	// запомнить камеру
	this->shadowViewProj = shadowViewProj;

	// установить состояние конвейера
	context->GetTargetState() = shadowContextState;
	context->GetTargetState().depthStencilBuffer = dsbShadows[shadowNumber];

	// очистить карту теней
	context->ClearDepthStencilBuffer(dsbShadows[shadowNumber], 1.0f);
}

void Painter::DrawShadowModel(const float4x4& worldTransform)
{
	// установить uniform'ы
	uShadowWorldViewProj.SetValue(worldTransform * shadowViewProj);
	// и залить в GPU
	context->SetUniformBufferData(ubShadow, ugShadow->GetData(), ugShadow->GetSize());

	// нарисовать
	context->Draw();
}

void Painter::BeginOpaque(const float4x4& cameraViewProj, const float3& cameraPosition)
{
	// очистить рендербуферы
	float color[4] = { 1, 0, 0, 0 };
	context->ClearRenderBuffer(rbBack, color);
	context->ClearDepthStencilBuffer(dsbDepth, 1.0f);

	// запомнить камеру
	this->cameraViewProj = cameraViewProj;
	this->cameraPosition = cameraPosition;

	// установить uniform'ы камеры
	uViewProj.SetValue(cameraViewProj);
	uCameraPosition.SetValue(cameraPosition);
	// и залить в GPU
	context->SetUniformBufferData(ubCamera, ugCamera->GetData(), ugCamera->GetSize());
}

void Painter::SetLightVariant(int basicLightsCount, int shadowLightsCount)
{
	this->basicLightsCount = basicLightsCount;
	this->shadowLightsCount = shadowLightsCount;
	// установить состояние конвейера
	context->GetTargetState() = lightVariants[basicLightsCount][shadowLightsCount].csOpaque;
}

void Painter::SetAmbientLight(const float3& ambientColor)
{
	lightVariants[basicLightsCount][shadowLightsCount].uAmbientColor.SetValue(ambientColor);
}

void Painter::SetBasicLight(int basicLightNumber, const float3& lightPosition, const float3& lightColor)
{
	// установить параметры
	LightVariant& lightVariant = lightVariants[basicLightsCount][shadowLightsCount];
	BasicLight& basicLight = lightVariant.basicLights[basicLightNumber];
	basicLight.uLightPosition.SetValue(lightPosition);
	basicLight.uLightColor.SetValue(lightColor);
}

void Painter::SetShadowLight(int shadowLightNumber, const float3& lightPosition, const float3& lightColor, const float4x4& lightTransform)
{
	// установить параметры
	LightVariant& lightVariant = lightVariants[basicLightsCount][shadowLightsCount];
	ShadowLight& shadowLight = lightVariant.shadowLights[shadowLightNumber];
	shadowLight.uLightPosition.SetValue(lightPosition);
	shadowLight.uLightColor.SetValue(lightColor);
	shadowLight.uLightTransform.SetValue(lightTransform);
}

void Painter::ApplyLight()
{
	// залить параметры света в GPU
	LightVariant& lightVariant = lightVariants[basicLightsCount][shadowLightsCount];
	context->SetUniformBufferData(lightVariant.ubLight, lightVariant.ugLight->GetData(), lightVariant.ugLight->GetSize());
}

void Painter::SetMaterial(ptr<Texture> diffuseTexture, ptr<Texture> specularTexture)
{
	ContextState& cs = context->GetTargetState();
	uDiffuseSampler.SetTexture(diffuseTexture);
	uDiffuseSampler.Apply(cs);
	uSpecularSampler.SetTexture(specularTexture);
	uSpecularSampler.Apply(cs);
}

void Painter::SetGeometry(ptr<VertexBuffer> vertexBuffer, ptr<IndexBuffer> indexBuffer)
{
	ContextState& state = context->GetTargetState();
	state.vertexBuffer = vertexBuffer;
	state.indexBuffer = indexBuffer;
}

void Painter::DrawOpaqueModel(const float4x4& worldTransform, bool skinned)
{
	// установить uniform'ы модели
	uWorldViewProj.SetValue(worldTransform * cameraViewProj);
	uWorld.SetValue(worldTransform);
	// и залить в GPU
	context->SetUniformBufferData(ubModel, ugModel->GetData(), ugModel->GetSize());

	// установить шейдеры
	std::unordered_map<ShaderKey, Shader>::const_iterator it = shaders.find(ShaderKey(basicLightsCount, shadowLightsCount, skinned));
	if(it == shaders.end())
		THROW_PRIMARY_EXCEPTION("Shader not compiled");
	const Shader& shader = it->second;
	ContextState& cs = context->GetTargetState();
	cs.vertexShader = shader.vertexShader;
	cs.pixelShader = shader.pixelShader;

	// нарисовать
	context->Draw();
}