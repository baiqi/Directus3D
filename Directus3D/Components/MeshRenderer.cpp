/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ===================================
#include "MeshRenderer.h"
#include "Transform.h"
#include "../IO/Serializer.h"
#include "../Logging/Log.h"
#include "../Core/GameObject.h"
#include "../Graphics/Shaders/ShaderVariation.h"
#include "../Graphics/Mesh.h"
#include "../FileSystem/FileSystem.h"
#include "../Resource/ResourceCache.h"
//==============================================

//= NAMESPACES ====================
using namespace std;
using namespace Directus::Math;
using namespace Directus::Resource;
//=================================

MeshRenderer::MeshRenderer()
{
	m_castShadows = true;
	m_receiveShadows = true;
}

MeshRenderer::~MeshRenderer()
{

}

//= ICOMPONENT ===============================================================
void MeshRenderer::Awake()
{
	m_material = g_context->GetSubsystem<ResourceCache>()->GetMaterialStandardDefault();
}

void MeshRenderer::Start()
{

}

void MeshRenderer::Remove()
{

}

void MeshRenderer::Update()
{

}

void MeshRenderer::Serialize()
{
	Serializer::WriteSTR(!m_material.expired() ? m_material.lock()->GetID() : (string)DATA_NOT_ASSIGNED);
	Serializer::WriteBool(m_castShadows);
	Serializer::WriteBool(m_receiveShadows);
}

void MeshRenderer::Deserialize()
{
	m_material = g_context->GetSubsystem<ResourceCache>()->GetResourceByID<Material>(Serializer::ReadSTR());
	m_castShadows = Serializer::ReadBool();
	m_receiveShadows = Serializer::ReadBool();
}
//==============================================================================

//= MISC =======================================================================
void MeshRenderer::Render(unsigned int indexCount) const
{
	auto material = GetMaterial();

	if (material.expired()) // Check if a material exists
	{
		LOG_WARNING("GameObject \"" + g_gameObject->GetName() + "\" has no material. It can't be rendered.");
		return;
	}

	if (!material.lock()->HasShader()) // Check if the material has a shader
	{
		LOG_WARNING("GameObject \"" + g_gameObject->GetName() + "\" has a material but not a shader associated with it. It can't be rendered.");
		return;
	}

	// Set the buffers and draw
	GetMaterial().lock()->GetShader().lock()->Render(indexCount);
}

//==============================================================================

//= PROPERTIES =================================================================
void MeshRenderer::SetCastShadows(bool castShadows)
{
	m_castShadows = castShadows;
}

bool MeshRenderer::GetCastShadows() const
{
	return m_castShadows;
}

void MeshRenderer::SetReceiveShadows(bool receiveShadows)
{
	m_receiveShadows = receiveShadows;
}

bool MeshRenderer::GetReceiveShadows() const
{
	return m_receiveShadows;
}
//==============================================================================

//= MATERIAL ===================================================================
weak_ptr<Material> MeshRenderer::GetMaterial() const
{
	return m_material;
}

void MeshRenderer::SetMaterial(weak_ptr<Material> material)
{
	m_material = g_context->GetSubsystem<ResourceCache>()->AddResource(material.lock());
}

weak_ptr<Material> MeshRenderer::SetMaterial(const string& filePath)
{
	m_material = g_context->GetSubsystem<ResourceCache>()->LoadResource<Material>(filePath);
	return m_material;
}

bool MeshRenderer::HasMaterial() const
{
	return GetMaterial().expired() ? false : true;
}
//==============================================================================