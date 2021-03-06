/*
Copyright(c) 2016-2018 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =================================
#include "Renderer.h"
#include "Rectangle.h"
#include "Material.h"
#include "Mesh.h"
#include "Grid.h"
#include "Font.h"
#include "Deferred/ShaderVariation.h"
#include "Deferred/LightShader.h"
#include "Deferred/GBuffer.h"
#include "../RHI/RHI_Shader.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/D3D11/D3D11_Device.h"
#include "../RHI/D3D11/D3D11_RenderTexture.h"
#include "../RHI/D3D11/D3D11_Sampler.h"
#include "../RHI/D3D11/D3D11_Shader.h"
#include "../RHI/D3D11/D3D11_ConstantBuffer.h"
#include "../Core/Context.h"
#include "../Core/EventSystem.h"
#include "../Scene/Actor.h"
#include "../Scene/Components/Transform.h"
#include "../Scene/Components/Renderable.h"
#include "../Scene/Components/Skybox.h"
#include "../Scene/Components/LineRenderer.h"
#include "../Physics/Physics.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "../Logging/Log.h"
#include "../Resource/ResourceManager.h"
#include "../Scene/TransformationGizmo.h"
//============================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

#define GIZMO_MAX_SIZE 5.0f
#define GIZMO_MIN_SIZE 0.1f

namespace Directus
{
	static Physics* g_physics				= nullptr;
	static ResourceManager* g_resourceMng	= nullptr;
	unsigned long Renderer::m_flags;

	Renderer::Renderer(Context* context) : Subsystem(context)
	{
		m_skybox					= nullptr;
		m_camera					= nullptr;
		m_texEnvironment			= nullptr;
		m_lineRenderer				= nullptr;
		m_nearPlane					= 0.0f;
		m_farPlane					= 0.0f;
		m_rhi						= nullptr;
		m_flags						= 0;
		m_flags						|= Render_SceneGrid;
		m_flags						|= Render_Light;
		m_flags						|= Render_Bloom;
		m_flags						|= Render_FXAA;
		m_flags						|= Render_Sharpening;
		m_flags						|= Render_ChromaticAberration;
		m_flags						|= Render_Correction;

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, EVENT_HANDLER(Render));
		SUBSCRIBE_TO_EVENT(EVENT_SCENE_RESOLVED, EVENT_HANDLER_VARIANT(Renderables_Acquire));
	}

	Renderer::~Renderer()
	{

	}

	bool Renderer::Initialize()
	{
		// Get required subsystems
		m_rhi = m_context->GetSubsystem<RHI>();
		if (!m_rhi->IsInitialized())
		{
			LOG_ERROR("Renderer::Initialize: Invalid RHI.");
			return false;
		}
		g_resourceMng	= m_context->GetSubsystem<ResourceManager>();
		g_physics		= m_context->GetSubsystem<Physics>();

		// Get standard resource directories
		string fontDir			= g_resourceMng->GetStandardResourceDirectory(Resource_Font);
		string shaderDirectory	= g_resourceMng->GetStandardResourceDirectory(Resource_Shader);
		string textureDirectory = g_resourceMng->GetStandardResourceDirectory(Resource_Texture);

		// Load a font (used for performance metrics)
		m_font = make_unique<Font>(m_context, fontDir + "CalibriBold.ttf", 12, Vector4(0.7f, 0.7f, 0.7f, 1.0f));
		// Make a grid (used in editor)
		m_grid = make_unique<Grid>(m_context);

		RenderTargets_Create(Settings::Get().GetResolutionWidth(), Settings::Get().GetResolutionHeight());

		// SAMPLERS
		{
			m_samplerPointWrapAlways		= make_unique<D3D11_Sampler>(m_rhi, Texture_Sampler_Point,			Texture_Address_Wrap,	Texture_Comparison_Always);
			m_samplerPointClampAlways		= make_unique<D3D11_Sampler>(m_rhi, Texture_Sampler_Point,			Texture_Address_Clamp,	Texture_Comparison_Always);
			m_samplerPointClampGreater		= make_unique<D3D11_Sampler>(m_rhi, Texture_Sampler_Point,			Texture_Address_Clamp,	Texture_Comparison_GreaterEqual);
			m_samplerLinearClampGreater		= make_unique<D3D11_Sampler>(m_rhi, Texture_Sampler_Linear,			Texture_Address_Clamp,	Texture_Comparison_GreaterEqual);
			m_samplerLinearWrapAlways		= make_unique<D3D11_Sampler>(m_rhi, Texture_Sampler_Linear,			Texture_Address_Wrap,	Texture_Comparison_Always);
			m_samplerBilinearWrapAlways		= make_unique<D3D11_Sampler>(m_rhi, Texture_Sampler_Bilinear,		Texture_Address_Wrap,	Texture_Comparison_Always);
			m_samplerAnisotropicWrapAlways	= make_unique<D3D11_Sampler>(m_rhi, Texture_Sampler_Anisotropic,	Texture_Address_Wrap,	Texture_Comparison_Always);
		}

		// SHADERS
		{
			// Light
			m_shaderLight = make_unique<LightShader>();
			m_shaderLight->Compile(shaderDirectory + "Light.hlsl", m_rhi);

			// Line
			m_shaderLine = make_unique<RHI_Shader>(m_context);
			m_shaderLine->Compile(shaderDirectory + "Line.hlsl", Input_PositionColor);
			m_shaderLine->AddBuffer(CB_Matrix_Matrix_Matrix, VertexShader);

			// Depth
			m_shaderLightDepth = make_unique<RHI_Shader>(m_context);
			m_shaderLightDepth->Compile(shaderDirectory + "ShadowingDepth.hlsl", Input_Position);
			m_shaderLightDepth->AddBuffer(CB_Matrix_Matrix_Matrix, VertexShader);

			// Grid
			m_shaderGrid = make_unique<RHI_Shader>(m_context);
			m_shaderGrid->Compile(shaderDirectory + "Grid.hlsl", Input_PositionColor);
			m_shaderGrid->AddBuffer(CB_Matrix, VertexShader);

			// Font
			m_shaderFont = make_unique<RHI_Shader>(m_context);
			m_shaderFont->Compile(shaderDirectory + "Font.hlsl", Input_PositionTexture);
			m_shaderFont->AddBuffer(CB_Matrix_Vector4, Global);

			// Texture
			m_shaderTexture = make_unique<RHI_Shader>(m_context);
			m_shaderTexture->Compile(shaderDirectory + "Texture.hlsl", Input_PositionTexture);
			m_shaderTexture->AddBuffer(CB_Matrix, VertexShader);

			// FXAA
			m_shaderFXAA = make_unique<RHI_Shader>(m_context);
			m_shaderFXAA->AddDefine("PASS_FXAA");
			m_shaderFXAA->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderFXAA->AddBuffer(CB_Matrix_Vector2, Global);

			// Sharpening
			m_shaderSharpening = make_unique<RHI_Shader>(m_context);
			m_shaderSharpening->AddDefine("PASS_SHARPENING");
			m_shaderSharpening->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);	
			m_shaderSharpening->AddBuffer(CB_Matrix_Vector2, Global);

			// Sharpening
			m_shaderChromaticAberration = make_unique<RHI_Shader>(m_context);
			m_shaderChromaticAberration->AddDefine("PASS_CHROMATIC_ABERRATION");
			m_shaderChromaticAberration->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);	
			m_shaderChromaticAberration->AddBuffer(CB_Matrix_Vector2, Global);

			// Blur Box
			m_shaderBlurBox = make_unique<RHI_Shader>(m_context);
			m_shaderBlurBox->AddDefine("PASS_BLUR_BOX");
			m_shaderBlurBox->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);	
			m_shaderBlurBox->AddBuffer(CB_Matrix_Vector2, Global);

			// Blur Gaussian Horizontal
			m_shaderBlurGaussianH = make_unique<RHI_Shader>(m_context);
			m_shaderBlurGaussianH->AddDefine("PASS_BLUR_GAUSSIAN_H");
			m_shaderBlurGaussianH->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);		
			m_shaderBlurGaussianH->AddBuffer(CB_Matrix_Vector2, Global);

			// Blur Gaussian Vertical
			m_shaderBlurGaussianV = make_unique<RHI_Shader>(m_context);
			m_shaderBlurGaussianV->AddDefine("PASS_BLUR_GAUSSIAN_V");
			m_shaderBlurGaussianV->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderBlurGaussianV->AddBuffer(CB_Matrix_Vector2, Global);

			// Bloom - bright
			m_shaderBloom_Bright = make_unique<RHI_Shader>(m_context);
			m_shaderBloom_Bright->AddDefine("PASS_BRIGHT");
			m_shaderBloom_Bright->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderBloom_Bright->AddBuffer(CB_Matrix_Vector2, Global);

			// Bloom - blend
			m_shaderBloom_BlurBlend = make_unique<RHI_Shader>(m_context);
			m_shaderBloom_BlurBlend->AddDefine("PASS_BLEND_ADDITIVE");
			m_shaderBloom_BlurBlend->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderBloom_BlurBlend->AddBuffer(CB_Matrix, VertexShader);

			// Tone-mapping
			m_shaderCorrection = make_unique<RHI_Shader>(m_context);
			m_shaderCorrection->AddDefine("PASS_CORRECTION");
			m_shaderCorrection->Compile(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);		
			m_shaderCorrection->AddBuffer(CB_Matrix_Vector2, Global);

			// Transformation gizmo
			m_shaderTransformationGizmo = make_unique<RHI_Shader>(m_context);
			m_shaderTransformationGizmo->Compile(shaderDirectory + "TransformationGizmo.hlsl", Input_PositionTextureTBN);
			m_shaderTransformationGizmo->AddBuffer(CB_Matrix_Vector3_Vector3, Global);

			// Shadowing (shadow mapping & SSAO)
			m_shaderShadowing = make_unique<RHI_Shader>(m_context);
			m_shaderShadowing->Compile(shaderDirectory + "Shadowing.hlsl", Input_PositionTexture);
			m_shaderShadowing->AddBuffer(CB_Shadowing, Global);
		}

		// TEXTURES
		{
			// Noise texture (used by SSAO shader)
			m_texNoiseMap = make_unique<RHI_Texture>(m_context);
			m_texNoiseMap->LoadFromFile(textureDirectory + "noise.png");
			m_texNoiseMap->SetType(TextureType_Normal);

			// Gizmo icons
			m_gizmoTexLightDirectional = make_unique<RHI_Texture>(m_context);
			m_gizmoTexLightDirectional->LoadFromFile(textureDirectory + "sun.png");
			m_gizmoTexLightDirectional->SetType(TextureType_Albedo);
			m_gizmoTexLightPoint = make_unique<RHI_Texture>(m_context);
			m_gizmoTexLightPoint->LoadFromFile(textureDirectory + "light_bulb.png");
			m_gizmoTexLightPoint->SetType(TextureType_Albedo);
			m_gizmoTexLightSpot = make_unique<RHI_Texture>(m_context);
			m_gizmoTexLightSpot->LoadFromFile(textureDirectory + "flashlight.png");
			m_gizmoTexLightSpot->SetType(TextureType_Albedo);
			m_gizmoRectLight = make_unique<Rectangle>(m_context);
		}

		return true;
	}

	void Renderer::SetRenderTarget(void* renderTarget, bool clear /*= true*/)
	{
		auto renderTexture = (D3D11_RenderTexture*)renderTarget;
		if (renderTexture)
		{
			renderTexture->SetAsRenderTarget();
			if (clear) renderTexture->Clear(GetClearColor());
			return;
		}

		m_rhi->Bind_BackBufferAsRenderTarget();
		m_rhi->SetBackBufferViewport();
		if (clear) m_rhi->Clear(GetClearColor());
	}

	void Renderer::SetRenderTarget(const shared_ptr<D3D11_RenderTexture>& renderTexture)
	{
		SetRenderTarget(renderTexture.get());
	}

	void* Renderer::GetFrame()
	{
		return m_renderTexPong ? m_renderTexPong->GetShaderResourceView() : nullptr;
	}

	void Renderer::Present()
	{
		m_rhi->Present();
	}

	void Renderer::Render()
	{
		if (!m_rhi || !m_rhi->IsInitialized())
			return;

		PROFILE_FUNCTION_BEGIN();
		Profiler::Get().Reset();

		// If there is a camera, render the scene
		if (m_camera)
		{
			m_mV					= m_camera->GetViewMatrix();
			m_mV_base				= m_camera->GetBaseViewMatrix();
			m_mP_perspective		= m_camera->GetProjectionMatrix();
			m_mP_orthographic		= Matrix::CreateOrthographicLH((float)Settings::Get().GetResolutionWidth(), (float)Settings::Get().GetResolutionHeight(), m_nearPlane, m_farPlane);		
			m_wvp_perspective		= m_mV * m_mP_perspective;
			m_wvp_baseOrthographic	= m_mV_base * m_mP_orthographic;
			m_nearPlane				= m_camera->GetNearPlane();
			m_farPlane				= m_camera->GetFarPlane();

			// If there is nothing to render clear to camera's color and present
			if (m_renderables.empty())
			{
				m_rhi->Clear(m_camera->GetClearColor());
				m_rhi->Present();
				return;
			}

			Pass_DepthDirectionalLight(m_directionalLight);
		
			Pass_GBuffer();
			
			Pass_PreLight(
				m_gbuffer->GetShaderResource(GBuffer_Target_Normal),	// IN:	Texture			- Normal
				m_gbuffer->GetShaderResource(GBuffer_Target_Depth),		// IN:	Texture			- Depth
				m_texNoiseMap->GetShaderResource(),						// IN:	Texture			- Normal noise
				m_renderTexPing.get(),									// IN:	Render texture		
				m_renderTexShadowing.get()								// OUT: Render texture	- Shadowing (Shadow mapping + SSAO)
			);

			Pass_Light(
				m_renderTexShadowing->GetShaderResourceView(),	// IN:	Texture			- Shadowing (Shadow mapping + SSAO)
				m_renderTexPing.get()							// OUT: Render texture	- Result
			);

			Pass_PostLight(
				m_renderTexPing,	// IN:	Render texture - Deferred pass result
				m_renderTexPing2,	// IN:	Render texture - A spare one
				m_renderTexPong		// OUT: Render texture - Result
			);
		}		
		else // If there is no camera, clear to black
		{
			m_rhi->Clear(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
		}

		PROFILE_FUNCTION_END();
	}

	void Renderer::SetBackBufferSize(int width, int height)
	{
		Settings::Get().SetViewport(width, height);
		m_rhi->SetResolution(width, height);
		m_rhi->SetBackBufferViewport((float)width, (float)height);
	}

	const RHI_Viewport& Renderer::GetViewportBackBuffer()
	{
		return m_rhi->GetBackBufferViewport();
	}

	void Renderer::SetResolution(int width, int height)
	{
		// Return if resolution already set
		if (Settings::Get().GetResolution().x == width && Settings::Get().GetResolution().y == height)
			return;

		// Return if resolution is invalid
		if (width <= 0 || height <= 0)
		{
			LOG_WARNING("Renderer::SetResolutionInternal: Invalid resolution");
			return;
		}

		// Make sure we are pixel perfect
		width	-= (width	% 2 != 0) ? 1 : 0;
		height	-= (height	% 2 != 0) ? 1 : 0;

		Settings::Get().SetResolution(Vector2((float)width, (float)height));
		RenderTargets_Create(width, height);
		LOGF_INFO("Renderer::SetResolution:: Resolution was set to %dx%d", width, height);
	}

	const Vector2& Renderer::GetViewportInternal()
	{
		// The internal (frame) viewport equals the resolution
		return Settings::Get().GetResolution();
	}

	void Renderer::Clear()
	{
		m_renderables.clear();
		m_renderables.shrink_to_fit();

		m_lights.clear();
		m_lights.shrink_to_fit();

		m_directionalLight	= nullptr;
		m_skybox			= nullptr;
		m_lineRenderer		= nullptr;
		m_camera			= nullptr;
	}

	void Renderer::RenderTargets_Create(int width, int height)
	{
		// Resize everything
		m_gbuffer.reset();
		m_gbuffer = make_unique<GBuffer>(m_rhi, width, height);

		m_quad.reset();
		m_quad = make_unique<Rectangle>(m_context);
		m_quad->Create(0, 0, (float)width, (float)height);

		m_renderTexPing.reset();
		m_renderTexPing = make_unique<D3D11_RenderTexture>(m_rhi, width, height, false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexPing2.reset();
		m_renderTexPing2 = make_unique<D3D11_RenderTexture>(m_rhi, width, height, false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexPong.reset();
		m_renderTexPong = make_unique<D3D11_RenderTexture>(m_rhi, width, height, false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexShadowing.reset();
		m_renderTexShadowing = make_unique<D3D11_RenderTexture>(m_rhi, int(width * 0.5f), int(height * 0.5f), false, Texture_Format_R32G32_FLOAT);
	}

	//= RENDERABLES ============================================================================================
	void Renderer::Renderables_Acquire(const Variant& renderables)
	{
		PROFILE_FUNCTION_BEGIN();

		Clear();
		auto renderablesVec = VARIANT_GET_FROM(vector<weak_ptr<Actor>>, renderables);

		for (const auto& renderable : renderablesVec)
		{
			Actor* actor = renderable.lock().get();
			if (!actor)
				continue;

			// Get renderables
			m_renderables.emplace_back(actor);

			// Get lights
			if (auto light = actor->GetComponent<Light>().lock())
			{
				m_lights.emplace_back(light.get());
				if (light->GetLightType() == LightType_Directional)
				{
					m_directionalLight = light.get();
				}
			}

			// Get skybox
			if (auto skybox = actor->GetComponent<Skybox>().lock())
			{
				m_skybox = skybox.get();
				m_lineRenderer = actor->GetComponent<LineRenderer>().lock().get(); // Hush hush...
			}

			// Get camera
			if (auto camera = actor->GetComponent<Camera>().lock())
			{
				m_camera = camera.get();
			}
		}
		Renderables_Sort(&m_renderables);

		PROFILE_FUNCTION_END();
	}

	void Renderer::Renderables_Sort(vector<Actor*>* renderables)
	{
		if (renderables->size() <= 1)
			return;

		sort(renderables->begin(), renderables->end(),[](Actor* a, Actor* b)
		{
			// Get renderable component
			auto a_renderable = a->GetRenderable_PtrRaw();
			auto b_renderable = b->GetRenderable_PtrRaw();

			// Validate renderable components
			if (!a_renderable || !b_renderable)
				return false;

			// Get geometry parents
			auto a_geometryModel = a_renderable->Geometry_Model();
			auto b_geometryModel = b_renderable->Geometry_Model();

			// Validate geometry parents
			if (!a_geometryModel || !b_geometryModel)
				return false;

			// Get materials
			auto a_material = a_renderable->Material_Ref();
			auto b_material = b_renderable->Material_Ref();

			if (!a_material || !b_material)
				return false;

			// Get key for models
			auto a_keyModel = a_geometryModel->GetResourceID();
			auto b_keyModel = b_geometryModel->GetResourceID();

			// Get key for shaders
			auto a_keyShader = a_material->GetShader().lock()->GetResourceID();
			auto b_keyShader = b_material->GetShader().lock()->GetResourceID();

			// Get key for materials
			auto a_keyMaterial = a_material->GetResourceID();
			auto b_keyMaterial = b_material->GetResourceID();

			auto a_key = 
				(((unsigned long long)a_keyModel)		<< 48u)	| 
				(((unsigned long long)a_keyShader)		<< 32u)	|
				(((unsigned long long)a_keyMaterial)	<< 16u);

			auto b_key = 
				(((unsigned long long)b_keyModel)		<< 48u)	|
				(((unsigned long long)b_keyShader)		<< 32u)	|
				(((unsigned long long)b_keyMaterial)	<< 16u);
	
			return a_key < b_key;
		});
	}
	//==========================================================================================================

	//= PASSES =================================================================================================
	void Renderer::Pass_DepthDirectionalLight(Light* light)
	{
		if (!light || !light->GetCastShadows())
			return;

		PROFILE_FUNCTION_BEGIN();

		m_rhi->EventBegin("Pass_DepthDirectionalLight");
		m_rhi->EnableDepth(true);
		m_shaderLightDepth->Bind();

		for (unsigned int i = 0; i < light->ShadowMap_GetCount(); i++)
		{
			light->ShadowMap_SetRenderTarget(i);
			m_rhi->EventBegin("Pass_ShadowMap_" + to_string(i));
			for (const auto& actor : m_renderables)
			{
				// Get renderable and material
				Renderable* obj_renderable = actor->GetRenderable_PtrRaw();
				Material* obj_material = obj_renderable ? obj_renderable->Material_Ref() : nullptr;

				if (!obj_renderable || !obj_material)
					continue;

				// Get geometry
				Model* obj_geometry = obj_renderable->Geometry_Model();
				if (!obj_geometry)
					continue;

				// Bind geometry
				if (m_currentlyBoundGeometry != obj_geometry->GetResourceID())
				{
					obj_geometry->Geometry_Bind();
					m_rhi->Set_PrimitiveTopology(PrimitiveTopology_TriangleList);
					m_currentlyBoundGeometry = obj_geometry->GetResourceID();
				}

				// Skip meshes that don't cast shadows
				if (!obj_renderable->GetCastShadows())
					continue;

				// Skip transparent meshes (for now)
				if (obj_material->GetOpacity() < 1.0f)
					continue;

				// skip objects outside of the view frustum
				//if (!m_directionalLight->IsInViewFrustrum(obj_renderable, i))
					//continue;

				m_shaderLightDepth->Bind_Buffer(actor->GetTransform_PtrRaw()->GetWorldTransform() * light->ComputeViewMatrix() * light->ShadowMap_ComputeProjectionMatrix(i));
				m_rhi->DrawIndexed(obj_renderable->Geometry_IndexCount(), obj_renderable->Geometry_IndexOffset(), obj_renderable->Geometry_VertexOffset());
				Profiler::Get().m_drawCalls++;
			}
			m_rhi->EventEnd();
		}

		// Reset pipeline state tracking
		m_currentlyBoundGeometry = 0;
		
		m_rhi->EnableDepth(false);
		m_rhi->EventEnd();

		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_rhi)
			return;

		PROFILE_FUNCTION_BEGIN();
		m_rhi->EventBegin("Pass_GBuffer");

		m_gbuffer->SetAsRenderTarget();
		m_gbuffer->Clear();

		// Bind sampler 
		m_rhi->Bind_Sampler(0, m_samplerAnisotropicWrapAlways->GetSamplerState());

		for (auto actor : m_renderables)
		{
			// Get renderable and material
			Renderable* obj_renderable	= actor->GetRenderable_PtrRaw();
			Material* obj_material		= obj_renderable ? obj_renderable->Material_Ref() : nullptr;

			if (!obj_renderable || !obj_material)
				continue;

			// Get geometry and shader
			Model* obj_geometry			= obj_renderable->Geometry_Model();
			ShaderVariation* obj_shader	= obj_material->GetShader().lock().get();

			if (!obj_geometry || !obj_shader)
				continue;

			// Skip transparent objects (for now)
			if (obj_material->GetOpacity() < 1.0f)
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(obj_renderable))
				continue;

			// set face culling (changes only if required)
			m_rhi->SetCullMode(obj_material->GetCullMode());

			// Bind geometry
			if (m_currentlyBoundGeometry != obj_geometry->GetResourceID())
			{	
				obj_geometry->Geometry_Bind();
				m_currentlyBoundGeometry = obj_geometry->GetResourceID();
			}

			// Bind shader
			if (m_currentlyBoundShader != obj_shader->GetResourceID())
			{
				obj_shader->Bind();
				obj_shader->Bind_PerFrameBuffer(m_camera);
				m_currentlyBoundShader = obj_shader->GetResourceID();
			}

			// Bind material
			if (m_currentlyBoundMaterial != obj_material->GetResourceID())
			{
				obj_shader->Bind_PerMaterialBuffer(obj_material);
				m_rhi->Bind_Textures(RESOURCES_FROM_VECTOR(obj_material->GetShaderResources()));
				m_currentlyBoundMaterial = obj_material->GetResourceID();
			}

			// UPDATE PER OBJECT BUFFER
			auto mWorld	= actor->GetTransform_PtrRaw()->GetWorldTransform();
			obj_shader->Bind_PerObjectBuffer(mWorld, m_mV, m_mP_perspective);
		
			// Render			
			m_rhi->DrawIndexed(obj_renderable->Geometry_IndexCount(), obj_renderable->Geometry_IndexOffset(), obj_renderable->Geometry_VertexOffset());
			Profiler::Get().m_meshesRendered++;

		} // Actor/MESH ITERATION

		// Reset pipeline state tracking
		m_currentlyBoundGeometry	= 0;
		m_currentlyBoundShader		= 0;
		m_currentlyBoundMaterial	= 0;

		m_rhi->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_PreLight(void* inTextureNormal, void* inTextureDepth, void* inTextureNormalNoise, void* inRenderTexure, void* outRenderTextureShadowing)
	{
		PROFILE_FUNCTION_BEGIN();
		m_rhi->EventBegin("Pass_PreLight");

		m_quad->SetBuffer();
		m_rhi->Set_PrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhi->SetCullMode(Cull_Back);

		// Shadow mapping + SSAO
		Pass_Shadowing(inTextureNormal, inTextureDepth, inTextureNormalNoise, m_directionalLight, inRenderTexure);

		// Blur the shadows and the SSAO
		Pass_Blur(((D3D11_RenderTexture*)inRenderTexure)->GetShaderResourceView(), outRenderTextureShadowing, Settings::Get().GetResolution());

		m_rhi->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_Light(void* inTextureShadowing, void* outRenderTexture)
	{
		if (!m_shaderLight->IsCompiled())
			return;

		PROFILE_FUNCTION_BEGIN();
		m_rhi->EventBegin("Pass_Light");

		m_rhi->EnableDepth(false);

		// Set render target
		SetRenderTarget(outRenderTexture, false);

		// Update buffers
		m_shaderLight->Bind();
		m_shaderLight->UpdateMatrixBuffer(Matrix::Identity, m_mV, m_mV_base, m_mP_perspective, m_mP_orthographic);
		m_shaderLight->UpdateMiscBuffer(m_lights, m_camera);
		m_rhi->Bind_Sampler(0, m_samplerAnisotropicWrapAlways->GetSamplerState());

		//= Update textures ===========================================================
		m_texArray.clear();
		m_texArray.shrink_to_fit();
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Albedo));
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Normal));
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Depth));
		m_texArray.emplace_back(m_gbuffer->GetShaderResource(GBuffer_Target_Specular));
		m_texArray.emplace_back(inTextureShadowing);
		m_texArray.emplace_back(m_renderTexPong->GetShaderResourceView()); // previous frame for SSR
		m_texArray.emplace_back(m_skybox ? m_skybox->GetShaderResource() : nullptr);

		m_rhi->Bind_Textures(RESOURCES_FROM_VECTOR(m_texArray));
		//=============================================================================

		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhi->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_PostLight(shared_ptr<D3D11_RenderTexture>& inRenderTexture1, shared_ptr<D3D11_RenderTexture>& inRenderTexture2, shared_ptr<D3D11_RenderTexture>& outRenderTexture)
	{
		PROFILE_FUNCTION_BEGIN();
		m_rhi->EventBegin("Pass_PostLight");

		m_quad->SetBuffer();
		m_rhi->Set_PrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhi->SetCullMode(Cull_Back);

		// Keep track of render target swapping
		bool swaped = false;
		auto SwapTargets = [&swaped, &inRenderTexture1, &outRenderTexture]()
		{
			outRenderTexture.swap(inRenderTexture1);
			swaped = !swaped;
		};

		// BLOOM
		if (RenderFlags_IsSet(Render_Bloom))
		{
			Pass_Bloom(inRenderTexture1, inRenderTexture2, outRenderTexture);
			SwapTargets();
		}

		// CORRECTION
		if (RenderFlags_IsSet(Render_Correction))
		{
			Pass_Correction(inRenderTexture1->GetShaderResourceView(), outRenderTexture.get());
			SwapTargets();
		}

		// FXAA
		if (RenderFlags_IsSet(Render_FXAA))
		{
			Pass_FXAA(inRenderTexture1->GetShaderResourceView(), outRenderTexture.get());
			SwapTargets();
		}

		// CHROMATIC ABERRATION
		if (RenderFlags_IsSet(Render_ChromaticAberration))
		{
			Pass_ChromaticAberration(inRenderTexture1->GetShaderResourceView(), outRenderTexture.get());
			SwapTargets();
		}

		// SHARPENING
		if (RenderFlags_IsSet(Render_Sharpening))
		{
			Pass_Sharpening(inRenderTexture1->GetShaderResourceView(), outRenderTexture.get());
		}

		// DEBUG - Rendering continues on last bound target
		Pass_DebugGBuffer();
		Pass_Debug();

		m_rhi->EventEnd();
		PROFILE_FUNCTION_END();
	}

	void Renderer::Pass_Correction(void* inTexture, void* outTexture)
	{
		m_rhi->EventBegin("Pass_Correction");

		SetRenderTarget(outTexture, false);
		m_shaderCorrection->Bind();
		m_shaderCorrection->Bind_Buffer(m_wvp_baseOrthographic, Settings::Get().GetResolution());
		m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
		m_rhi->Bind_Texture(0, inTexture);
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhi->EventEnd();
	}

	void Renderer::Pass_FXAA(void* inTexture, void* outTexture)
	{
		m_rhi->EventBegin("Pass_FXAA");

		SetRenderTarget(outTexture, false);
		m_shaderFXAA->Bind();
		m_shaderFXAA->Bind_Buffer(m_wvp_baseOrthographic, Settings::Get().GetResolution());
		m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
		m_rhi->Bind_Texture(0, inTexture);
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhi->EventEnd();
	}

	void Renderer::Pass_Sharpening(void* inTexture, void* outTexture)
	{
		m_rhi->EventBegin("Pass_Sharpening");

		SetRenderTarget(outTexture, false);
		m_shaderSharpening->Bind();
		m_shaderSharpening->Bind_Buffer(m_wvp_baseOrthographic, Settings::Get().GetResolution());
		m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
		m_rhi->Bind_Texture(0, inTexture);
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhi->EventEnd();
	}

	void Renderer::Pass_ChromaticAberration(void* inTexture, void* outTexture)
	{
		m_rhi->EventBegin("Pass_ChromaticAberration");

		SetRenderTarget(outTexture, false);
		m_shaderChromaticAberration->Bind();
		m_shaderChromaticAberration->Bind_Buffer(m_wvp_baseOrthographic, Settings::Get().GetResolution());
		m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
		m_rhi->Bind_Texture(0, inTexture);
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhi->EventEnd();
	}

	void Renderer::Pass_Bloom(shared_ptr<D3D11_RenderTexture>& inSourceTexture, shared_ptr<D3D11_RenderTexture>& inTextureSpare, shared_ptr<D3D11_RenderTexture>& outTexture)
	{
		m_rhi->EventBegin("Pass_Bloom");

		// Bright pass
		SetRenderTarget(inTextureSpare.get(), false);
		m_shaderBloom_Bright->Bind();
		m_shaderBloom_Bright->Bind_Buffer(m_wvp_baseOrthographic, Settings::Get().GetResolution());
		m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
		m_rhi->Bind_Texture(0, inSourceTexture->GetShaderResourceView());
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Horizontal Gaussian blur
		SetRenderTarget(outTexture.get(), false);
		m_shaderBlurGaussianH->Bind();
		m_shaderBlurGaussianH->Bind_Buffer(m_wvp_baseOrthographic, Settings::Get().GetResolution());
		m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
		m_rhi->Bind_Texture(0, inTextureSpare->GetShaderResourceView());
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Vertical Gaussian blur
		SetRenderTarget(inTextureSpare.get(), false);
		m_shaderBlurGaussianV->Bind();
		m_shaderBlurGaussianV->Bind_Buffer(m_wvp_baseOrthographic, Settings::Get().GetResolution());
			m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
		m_rhi->Bind_Texture(0, outTexture->GetShaderResourceView());
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Additive blending
		SetRenderTarget(outTexture.get(), false);
		m_shaderBloom_BlurBlend->Bind();
		m_shaderBloom_BlurBlend->Bind_Buffer(m_wvp_baseOrthographic);
		m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
		m_rhi->Bind_Texture(0, inSourceTexture->GetShaderResourceView());
		m_rhi->Bind_Texture(1, inTextureSpare->GetShaderResourceView());
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhi->EventEnd();
	}

	void Renderer::Pass_Blur(void* texture, void* renderTarget, const Vector2& blurScale)
	{
		m_rhi->EventBegin("Pass_Blur");

		SetRenderTarget(renderTarget, false);
		m_shaderBlurBox->Bind();
		m_shaderBlurBox->Bind_Buffer(m_wvp_baseOrthographic, blurScale);
		m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
		m_rhi->Bind_Texture(0, texture); // Shadows are in the alpha channel
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhi->EventEnd();
	}

	void Renderer::Pass_Shadowing(void* inTextureNormal, void* inTextureDepth, void* inTextureNormalNoise, Light* inDirectionalLight, void* outRenderTexture)
	{
		if (!inDirectionalLight)
			return;

		PROFILE_FUNCTION_BEGIN();
		m_rhi->EventBegin("Pass_Shadowing");

		// SHADOWING (Shadow mapping + SSAO)
		SetRenderTarget(outRenderTexture, false);

		// TEXTURES
		m_texArray.clear();
		m_texArray.shrink_to_fit();
		m_texArray.emplace_back(inTextureNormal);
		m_texArray.emplace_back(inTextureDepth);
		m_texArray.emplace_back(inTextureNormalNoise);
		if (inDirectionalLight)
		{
			m_texArray.emplace_back(inDirectionalLight->ShadowMap_GetShaderResource(0));
			m_texArray.emplace_back(inDirectionalLight->ShadowMap_GetShaderResource(1));
			m_texArray.emplace_back(inDirectionalLight->ShadowMap_GetShaderResource(2));
		}

		// BUFFER
		m_shaderShadowing->Bind();
		m_shaderShadowing->Bind_Buffer(
			m_wvp_baseOrthographic, 
			m_wvp_perspective.Inverted(), 
			m_mV, 
			m_mP_perspective,		
			Settings::Get().GetResolution(), 
			inDirectionalLight,
			m_camera,
			0
		);
		m_rhi->Bind_Sampler(0, m_samplerPointClampGreater->GetSamplerState());	// Shadow mapping
		m_rhi->Bind_Sampler(1, m_samplerLinearClampGreater->GetSamplerState()); // SSAO
		m_rhi->Bind_Textures(RESOURCES_FROM_VECTOR(m_texArray));
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhi->EventEnd();
		PROFILE_FUNCTION_END();
	}
	//=============================================================================================================

	bool Renderer::Pass_DebugGBuffer()
	{
		PROFILE_FUNCTION_BEGIN();
		m_rhi->EventBegin("Pass_DebugGBuffer");

		GBuffer_Texture_Type texType = GBuffer_Target_Unknown;
		texType	= RenderFlags_IsSet(Render_Albedo)		? GBuffer_Target_Albedo		: texType;
		texType = RenderFlags_IsSet(Render_Normal)		? GBuffer_Target_Normal		: texType;
		texType = RenderFlags_IsSet(Render_Specular)	? GBuffer_Target_Specular	: texType;
		texType = RenderFlags_IsSet(Render_Depth)		? GBuffer_Target_Depth		: texType;

		if (texType == GBuffer_Target_Unknown)
		{
			m_rhi->EventEnd();
			return false;
		}

		// TEXTURE
		m_shaderTexture->Bind();
		m_shaderTexture->Bind_Buffer(m_wvp_baseOrthographic, 0);
		m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
		m_rhi->Bind_Texture(0, m_gbuffer->GetShaderResource(texType));
		m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhi->EventEnd();
		PROFILE_FUNCTION_END();

		return true;
	}

	void Renderer::Pass_Debug()
	{
		PROFILE_FUNCTION_BEGIN();
		m_rhi->EventBegin("Pass_Debug");

		//= PRIMITIVES ===================================================================================
		// Anything that is a bunch of vertices (doesn't have a vertex and and index buffer) gets rendered here
		// by passing it's vertices (VertexPosCol) to the LineRenderer. Typically used only for debugging.
		if (m_lineRenderer)
		{
			m_lineRenderer->ClearVertices();

			// Physics
			if (m_flags & Render_Physics)
			{
				g_physics->DebugDraw();
				if (g_physics->GetPhysicsDebugDraw()->IsDirty())
				{
					m_lineRenderer->AddLines(g_physics->GetPhysicsDebugDraw()->GetLines());
				}
			}

			// Picking ray
			if (m_flags & Render_PickingRay)
			{
				m_lineRenderer->AddLines(m_camera->GetPickingRay());
			}

			// bounding boxes
			if (m_flags & Render_AABB)
			{
				for (const auto& renderableWeak : m_renderables)
				{
					if (auto renderable = renderableWeak->GetRenderable_PtrRaw())
					{
						m_lineRenderer->AddBoundigBox(renderable->Geometry_BB(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}
			}

			if (m_lineRenderer->GetVertexCount() != 0)
			{
				m_rhi->EventBegin("Lines");

				// Render
				m_lineRenderer->SetBuffer();
				m_shaderLine->Bind();
				m_shaderLine->Bind_Buffer(Matrix::Identity, m_camera->GetViewMatrix(), m_camera->GetProjectionMatrix());
				m_rhi->Set_PrimitiveTopology(PrimitiveTopology_LineList);
				m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
				m_rhi->Bind_Texture(0, m_gbuffer->GetShaderResource(GBuffer_Target_Depth));
				m_rhi->Draw(m_lineRenderer->GetVertexCount());

				m_rhi->EventEnd();
			}			
		}
		//============================================================================================================

		m_rhi->EnableAlphaBlending(true);

		// Grid
		if (m_flags & Render_SceneGrid)
		{
			m_rhi->EventBegin("Grid");

			m_grid->SetBuffer();
			m_shaderGrid->Bind();
			m_shaderGrid->Bind_Buffer(m_grid->ComputeWorldMatrix(m_camera->GetTransform()) * m_camera->GetViewMatrix() * m_camera->GetProjectionMatrix());
			m_rhi->Set_PrimitiveTopology(PrimitiveTopology_LineList);
			m_rhi->Bind_Sampler(0, m_samplerAnisotropicWrapAlways->GetSamplerState());
			m_rhi->Bind_Texture(0, m_gbuffer->GetShaderResource(GBuffer_Target_Depth));
			m_rhi->DrawIndexed(m_grid->GetIndexCount(), 0, 0);

			m_rhi->EventEnd();
		}

		// Light gizmo
		m_rhi->EventBegin("Gizmos");
		{
			if (m_flags & Render_Light)
			{
				m_rhi->EventBegin("Lights");
				for (auto* light : m_lights)
				{
					Vector3 lightWorldPos = light->GetTransform()->GetPosition();
					Vector3 cameraWorldPos = m_camera->GetTransform()->GetPosition();

					// Compute light screen space position and scale (based on distance from the camera)
					Vector2 lightScreenPos	= m_camera->WorldToScreenPoint(lightWorldPos);
					float distance			= Vector3::Length(lightWorldPos, cameraWorldPos);
					float scale				= GIZMO_MAX_SIZE / distance;
					scale					= Clamp(scale, GIZMO_MIN_SIZE, GIZMO_MAX_SIZE);

					// Skip if the light is not in front of the camera
					if (!m_camera->IsInViewFrustrum(lightWorldPos, Vector3(1.0f)))
						continue;

					// Skip if the light if it's too small
					if (scale == GIZMO_MIN_SIZE)
						continue;

					RHI_Texture* lightTex = nullptr;
					LightType type = light->Getactor_PtrRaw()->GetComponent<Light>().lock()->GetLightType();
					if (type == LightType_Directional)
					{
						lightTex = m_gizmoTexLightDirectional.get();
					}
					else if (type == LightType_Point)
					{
						lightTex = m_gizmoTexLightPoint.get();
					}
					else if (type == LightType_Spot)
					{
						lightTex = m_gizmoTexLightSpot.get();
					}

					// Construct appropriate rectangle
					float texWidth = lightTex->GetWidth() * scale;
					float texHeight = lightTex->GetHeight() * scale;
					m_gizmoRectLight->Create(
						lightScreenPos.x - texWidth * 0.5f,
						lightScreenPos.y - texHeight * 0.5f,
						texWidth,
						texHeight
					);

					m_gizmoRectLight->SetBuffer();
					m_shaderTexture->Bind();
					m_shaderTexture->Bind_Buffer(m_wvp_baseOrthographic, 0);
					m_rhi->Set_PrimitiveTopology(PrimitiveTopology_TriangleList);
					m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
					m_rhi->Bind_Texture(0, lightTex->GetShaderResource());
					m_rhi->DrawIndexed(m_quad->GetIndexCount(), 0, 0);
				}
				m_rhi->EventEnd();
			}

			// Transformation Gizmo
			/*
			m_graphics->EventBegin("Transformation");
			{
				TransformationGizmo* gizmo = m_camera->GetTransformationGizmo();
				gizmo->SetBuffers();
				m_shaderTransformationGizmo->Set();

				// X - Axis
				m_shaderTransformationGizmo->SetBuffer(gizmo->GetTransformationX() * m_mView * m_mProjectionPersp, Vector3::Right, Vector3::Zero, 0);
				m_shaderTransformationGizmo->DrawIndexed(gizmo->GetIndexCount());
				// Y - Axis
				m_shaderTransformationGizmo->SetBuffer(gizmo->GetTransformationY() * m_mView * m_mProjectionPersp, Vector3::Up, Vector3::Zero, 0);
				m_shaderTransformationGizmo->DrawIndexed(gizmo->GetIndexCount());
				// Z - Axis
				m_shaderTransformationGizmo->SetBuffer(gizmo->GetTransformationZ() * m_mView * m_mProjectionPersp, Vector3::Forward, Vector3::Zero, 0);
				m_shaderTransformationGizmo->DrawIndexed(gizmo->GetIndexCount());
			}
			m_graphics->EventEnd();
			*/
		}
		m_rhi->EventEnd();

		// Performance metrics
		if (m_flags & Render_PerformanceMetrics)
		{
			m_font->SetText(Profiler::Get().GetMetrics(), Vector2(-Settings::Get().GetResolutionWidth() * 0.5f + 1.0f, Settings::Get().GetResolutionHeight() * 0.5f));
			m_font->SetVertexAndIndexBuffers();
			m_shaderFont->Bind();
			m_shaderFont->Bind_Buffer(m_wvp_baseOrthographic, m_font->GetColor());
			m_rhi->Set_PrimitiveTopology(PrimitiveTopology_TriangleList);
			m_rhi->Bind_Sampler(0, m_samplerLinearWrapAlways->GetSamplerState());
			m_rhi->Bind_Texture(0, m_font->GetShaderResource());
			m_rhi->DrawIndexed(m_font->GetIndexCount(), 0, 0);
		}

		m_rhi->EnableAlphaBlending(false);

		m_rhi->EventEnd();
		PROFILE_FUNCTION_END();
	}

	const Vector4& Renderer::GetClearColor()
	{
		return m_camera ? m_camera->GetClearColor() : Vector4::Zero;
	}
}
